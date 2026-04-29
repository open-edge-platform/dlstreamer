# ==============================================================================
# Copyright (C) 2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================

"""Unit tests for DLStreamerMeta.KeypointDescriptor and related Python bindings."""

import sys
import unittest

import gi
gi.require_version('Gst', '1.0')
gi.require_version('GstAnalytics', '1.0')
gi.require_version('DLStreamerMeta', '1.0')

from gi.repository import Gst, GstAnalytics, DLStreamerMeta  # pylint: disable=no-name-in-module

Gst.init(sys.argv)


class KeypointDescriptorTestCase(unittest.TestCase):
    """Tests for DLStreamerMeta.KeypointDescriptor Python bindings."""

    def test_lookup_coco17(self):
        desc = DLStreamerMeta.KeypointDescriptor.lookup('body-pose/coco-17')
        self.assertIsNotNone(desc)

    def test_lookup_openpose18(self):
        desc = DLStreamerMeta.KeypointDescriptor.lookup(
            'body-pose/openpose-18')
        self.assertIsNotNone(desc)

    def test_lookup_hrnet_coco17(self):
        desc = DLStreamerMeta.KeypointDescriptor.lookup(
            'body-pose/hrnet-coco-17')
        self.assertIsNotNone(desc)

    def test_lookup_centerface5(self):
        desc = DLStreamerMeta.KeypointDescriptor.lookup(
            'face-landmarks/centerface-5')
        self.assertIsNotNone(desc)

    def test_lookup_unknown_returns_none(self):
        desc = DLStreamerMeta.KeypointDescriptor.lookup('nonexistent-tag')
        self.assertIsNone(desc)

    def test_lookup_none_raises_type_error(self):
        with self.assertRaises(TypeError):
            DLStreamerMeta.KeypointDescriptor.lookup(None)

    def test_namespace_level_lookup(self):
        desc = DLStreamerMeta.keypoint_descriptor_lookup('body-pose/coco-17')
        self.assertIsNotNone(desc)

    def test_semantic_tag_coco17(self):
        desc = DLStreamerMeta.KeypointDescriptor.lookup('body-pose/coco-17')
        self.assertEqual(desc.semantic_tag, 'body-pose/coco-17')

    def test_semantic_tag_openpose18(self):
        desc = DLStreamerMeta.KeypointDescriptor.lookup(
            'body-pose/openpose-18')
        self.assertEqual(desc.semantic_tag, 'body-pose/openpose-18')

    def test_point_count_coco17(self):
        desc = DLStreamerMeta.KeypointDescriptor.lookup('body-pose/coco-17')
        self.assertEqual(desc.point_count, 17)

    def test_point_count_openpose18(self):
        desc = DLStreamerMeta.KeypointDescriptor.lookup(
            'body-pose/openpose-18')
        self.assertEqual(desc.point_count, 18)

    def test_point_count_centerface5(self):
        desc = DLStreamerMeta.KeypointDescriptor.lookup(
            'face-landmarks/centerface-5')
        self.assertEqual(desc.point_count, 5)

    def test_get_point_name_coco17_first(self):
        desc = DLStreamerMeta.KeypointDescriptor.lookup('body-pose/coco-17')
        self.assertEqual(desc.get_point_name(0), 'nose')

    def test_get_point_name_coco17_last(self):
        desc = DLStreamerMeta.KeypointDescriptor.lookup('body-pose/coco-17')
        self.assertEqual(desc.get_point_name(16), 'ankle_r')

    def test_get_point_name_openpose18_neck(self):
        desc = DLStreamerMeta.KeypointDescriptor.lookup(
            'body-pose/openpose-18')
        self.assertEqual(desc.get_point_name(1), 'neck')

    def test_get_point_name_centerface5(self):
        desc = DLStreamerMeta.KeypointDescriptor.lookup(
            'face-landmarks/centerface-5')
        self.assertEqual(desc.get_point_name(2), 'nose_tip')

    def test_get_point_name_out_of_range(self):
        desc = DLStreamerMeta.KeypointDescriptor.lookup('body-pose/coco-17')
        self.assertIsNone(desc.get_point_name(100))

    def test_skeleton_connection_count_coco17(self):
        desc = DLStreamerMeta.KeypointDescriptor.lookup('body-pose/coco-17')
        self.assertEqual(desc.skeleton_connection_count, 18)

    def test_skeleton_connection_count_openpose18(self):
        desc = DLStreamerMeta.KeypointDescriptor.lookup(
            'body-pose/openpose-18')
        self.assertEqual(desc.skeleton_connection_count, 13)

    def test_skeleton_connection_count_centerface5(self):
        desc = DLStreamerMeta.KeypointDescriptor.lookup(
            'face-landmarks/centerface-5')
        self.assertEqual(desc.skeleton_connection_count, 0)

    def test_get_skeleton_connection_coco17_first(self):
        desc = DLStreamerMeta.KeypointDescriptor.lookup('body-pose/coco-17')
        ok, from_idx, to_idx = desc.get_skeleton_connection(0)
        self.assertTrue(ok)
        self.assertEqual(from_idx, 0)  # nose
        self.assertEqual(to_idx, 1)    # eye_l

    def test_get_skeleton_connection_coco17_last(self):
        desc = DLStreamerMeta.KeypointDescriptor.lookup('body-pose/coco-17')
        ok, from_idx, to_idx = desc.get_skeleton_connection(17)
        self.assertTrue(ok)
        self.assertEqual(from_idx, 14)  # knee_r
        self.assertEqual(to_idx, 16)    # ankle_r

    def test_get_skeleton_connection_out_of_range(self):
        desc = DLStreamerMeta.KeypointDescriptor.lookup('body-pose/coco-17')
        ok, from_idx, to_idx = desc.get_skeleton_connection(100)
        self.assertFalse(ok)

    def test_get_skeleton_connection_no_skeleton(self):
        desc = DLStreamerMeta.KeypointDescriptor.lookup(
            'face-landmarks/centerface-5')
        ok, from_idx, to_idx = desc.get_skeleton_connection(0)
        self.assertFalse(ok)

    def test_all_coco17_point_names(self):
        desc = DLStreamerMeta.KeypointDescriptor.lookup('body-pose/coco-17')
        expected = [
            'nose', 'eye_l', 'eye_r', 'ear_l', 'ear_r',
            'shoulder_l', 'shoulder_r', 'elbow_l', 'elbow_r',
            'wrist_l', 'wrist_r', 'hip_l', 'hip_r',
            'knee_l', 'knee_r', 'ankle_l', 'ankle_r']
        names = [desc.get_point_name(i) for i in range(desc.point_count)]
        self.assertEqual(names, expected)

    def test_hrnet_coco17_shares_point_names_with_coco17(self):
        coco = DLStreamerMeta.KeypointDescriptor.lookup('body-pose/coco-17')
        hrnet = DLStreamerMeta.KeypointDescriptor.lookup(
            'body-pose/hrnet-coco-17')
        self.assertEqual(coco.point_count, hrnet.point_count)
        for i in range(coco.point_count):
            self.assertEqual(coco.get_point_name(i),
                             hrnet.get_point_name(i))

    def test_hrnet_coco17_different_skeleton_from_coco17(self):
        coco = DLStreamerMeta.KeypointDescriptor.lookup('body-pose/coco-17')
        hrnet = DLStreamerMeta.KeypointDescriptor.lookup(
            'body-pose/hrnet-coco-17')
        # Both have 17 points but different skeleton connection counts
        self.assertNotEqual(
            coco.skeleton_connection_count,
            hrnet.skeleton_connection_count)


if __name__ == '__main__':
    unittest.main()
