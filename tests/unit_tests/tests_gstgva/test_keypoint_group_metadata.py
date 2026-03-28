# ==============================================================================
# Copyright (C) 2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================

import sys
import unittest

import gi
gi.require_version('Gst', '1.0')
gi.require_version('GstAnalytics', '1.0')
gi.require_version('DLStreamerMeta', '1.0')

from gi.repository import Gst, GstAnalytics, DLStreamerMeta

# Import gstgva.region_of_interest to trigger _wrap_mtd() registration
# which makes type() return proper DLStreamerMeta types during iteration
from gstgva.region_of_interest import RegionOfInterest  # noqa: F401

Gst.init(sys.argv)


class KeypointMtdTestCase(unittest.TestCase):
    """Tests for DLStreamerMeta.KeypointMtd Python bindings."""

    def setUp(self):
        self.buffer = Gst.Buffer.new_allocate(None, 0, None)
        self.rmeta = GstAnalytics.buffer_add_analytics_relation_meta(
            self.buffer)

    def test_add_and_get_position_2d(self):
        ok, kp = DLStreamerMeta.relation_meta_add_keypoint_mtd(
            self.rmeta, DLStreamerMeta.KeypointDimensions(2),
            100, 200, 0, int(DLStreamerMeta.KeypointVisibility.VISIBLE), 0.95)
        self.assertTrue(ok)

        ok, x, y, z, dim = kp.get_position()
        self.assertTrue(ok)
        self.assertEqual(x, 100)
        self.assertEqual(y, 200)
        self.assertEqual(z, 0)
        self.assertEqual(dim, DLStreamerMeta.KeypointDimensions(2))

    def test_add_and_get_position_3d(self):
        ok, kp = DLStreamerMeta.relation_meta_add_keypoint_mtd(
            self.rmeta, DLStreamerMeta.KeypointDimensions(3),
            10, 20, 30, 0, 0.5)
        self.assertTrue(ok)

        ok, x, y, z, dim = kp.get_position()
        self.assertTrue(ok)
        self.assertEqual(x, 10)
        self.assertEqual(y, 20)
        self.assertEqual(z, 30)
        self.assertEqual(dim, DLStreamerMeta.KeypointDimensions(3))

    def test_get_confidence(self):
        ok, kp = DLStreamerMeta.relation_meta_add_keypoint_mtd(
            self.rmeta, DLStreamerMeta.KeypointDimensions(2),
            0, 0, 0, 0, 0.87)
        self.assertTrue(ok)

        ok, conf = kp.get_confidence()
        self.assertTrue(ok)
        self.assertAlmostEqual(conf, 0.87, places=2)

    def test_get_visibility_flags(self):
        vis_flags = (int(DLStreamerMeta.KeypointVisibility.VISIBLE) |
                     int(DLStreamerMeta.KeypointVisibility.PROJECTED))
        ok, kp = DLStreamerMeta.relation_meta_add_keypoint_mtd(
            self.rmeta, DLStreamerMeta.KeypointDimensions(2),
            0, 0, 0, vis_flags, 0.5)
        self.assertTrue(ok)

        ok, vis = kp.get_visibility_flags()
        self.assertTrue(ok)
        self.assertEqual(vis, vis_flags)

    def test_get_mtd_type(self):
        kp_type = DLStreamerMeta.keypoint_mtd_get_mtd_type()
        self.assertEqual(DLStreamerMeta.KeypointMtd.get_mtd_type(), kp_type)

    def test_relation_meta_get_keypoint_mtd(self):
        ok, kp = DLStreamerMeta.relation_meta_add_keypoint_mtd(
            self.rmeta, DLStreamerMeta.KeypointDimensions(2),
            42, 99, 0, 0, 0.75)
        self.assertTrue(ok)

        ok, kp2 = DLStreamerMeta.relation_meta_get_keypoint_mtd(
            self.rmeta, kp.id)
        self.assertTrue(ok)

        ok, x, y, z, dim = kp2.get_position()
        self.assertTrue(ok)
        self.assertEqual(x, 42)
        self.assertEqual(y, 99)


class GroupMtdTestCase(unittest.TestCase):
    """Tests for DLStreamerMeta.GroupMtd Python bindings."""

    def setUp(self):
        self.buffer = Gst.Buffer.new_allocate(None, 0, None)
        self.rmeta = GstAnalytics.buffer_add_analytics_relation_meta(
            self.buffer)

    def test_add_group_mtd(self):
        ok, grp = DLStreamerMeta.relation_meta_add_group_mtd(self.rmeta, 5)
        self.assertTrue(ok)

    def test_add_group_mtd_with_size(self):
        ok, grp = DLStreamerMeta.relation_meta_add_group_mtd_with_size(
            self.rmeta, 10)
        self.assertTrue(ok)
        self.assertEqual(grp.get_member_count(), 0)

    def test_set_get_semantic_tag(self):
        ok, grp = DLStreamerMeta.relation_meta_add_group_mtd(self.rmeta, 5)
        self.assertTrue(ok)

        grp.set_semantic_tag('pose-17-kp')
        self.assertEqual(grp.get_semantic_tag(), 'pose-17-kp')

    def test_set_semantic_tag_none_clears(self):
        ok, grp = DLStreamerMeta.relation_meta_add_group_mtd(self.rmeta, 5)
        grp.set_semantic_tag('test')
        grp.set_semantic_tag(None)
        tag = grp.get_semantic_tag()
        self.assertFalse(grp.has_semantic_tag('test'))

    def test_has_semantic_tag(self):
        ok, grp = DLStreamerMeta.relation_meta_add_group_mtd(self.rmeta, 5)
        grp.set_semantic_tag('hand-21-kp')

        self.assertTrue(grp.has_semantic_tag('hand-21-kp'))
        self.assertFalse(grp.has_semantic_tag('pose-17-kp'))

    def test_semantic_tag_has_prefix(self):
        ok, grp = DLStreamerMeta.relation_meta_add_group_mtd(self.rmeta, 5)
        grp.set_semantic_tag('hand-21-kp')

        self.assertTrue(grp.semantic_tag_has_prefix('hand'))
        self.assertFalse(grp.semantic_tag_has_prefix('pose'))

    def test_add_member_and_get_member_count(self):
        ok, grp = DLStreamerMeta.relation_meta_add_group_mtd(self.rmeta, 5)

        ok, kp1 = DLStreamerMeta.relation_meta_add_keypoint_mtd(
            self.rmeta, DLStreamerMeta.KeypointDimensions(2),
            10, 20, 0, 0, 0.9)
        ok, kp2 = DLStreamerMeta.relation_meta_add_keypoint_mtd(
            self.rmeta, DLStreamerMeta.KeypointDimensions(2),
            30, 40, 0, 0, 0.8)

        grp.add_member(kp1.id)
        grp.add_member(kp2.id)
        self.assertEqual(grp.get_member_count(), 2)

    def test_get_member(self):
        ok, grp = DLStreamerMeta.relation_meta_add_group_mtd(self.rmeta, 5)
        ok, kp = DLStreamerMeta.relation_meta_add_keypoint_mtd(
            self.rmeta, DLStreamerMeta.KeypointDimensions(2),
            10, 20, 0, 0, 0.9)

        grp.add_member(kp.id)

        ok, member = grp.get_member(0)
        self.assertTrue(ok)
        self.assertEqual(member.id, kp.id)

    def test_iterate(self):
        ok, grp = DLStreamerMeta.relation_meta_add_group_mtd(self.rmeta, 5)

        kp_ids = []
        for i in range(3):
            ok, kp = DLStreamerMeta.relation_meta_add_keypoint_mtd(
                self.rmeta, DLStreamerMeta.KeypointDimensions(2),
                i * 10, i * 20, 0, 0, 0.5)
            grp.add_member(kp.id)
            kp_ids.append(kp.id)

        kp_type = DLStreamerMeta.keypoint_mtd_get_mtd_type()
        iterated_ids = []
        state = None
        while True:
            ok, state, member = grp.iterate(state, kp_type)
            if not ok:
                break
            iterated_ids.append(member.id)

        self.assertEqual(iterated_ids, kp_ids)

    def test_get_mtd_type(self):
        grp_type = DLStreamerMeta.group_mtd_get_mtd_type()
        self.assertEqual(DLStreamerMeta.GroupMtd.get_mtd_type(), grp_type)

    def test_relation_meta_get_group_mtd(self):
        ok, grp = DLStreamerMeta.relation_meta_add_group_mtd(self.rmeta, 5)
        grp.set_semantic_tag('my-group')

        ok, grp2 = DLStreamerMeta.relation_meta_get_group_mtd(
            self.rmeta, grp.id)
        self.assertTrue(ok)
        self.assertEqual(grp2.get_semantic_tag(), 'my-group')


class KeypointsGroupTestCase(unittest.TestCase):
    """Tests for the high-level add_keypoints_group convenience function."""

    def setUp(self):
        self.buffer = Gst.Buffer.new_allocate(None, 0, None)
        self.rmeta = GstAnalytics.buffer_add_analytics_relation_meta(
            self.buffer)

    def test_add_keypoints_group(self):
        positions = [10, 20, 30, 40, 50, 60]
        confidences = [0.9, 0.8, 0.7]
        skeleton = [0, 1, 1, 2]

        ok, grp = DLStreamerMeta.relation_meta_add_keypoints_group(
            self.rmeta, 'hand-3-kp',
            DLStreamerMeta.KeypointDimensions(2),
            positions, confidences, None, skeleton)
        self.assertTrue(ok)
        self.assertEqual(grp.get_semantic_tag(), 'hand-3-kp')
        self.assertEqual(grp.get_member_count(), 3)

    def test_keypoints_group_member_positions(self):
        positions = [100, 200, 300, 400]
        confidences = [0.95, 0.85]

        ok, grp = DLStreamerMeta.relation_meta_add_keypoints_group(
            self.rmeta, 'test-kp',
            DLStreamerMeta.KeypointDimensions(2),
            positions, confidences, None, None)
        self.assertTrue(ok)

        for i, (exp_x, exp_y) in enumerate([(100, 200), (300, 400)]):
            ok, member = grp.get_member(i)
            self.assertTrue(ok)
            ok, kp = DLStreamerMeta.relation_meta_get_keypoint_mtd(
                self.rmeta, member.id)
            self.assertTrue(ok)
            ok, x, y, z, dim = kp.get_position()
            self.assertTrue(ok)
            self.assertEqual(x, exp_x)
            self.assertEqual(y, exp_y)

    def test_keypoints_group_no_skeleton(self):
        positions = [1, 2, 3, 4]
        confidences = [0.5, 0.6]

        ok, grp = DLStreamerMeta.relation_meta_add_keypoints_group(
            self.rmeta, 'no-skel',
            DLStreamerMeta.KeypointDimensions(2),
            positions, confidences, None, None)
        self.assertTrue(ok)
        self.assertEqual(grp.get_member_count(), 2)

    def test_keypoints_group_with_visibilities(self):
        positions = [10, 20, 30, 40]
        confidences = [0.9, 0.8]
        visibilities = [int(DLStreamerMeta.KeypointVisibility.VISIBLE),
                        int(DLStreamerMeta.KeypointVisibility.OCCLUDED)]

        ok, grp = DLStreamerMeta.relation_meta_add_keypoints_group(
            self.rmeta, 'vis-kp',
            DLStreamerMeta.KeypointDimensions(2),
            positions, confidences, visibilities, None)
        self.assertTrue(ok)

        ok, member = grp.get_member(1)
        ok, kp = DLStreamerMeta.relation_meta_get_keypoint_mtd(
            self.rmeta, member.id)
        ok, vis = kp.get_visibility_flags()
        self.assertTrue(ok)
        self.assertEqual(vis, int(DLStreamerMeta.KeypointVisibility.OCCLUDED))


class WrapMtdTypeTestCase(unittest.TestCase):
    """Tests that _wrap_mtd registration makes type() return correct types
    when iterating over RelationMeta."""

    def setUp(self):
        self.buffer = Gst.Buffer.new_allocate(None, 0, None)
        self.rmeta = GstAnalytics.buffer_add_analytics_relation_meta(
            self.buffer)

    def test_keypoint_type_during_iteration(self):
        ok, kp = DLStreamerMeta.relation_meta_add_keypoint_mtd(
            self.rmeta, DLStreamerMeta.KeypointDimensions(2),
            10, 20, 0, 0, 0.9)
        self.assertTrue(ok)

        found = False
        for mtd in self.rmeta:
            if mtd.id == kp.id:
                self.assertIsInstance(mtd, DLStreamerMeta.KeypointMtd)
                found = True
                break
        self.assertTrue(found, "KeypointMtd not found during iteration")

    def test_group_type_during_iteration(self):
        ok, grp = DLStreamerMeta.relation_meta_add_group_mtd(self.rmeta, 5)
        grp.set_semantic_tag('test')
        self.assertTrue(ok)

        found = False
        for mtd in self.rmeta:
            if mtd.id == grp.id:
                self.assertIsInstance(mtd, DLStreamerMeta.GroupMtd)
                found = True
                break
        self.assertTrue(found, "GroupMtd not found during iteration")

    def test_mixed_types_during_iteration(self):
        ok, kp = DLStreamerMeta.relation_meta_add_keypoint_mtd(
            self.rmeta, DLStreamerMeta.KeypointDimensions(2),
            1, 2, 0, 0, 0.5)
        ok, grp = DLStreamerMeta.relation_meta_add_group_mtd(self.rmeta, 5)

        types_found = set()
        for mtd in self.rmeta:
            types_found.add(type(mtd))

        self.assertIn(DLStreamerMeta.KeypointMtd, types_found)
        self.assertIn(DLStreamerMeta.GroupMtd, types_found)

    def test_keypoints_group_iteration_types(self):
        positions = [10, 20, 30, 40]
        confidences = [0.9, 0.8]
        ok, grp = DLStreamerMeta.relation_meta_add_keypoints_group(
            self.rmeta, 'pose-2-kp',
            DLStreamerMeta.KeypointDimensions(2),
            positions, confidences, None, [0, 1])
        self.assertTrue(ok)

        types_found = set()
        for mtd in self.rmeta:
            types_found.add(type(mtd))

        self.assertIn(DLStreamerMeta.KeypointMtd, types_found)
        self.assertIn(DLStreamerMeta.GroupMtd, types_found)


if __name__ == '__main__':
    unittest.main()
