# ==============================================================================
# Copyright (C) 2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================

"""Unit tests for Tensor.convert_to_meta() and Tensor.convert_to_tensor()
roundtrip conversion between GstStructure tensors and GstAnalytics metadata."""

import sys
import unittest

import numpy

import gi
gi.require_version('Gst', '1.0')
gi.require_version('GstAnalytics', '1.0')
gi.require_version('DLStreamerMeta', '1.0')

from gi.repository import Gst, GstAnalytics, GLib, DLStreamerMeta  # pylint: disable=no-name-in-module

# Trigger _wrap_mtd() registration for DLStreamerMeta types
from gstgva.region_of_interest import RegionOfInterest  # noqa: F401

from gstgva.tensor import Tensor
from gstgva.util import libgst

Gst.init(sys.argv)


# ── Test data matching real pipeline output ──────────────────────────────────

# From: gvadetect with yolo11s-pose model
# GstStructure: keypoints, type=(string)keypoints, format=(string)body-pose/coco-17, ...
KEYPOINT_CONFIDENCES = [
    0.97998046875, 0.9677734375, 0.8984375, 0.888671875, 0.416259765625,
    0.9921875, 0.98388671875, 0.9677734375, 0.91845703125, 0.947265625,
    0.88818359375, 0.97412109375, 0.96240234375, 0.0, 0.71826171875,
    0.0, 0.0,
]

KEYPOINT_COUNT = 17
KEYPOINT_DIM = 2

# Normalized float positions (17 keypoints x 2 coords)
# Simulated values in [0,1] range
KEYPOINT_POSITIONS_NORM = [
    0.5, 0.3,    # nose
    0.52, 0.28,  # eye_l
    0.48, 0.28,  # eye_r
    0.56, 0.27,  # ear_l
    0.44, 0.27,  # ear_r
    0.60, 0.40,  # shoulder_l
    0.40, 0.40,  # shoulder_r
    0.65, 0.55,  # elbow_l
    0.35, 0.55,  # elbow_r
    0.63, 0.70,  # wrist_l
    0.37, 0.70,  # wrist_r
    0.58, 0.70,  # hip_l
    0.42, 0.70,  # hip_r
    0.60, 0.85,  # knee_l
    0.40, 0.85,  # knee_r
    0.61, 0.95,  # ankle_l
    0.39, 0.95,  # ankle_r
]

# Bounding box for the person (pixel coordinates)
OD_X, OD_Y, OD_W, OD_H = 100, 50, 200, 400


def _coco17_descriptor():
    """Look up the COCO-17 keypoint descriptor."""
    desc = DLStreamerMeta.KeypointDescriptor.lookup("body-pose/coco-17")
    assert desc is not None
    return desc


def _descriptor_point_names(desc):
    """Extract all point names from a keypoint descriptor."""
    return [desc.get_point_name(i) for i in range(desc.get_point_count())]


def _descriptor_skeleton(desc):
    """Extract skeleton connections as a flat list from a keypoint descriptor."""
    skeleton = []
    for i in range(desc.get_skeleton_connection_count()):
        ok, from_idx, to_idx = desc.get_skeleton_connection(i)
        assert ok
        skeleton.extend([from_idx, to_idx])
    return skeleton


def _build_keypoint_tensor():
    """Create a keypoints Tensor matching real pipeline output structure."""
    desc = _coco17_descriptor()
    structure = libgst.gst_structure_new_empty(b"keypoints")
    tensor = Tensor(structure)

    tensor["iou_threshold"] = 0.7
    tensor["converter"] = "yolo_v11_pose"
    tensor["confidence_threshold"] = 0.5
    tensor["layer_name"] = "output"
    tensor["model_name"] = "Model0"
    tensor.set_type("keypoints")
    tensor.set_format(desc.get_semantic_tag())
    tensor.set_dims([KEYPOINT_COUNT, KEYPOINT_DIM])
    tensor.set_data(numpy.array(KEYPOINT_POSITIONS_NORM, dtype=numpy.float32))
    tensor.set_precision(Tensor.PRECISION.FP32)
    tensor.set_vector("confidence", KEYPOINT_CONFIDENCES, value_type="float")
    tensor.set_vector("point_names", _descriptor_point_names(desc), value_type="string")
    tensor.set_vector("point_connections", _descriptor_skeleton(desc), value_type="uint")

    return tensor


# Classification test data — from real pipeline output
CLS_LABEL = "neutral"
CLS_CONFIDENCE = 0.53389871120452881
CLS_LABEL_ID = 5
CLS_TENSOR_ID = 0
CLS_DATA = numpy.array([
    0.01, 0.02, 0.05, 0.03, 0.10,
    0.534, 0.08, 0.06, 0.04, 0.076,
], dtype=numpy.float32)


def _build_classification_tensor():
    """Create a classification_result Tensor matching real pipeline output."""
    structure = libgst.gst_structure_new_empty(b"classification_layer_name:output")
    tensor = Tensor(structure)

    tensor["converter"] = "label"
    tensor["method"] = "softmax"
    tensor["layer_name"] = "output"
    tensor["model_name"] = "torch_jit"
    tensor.set_data(CLS_DATA)
    tensor.set_precision(Tensor.PRECISION.FP32)
    tensor["layout"] = Tensor.LAYOUT.ANY.value
    tensor.set_dims([1, 10])
    tensor["label"] = CLS_LABEL
    tensor["label_id"] = CLS_LABEL_ID
    tensor["confidence"] = CLS_CONFIDENCE
    tensor["tensor_id"] = CLS_TENSOR_ID
    tensor.set_type("classification_result")

    return tensor


# ── Keypoints: convert_to_meta tests ────────────────────────────────────────

class KeypointConvertToMetaTestCase(unittest.TestCase):
    """Test Tensor.convert_to_meta() for keypoints type tensors."""

    def setUp(self):
        self.buffer = Gst.Buffer.new_allocate(None, 0, None)
        self.rmeta = GstAnalytics.buffer_add_analytics_relation_meta(self.buffer)
        # Add a bounding box for the person
        ok, self.od_mtd = self.rmeta.add_od_mtd(
            GLib.quark_from_string("person"), OD_X, OD_Y, OD_W, OD_H, 0.9)
        self.assertTrue(ok)

    def test_returns_group_mtd(self):
        tensor = _build_keypoint_tensor()
        mtd = tensor.convert_to_meta(self.rmeta, self.od_mtd)
        self.assertIsNotNone(mtd)
        self.assertIsInstance(mtd, DLStreamerMeta.GroupMtd)

    def test_group_has_correct_member_count(self):
        tensor = _build_keypoint_tensor()
        group = tensor.convert_to_meta(self.rmeta, self.od_mtd)
        self.assertEqual(group.get_member_count(), KEYPOINT_COUNT)

    def test_group_has_semantic_tag(self):
        tensor = _build_keypoint_tensor()
        group = tensor.convert_to_meta(self.rmeta, self.od_mtd)
        self.assertEqual(group.get_semantic_tag(),
                         _coco17_descriptor().get_semantic_tag())

    def test_keypoint_positions_are_pixel_coords(self):
        """Positions should be converted from normalized to pixel coords using bbox."""
        tensor = _build_keypoint_tensor()
        group = tensor.convert_to_meta(self.rmeta, self.od_mtd)

        for k in range(KEYPOINT_COUNT):
            ok, member = group.get_member(k)
            self.assertTrue(ok)
            ok, kp = DLStreamerMeta.relation_meta_get_keypoint_mtd(self.rmeta, member.id)
            self.assertTrue(ok)
            ok, px, py, pz, dim = kp.get_position()
            self.assertTrue(ok)

            norm_x = numpy.float32(KEYPOINT_POSITIONS_NORM[k * 2])
            norm_y = numpy.float32(KEYPOINT_POSITIONS_NORM[k * 2 + 1])
            expected_x = OD_X + int(OD_W * float(norm_x))
            expected_y = OD_Y + int(OD_H * float(norm_y))
            self.assertAlmostEqual(px, expected_x, delta=1, msg=f"keypoint {k} x mismatch")
            self.assertAlmostEqual(py, expected_y, delta=1, msg=f"keypoint {k} y mismatch")

    def test_keypoint_confidences(self):
        tensor = _build_keypoint_tensor()
        group = tensor.convert_to_meta(self.rmeta, self.od_mtd)

        for k in range(KEYPOINT_COUNT):
            ok, member = group.get_member(k)
            self.assertTrue(ok)
            ok, kp = DLStreamerMeta.relation_meta_get_keypoint_mtd(self.rmeta, member.id)
            self.assertTrue(ok)
            ok, conf = kp.get_confidence()
            self.assertTrue(ok)
            self.assertAlmostEqual(conf, KEYPOINT_CONFIDENCES[k], places=3,
                                   msg=f"keypoint {k} confidence mismatch")

    def test_without_od_meta_raises(self):
        """Without bounding box, convert_to_meta should raise ValueError."""
        tensor = _build_keypoint_tensor()
        with self.assertRaises(ValueError):
            tensor.convert_to_meta(self.rmeta, od_meta=None)

    def test_unknown_type_returns_none(self):
        """Tensor with unsupported type should return None."""
        structure = libgst.gst_structure_new_empty(b"unknown")
        tensor = Tensor(structure)
        tensor["type"] = "some_unsupported_type"
        mtd = tensor.convert_to_meta(self.rmeta)
        self.assertIsNone(mtd)


# ── Keypoints: convert_to_tensor tests ──────────────────────────────────────

class KeypointConvertToTensorTestCase(unittest.TestCase):
    """Test Tensor.convert_to_tensor() for GroupMtd (keypoints) metadata."""

    def setUp(self):
        self.buffer = Gst.Buffer.new_allocate(None, 0, None)
        self.rmeta = GstAnalytics.buffer_add_analytics_relation_meta(self.buffer)

        # Add OD mtd for the person
        ok, self.od_mtd = self.rmeta.add_od_mtd(
            GLib.quark_from_string("person"), OD_X, OD_Y, OD_W, OD_H, 0.9)
        self.assertTrue(ok)

        # Keep original tensor for comparison
        self.original = _build_keypoint_tensor()

        # Convert tensor → meta (to get a proper GroupMtd)
        self.group_mtd = self.original.convert_to_meta(self.rmeta, self.od_mtd)
        self.assertIsNotNone(self.group_mtd)

        # Set IS_PART_OF relation so convert_to_tensor can find bbox
        self.rmeta.set_relation(
            GstAnalytics.RelTypes.IS_PART_OF,
            self.group_mtd.id, self.od_mtd.id)

    def _roundtrip_tensor(self):
        """Perform convert_to_tensor and return result as Tensor object."""
        structure = Tensor.convert_to_tensor(self.group_mtd)
        self.assertIsNotNone(structure)
        return Tensor(structure)

    def test_name(self):
        t = self._roundtrip_tensor()
        self.assertEqual(t.name(), self.original.name())

    def test_type(self):
        t = self._roundtrip_tensor()
        self.assertEqual(t.type(), self.original.type())

    def test_format(self):
        t = self._roundtrip_tensor()
        self.assertEqual(t.format(), self.original.format())

    def test_precision(self):
        t = self._roundtrip_tensor()
        self.assertEqual(t.precision(), self.original.precision())

    def test_dims(self):
        t = self._roundtrip_tensor()
        self.assertEqual(t.dims(), self.original.dims())

    def test_data_roundtrips_positions(self):
        """After pixel→normalized roundtrip, positions should approximately match original."""
        t = self._roundtrip_tensor()
        orig_data = self.original.data()
        rest_data = t.data()
        self.assertEqual(len(rest_data), len(orig_data))

        for i in range(len(orig_data)):
            self.assertAlmostEqual(rest_data[i], orig_data[i], delta=0.01,
                                   msg=f"position[{i}] roundtrip mismatch")

    def test_confidences_roundtrip(self):
        t = self._roundtrip_tensor()
        orig_conf = list(self.original.confidence())
        rest_conf = list(t.confidence())
        self.assertEqual(len(rest_conf), len(orig_conf))
        for k in range(len(orig_conf)):
            self.assertAlmostEqual(rest_conf[k], orig_conf[k], places=3,
                                   msg=f"keypoint {k} confidence roundtrip mismatch")

    def test_point_names_roundtrip(self):
        t = self._roundtrip_tensor()
        self.assertEqual(t["point_names"], self.original["point_names"])

    def test_skeleton_roundtrip(self):
        t = self._roundtrip_tensor()
        orig_conn = list(self.original["point_connections"])
        rest_conn = list(t["point_connections"])
        # Order of pairs may differ due to relation iteration order
        orig_pairs = set(zip(orig_conn[::2], orig_conn[1::2]))
        rest_pairs = set(zip(rest_conn[::2], rest_conn[1::2]))
        self.assertEqual(rest_pairs, orig_pairs)

    def test_empty_group_returns_none(self):
        """A group with zero keypoint members should return None."""
        ok, empty_group = DLStreamerMeta.relation_meta_add_group_mtd(self.rmeta, 0)
        self.assertTrue(ok)
        structure = Tensor.convert_to_tensor(empty_group)
        self.assertIsNone(structure)


# ── Classification: convert_to_meta tests ───────────────────────────────────

class ClassificationConvertToMetaTestCase(unittest.TestCase):
    """Test Tensor.convert_to_meta() for classification_result tensors."""

    def setUp(self):
        self.buffer = Gst.Buffer.new_allocate(None, 0, None)
        self.rmeta = GstAnalytics.buffer_add_analytics_relation_meta(self.buffer)

    def test_returns_cls_mtd(self):
        tensor = _build_classification_tensor()
        mtd = tensor.convert_to_meta(self.rmeta)
        self.assertIsNotNone(mtd)
        self.assertIsInstance(mtd, GstAnalytics.ClsMtd)

    def test_cls_mtd_confidence(self):
        tensor = _build_classification_tensor()
        mtd = tensor.convert_to_meta(self.rmeta)
        conf = mtd.get_level(0)
        self.assertAlmostEqual(conf, 0.53389871120452881, places=5)

    def test_cls_mtd_label_quark(self):
        tensor = _build_classification_tensor()
        mtd = tensor.convert_to_meta(self.rmeta)
        quark = mtd.get_quark(0)
        label = GLib.quark_to_string(quark)
        self.assertEqual(label, "neutral")


# ── Classification: convert_to_tensor tests ─────────────────────────────────

class ClassificationConvertToTensorTestCase(unittest.TestCase):
    """Test Tensor.convert_to_tensor() for ClsMtd metadata."""

    def setUp(self):
        self.buffer = Gst.Buffer.new_allocate(None, 0, None)
        self.rmeta = GstAnalytics.buffer_add_analytics_relation_meta(self.buffer)

        # Keep original tensor for comparison
        self.original = _build_classification_tensor()
        self.cls_mtd = self.original.convert_to_meta(self.rmeta)
        self.assertIsNotNone(self.cls_mtd)

    def _roundtrip_tensor(self):
        """Perform convert_to_tensor and return result as Tensor object."""
        structure = Tensor.convert_to_tensor(self.cls_mtd)
        self.assertIsNotNone(structure)
        return Tensor(structure)

    def test_name(self):
        t = self._roundtrip_tensor()
        # Name changes: input "classification_layer_name:output" → output "classification"
        self.assertEqual(t.name(), "classification")

    def test_type(self):
        t = self._roundtrip_tensor()
        self.assertEqual(t.type(), self.original.type())

    def test_label(self):
        t = self._roundtrip_tensor()
        self.assertEqual(t.label(), self.original.label())

    def test_confidence(self):
        t = self._roundtrip_tensor()
        # confidence goes through: double → gfloat (in ClsMtd) → double (in tensor)
        self.assertAlmostEqual(t.confidence(), self.original.confidence(), places=5)


# ── Roundtrip: tensor → meta → tensor ──────────────────────────────────────

class KeypointFullRoundtripTestCase(unittest.TestCase):
    """Full roundtrip: build keypoint tensor → convert_to_meta → convert_to_tensor
    and verify the reconstructed tensor matches the original (within lossy limits)."""

    def setUp(self):
        self.buffer = Gst.Buffer.new_allocate(None, 0, None)
        self.rmeta = GstAnalytics.buffer_add_analytics_relation_meta(self.buffer)
        ok, self.od_mtd = self.rmeta.add_od_mtd(
            GLib.quark_from_string("person"), OD_X, OD_Y, OD_W, OD_H, 0.9)

    def test_full_roundtrip(self):
        original = _build_keypoint_tensor()

        # tensor → meta
        group = original.convert_to_meta(self.rmeta, self.od_mtd)
        self.assertIsNotNone(group)

        # Set IS_PART_OF so convert_to_tensor finds the bbox
        self.rmeta.set_relation(
            GstAnalytics.RelTypes.IS_PART_OF,
            group.id, self.od_mtd.id)

        # meta → tensor
        structure = Tensor.convert_to_tensor(group)
        self.assertIsNotNone(structure)
        restored = Tensor(structure)

        # Verify preserved fields
        self.assertEqual(restored.type(), original.type())
        self.assertEqual(restored.format(), original.format())
        self.assertEqual(restored.precision(), original.precision())
        self.assertEqual(restored.dims(), original.dims())
        self.assertEqual(restored.name(), "keypoints")

        # Verify positions approximately match (lossy due to float→int→float)
        orig_data = original.data()
        rest_data = restored.data()
        self.assertEqual(len(orig_data), len(rest_data))
        for i in range(len(orig_data)):
            self.assertAlmostEqual(
                rest_data[i], orig_data[i], delta=0.01,
                msg=f"position[{i}] roundtrip mismatch")

        # Verify confidences match
        orig_conf = list(original.confidence())
        rest_conf = list(restored.confidence())
        self.assertEqual(len(orig_conf), len(rest_conf))
        for i in range(len(orig_conf)):
            self.assertAlmostEqual(
                rest_conf[i], orig_conf[i], places=3,
                msg=f"confidence[{i}] roundtrip mismatch")

        # Verify skeleton connections match (order may differ)
        orig_pc = list(original["point_connections"])
        rest_pc = list(restored["point_connections"])
        orig_pairs = set(zip(orig_pc[::2], orig_pc[1::2]))
        rest_pairs = set(zip(rest_pc[::2], rest_pc[1::2]))
        self.assertEqual(rest_pairs, orig_pairs)


if __name__ == '__main__':
    unittest.main()
