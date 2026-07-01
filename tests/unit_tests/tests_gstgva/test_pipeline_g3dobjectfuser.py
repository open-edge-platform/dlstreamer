# ==============================================================================
# Copyright (C) 2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================

"""Pipeline test for the g3dobjectfuser element.

It reuses the PointPillars demo frame shared with the g3dinference sample
(KITTI frame: camera image, LiDAR point cloud, calibration) and runs the
full fusion pipeline:

    000002.png ! pngdec ! videoconvert ! gvadetect          ! mux.sink_0
    000002.bin ! g3dlidarparse ! g3dinference               ! mux.sink_1
    gvastreammux output-mode=container ! g3dobjectfuser ! gvametaconvert ! gvametapublish (JSON)

The 3D-detection provisioning (PointPillars sources / extension build) is shared
with test_pipeline_g3dinference; the golden 3D detections are imported from it.
"""

import json
import os
import re
import shutil
import tempfile
import unittest

from tests_gstgva.utils import get_model_path
YOLO_MODEL_NAME = "yolo11n"

import gi
gi.require_version("Gst", "1.0")
from gi.repository import Gst  # pylint: disable=no-name-in-module, wrong-import-position

Gst.init([])

# Shared provisioning + golden detections from the g3dinference test (same dir, on sys.path).
from test_pipeline_g3dinference import (  # pylint: disable=wrong-import-position
    EXPECTED_DETECTIONS,
    ensure_pointpillars_extension,
    ensure_pointpillars_root,
)


def build_kitti_calibration(calib_txt_path):
    """Build the g3dobjectfuser calibration dict from a KITTI txt file.

    KITTI stores R0_rect as 3x3 and Tr_velo_to_cam as 3x4; the fuser wants both
    as 4x4 homogeneous matrices and P2 as the raw 3x4 (12-vector).
    """
    with open(calib_txt_path, "r", encoding="utf-8") as handle:
        txt = handle.read()

    def row(name, count):
        match = re.search(rf"^{name}:\s*([-\d.eE+\s]+)", txt, re.M)
        if not match:
            raise unittest.SkipTest(f"calibration key not found in {calib_txt_path}: {name}")
        values = [float(token) for token in match.group(1).split()[:count]]
        if len(values) != count:
            raise unittest.SkipTest(f"{name}: expected {count} values, got {len(values)}")
        return values

    p2 = row("P2", 12)
    r0 = row("R0_rect", 9)
    tr = row("Tr_velo_to_cam", 12)
    r0_4x4 = [r0[0], r0[1], r0[2], 0, r0[3], r0[4], r0[5], 0, r0[6], r0[7], r0[8], 0, 0, 0, 0, 1]
    tr_4x4 = [tr[0], tr[1], tr[2], tr[3], tr[4], tr[5], tr[6], tr[7], tr[8], tr[9], tr[10], tr[11], 0, 0, 0, 1]
    return {"p2": p2, "r0_rect": r0_4x4, "tr_velo_to_cam": tr_4x4}


class TestG3DObjectFuser(unittest.TestCase):
    # Same tolerances as test_pipeline_g3dinference: the fuser forwards the 3D
    # detections unchanged, so they must still match the golden values.
    BOX_DELTA = 1e-2
    CONFIDENCE_DELTA = 2e-2

    @classmethod
    def setUpClass(cls):
        if Gst.ElementFactory.make("g3dobjectfuser", None) is None:
            raise unittest.SkipTest("g3dobjectfuser element not available")
        cls.pointpillars_root = ensure_pointpillars_root()
        cls.extension_lib = ensure_pointpillars_extension(cls.pointpillars_root)
        cls.yolo_model = get_model_path(YOLO_MODEL_NAME)

    def setUp(self):
        self.test_dir = tempfile.mkdtemp(prefix="g3dobjectfuser_test_")
        self.test_output = os.path.join(self.test_dir, "g3dobjectfuser_output.json")
        self.calib_json = os.path.join(self.test_dir, "kitti_000002.json")

        demo_dir = os.path.join(self.pointpillars_root, "pointpillars", "dataset", "demo_data", "test")
        self.pc_file = os.path.join(demo_dir, "000002.bin")
        self.image_file = os.path.join(demo_dir, "000002.png")
        self.calib_txt = os.path.join(demo_dir, "000002.txt")
        self.frame_index = 2
        self.image_pattern = os.path.join(demo_dir, "%06d.png")
        self.lidar_pattern = os.path.join(demo_dir, "%06d.bin")

        self.config_file = os.path.join(self.test_dir, "pointpillars_ov_config.json")
        self.voxel_model = os.path.join(self.pointpillars_root, "pretrained", "pointpillars_ov_pillar_layer.xml")
        self.nn_model = os.path.join(self.pointpillars_root, "pretrained", "pointpillars_ov_nn.xml")
        self.postproc_model = os.path.join(self.pointpillars_root, "pretrained", "pointpillars_ov_postproc.xml")

    def tearDown(self):
        shutil.rmtree(self.test_dir, ignore_errors=True)

    # --- helpers --------------------------------------------------------------

    def _assert_runtime_assets_exist(self):
        for label, path in (
            ("camera image", self.image_file),
            ("point cloud", self.pc_file),
            ("calibration", self.calib_txt),
            ("PointPillars extension library", self.extension_lib),
            ("PointPillars voxel model", self.voxel_model),
            ("PointPillars NN model", self.nn_model),
            ("PointPillars postproc model", self.postproc_model),
            ("YOLO model", self.yolo_model),
        ):
            if not os.path.exists(path):
                self.skipTest(f"{label} not found: {path}")

    def _write_runtime_config(self):
        config_payload = {
            "voxel_params": {
                "voxel_size": [0.16, 0.16, 4],
                "point_cloud_range": [0, -39.68, -3, 69.12, 39.68, 1],
                "max_num_points": 32,
                "max_voxels": 16000,
            },
            "extension_lib": self.extension_lib,
            "voxel_model": self.voxel_model,
            "nn_model": self.nn_model,
            "postproc_model": self.postproc_model,
        }
        with open(self.config_file, "w", encoding="utf-8") as handle:
            json.dump(config_payload, handle)

    def _write_calibration(self):
        calibration = build_kitti_calibration(self.calib_txt)
        with open(self.calib_json, "w", encoding="utf-8") as handle:
            json.dump(calibration, handle)

    def _run_pipeline(self, pipeline):
        exceptions = []
        gst_pipeline = Gst.parse_launch(pipeline)
        bus = gst_pipeline.get_bus()

        state_change = gst_pipeline.set_state(Gst.State.PLAYING)

        timeout = 10 * Gst.SECOND
        drain_timeout = Gst.SECOND // 2
        saw_error = False

        while True:
            wait_timeout = drain_timeout if saw_error else timeout
            msg = bus.timed_pop_filtered(wait_timeout, Gst.MessageType.ERROR | Gst.MessageType.EOS)
            if msg is None:
                break
            if msg.type is Gst.MessageType.ERROR:
                exceptions.append(msg.parse_error())
                saw_error = True
                continue
            if msg.type is Gst.MessageType.EOS and not saw_error:
                break

        if state_change == Gst.StateChangeReturn.FAILURE and not exceptions:
            exceptions.append((RuntimeError("Pipeline failed to start"), None))

        gst_pipeline.set_state(Gst.State.NULL)
        return exceptions

    @staticmethod
    def _format_exception(exception):
        if isinstance(exception, tuple):
            error = exception[0] if exception else None
            debug = exception[1] if len(exception) > 1 else None
            parts = []
            if error is not None:
                parts.append(getattr(error, "message", str(error)))
            if debug:
                parts.append(str(debug))
            if parts:
                return " | ".join(parts)
        return str(exception)

    def _parse_records(self, content):
        """Parse gvametapublish file-format=2 (json-lines) output.
        """
        decoder = json.JSONDecoder()
        records = []
        index = 0
        length = len(content)
        while index < length:
            while index < length and content[index] in " \t\r\n":
                index += 1
            if index >= length:
                break
            obj, index = decoder.raw_decode(content, index)
            records.append(obj)
        self.assertGreater(len(records), 0, "Output JSON contained no records")
        return records

    def _collect_streams(self, records, key):
        """All per-stream dicts (across every batch record) that carry @key."""
        streams = []
        for record in records:
            record_streams = record.get("streams")
            self.assertIsInstance(record_streams, list, "Batch JSON must contain a 'streams' array")
            for stream in record_streams:
                if key in stream:
                    streams.append(stream)
        return streams

    def _extract_3d_objects(self, stream):
        objects = stream.get("objects_3d")
        self.assertIsInstance(objects, list, "3D stream must contain an 'objects_3d' array")
        parsed = []
        for obj in objects:
            bbox = obj.get("bbox_3d", {})
            parsed.append(
                {
                    "label_id": obj.get("label_id"),
                    "confidence": obj.get("confidence"),
                    "modality": obj.get("modality"),
                    "bbox_3d": {field: bbox.get(field) for field in ("x", "y", "z", "w", "l", "h", "yaw")},
                }
            )
        return sorted(parsed, key=lambda item: item["confidence"], reverse=True)

    def _assert_3d_detections_match(self, threed_streams):
        # The single LiDAR frame is processed exactly once, so exactly one stream
        # across all batches carries 3D detections. More than one would mean the
        # 3D stream was duplicated across batches.
        self.assertEqual(
            len(threed_streams), 1,
            f"Expected exactly one 3D stream across batches, got {len(threed_streams)}",
        )
        actual = self._extract_3d_objects(threed_streams[0])
        # Exact count is the regression guard against dropped / duplicated 3D mtds
        # (the fuser must reuse g3dinference's mtds, never re-emit them).
        self.assertEqual(
            len(actual), len(EXPECTED_DETECTIONS),
            f"Expected {len(EXPECTED_DETECTIONS)} 3D detections, got {len(actual)}:\n"
            f"{json.dumps(actual, indent=2)}",
        )
        for index, (got, want) in enumerate(zip(actual, EXPECTED_DETECTIONS)):
            self.assertEqual(got["modality"], "lidar", f"object {index} modality must be 'lidar'")
            self.assertEqual(got["label_id"], want["label_id"], f"label_id mismatch for 3D object {index}")
            self.assertAlmostEqual(
                got["confidence"], want["confidence"], delta=self.CONFIDENCE_DELTA,
                msg=f"confidence mismatch for 3D object {index}",
            )
            for field in ("x", "y", "z", "w", "l", "h", "yaw"):
                self.assertAlmostEqual(
                    got["bbox_3d"][field], want["bbox_3d"][field], delta=self.BOX_DELTA,
                    msg=f"bbox_3d.{field} mismatch for 3D object {index}",
                )

    def _assert_camera_detections(self, camera_streams):
        self.assertGreater(len(camera_streams), 0, "Batch JSON has no camera stream (with 'objects')")

        total_objects = 0
        tracked = 0
        for stream in camera_streams:
            objects = stream.get("objects")
            self.assertIsInstance(objects, list, "Camera stream must contain an 'objects' array")
            for obj in objects:
                total_objects += 1
                detection = obj.get("detection")
                self.assertIsInstance(detection, dict, "camera object missing 'detection'")
                self.assertIn("bounding_box", detection, "camera detection missing 'bounding_box'")
                self.assertIn("label_id", detection, "camera detection missing 'label_id'")
                if isinstance(obj.get("id"), int):
                    tracked += 1

        self.assertGreater(total_objects, 0, "Camera stream produced no 2D detections")
        # The fuser runs a per-camera vas::ot tracker; at least one detection must
        # carry a tracker id (serialized as the object's "id").
        self.assertGreater(tracked, 0, "No camera detection carried a tracker id (per-camera tracking did not run)")

    # --- tests ----------------------------------------------------------------

    def test_g3dobjectfuser_camera_lidar_pipeline(self):
        self._assert_runtime_assets_exist()
        self._write_runtime_config()
        self._write_calibration()

        pipeline = (
            f'multifilesrc location="{self.image_pattern}" '
            f'start-index={self.frame_index} stop-index={self.frame_index} caps=image/png ! '
            f'pngdec ! videoconvert ! video/x-raw,format=BGR ! '
            f'gvadetect model="{self.yolo_model}" pre-process-backend=opencv device=CPU ! mux.sink_0 '
            f'multifilesrc location="{self.lidar_pattern}" '
            f'start-index={self.frame_index} stop-index={self.frame_index} caps=application/octet-stream ! '
            f'g3dlidarparse ! g3dinference config="{self.config_file}" device=CPU ! mux.sink_1 '
            f'gvastreammux name=mux output-mode=container sync-mode=first-pts ! '
            f'g3dobjectfuser calibration="{self.calib_json}" ! '
            f'gvametaconvert format=json json-indent=2 ! '
            f'gvametapublish file-format=2 file-path="{self.test_output}" ! '
            f'fakesink'
        )

        exceptions = self._run_pipeline(pipeline)
        self.assertEqual(
            len(exceptions), 0,
            "Pipeline should run without errors: "
            + "\n".join(self._format_exception(exc) for exc in exceptions),
        )
        self.assertTrue(os.path.exists(self.test_output), "Output JSON file was not created")

        with open(self.test_output, "r", encoding="utf-8") as handle:
            content = handle.read().strip()
        self.assertTrue(content, "Output JSON file is empty")
        records = self._parse_records(content)

        camera_streams = self._collect_streams(records, "objects")
        threed_streams = self._collect_streams(records, "objects_3d")

        self._assert_3d_detections_match(threed_streams)
        self._assert_camera_detections(camera_streams)

        serialized = json.dumps(records)
        self.assertIn("bbox_3d", serialized)
        self.assertIn("lidar", serialized)

    def test_g3dobjectfuser_missing_calibration_pipeline(self):
        # Without the required 'calibration' property the element must refuse to
        # start, failing the pipeline rather than passing data through.
        pipeline = (
            'appsrc caps=multistream/x-analytics-batch ! '
            'g3dobjectfuser ! '
            'fakesink'
        )
        exceptions = self._run_pipeline(pipeline)
        formatted = [self._format_exception(exc) for exc in exceptions]
        self.assertNotEqual(len(formatted), 0, "Pipeline was expected to fail without 'calibration'")
        joined = "\n".join(formatted)
        self.assertTrue(
            "calibration" in joined
            or "Pipeline failed to start" in joined
            or "Internal data stream error" in joined,
            f"Expected a startup failure in pipeline errors:\n{joined}",
        )


if __name__ == "__main__":
    unittest.main()
