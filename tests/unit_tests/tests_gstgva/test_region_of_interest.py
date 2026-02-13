# ==============================================================================
# Copyright (C) 2018-2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================

import sys
import unittest
import time

import gi
gi.require_version('GstVideo', '1.0')
gi.require_version('GLib', '2.0')
gi.require_version('Gst', '1.0')

from gstgva.region_of_interest import RegionOfInterest, Tensor
from gstgva.util import VideoRegionOfInterestMeta, libgst
from gstgva.video_frame import VideoFrame
from gi.repository import GstVideo, GLib, GObject, Gst # pylint: disable=no-name-in-module

Gst.init(sys.argv)


class RegionOfInterestTestCase(unittest.TestCase):
    def setUp(self):
        # self is essential to keep buffer alive during whole test case run
        self.buffer = Gst.Buffer.new_allocate(None, 0, None)
        video_info = GstVideo.VideoInfo()
        video_info.set_format(GstVideo.VideoFormat.NV12, 1920, 1080)  # FullHD

        vf = VideoFrame(self.buffer, video_info=video_info)
        self.roi = vf.add_region(
            0.0, 0.0, 0.3, 0.6, "label", 0.77, normalized=True)

    def test_add_tensor(self):
        self.assertAlmostEqual(self.roi.confidence(), 0.77)
        self.assertEqual(len(list(self.roi.tensors())), 1)

        tensors_num = 10
        for i in range(0, tensors_num):
            gst_structure = libgst.gst_structure_new_empty(
                f"tensor_{i}".encode("utf-8")
            )
            tensor = Tensor(gst_structure)
            tensor["confidence"] = i / 10
            self.roi.add_tensor(tensor)

        self.assertEqual(len(list(self.roi.tensors())), tensors_num + 1)
        delta = 0.0000001
        self.assertAlmostEqual(self.roi.confidence(), 0.77, delta=delta)
        self.assertAlmostEqual((list(self.roi.tensors()))[
                               5].confidence(), 0.4, delta=delta)

        confidence = 0.0
        for it_tensor in self.roi.tensors():
            if not it_tensor.is_detection():
                self.assertAlmostEqual(
                    it_tensor.confidence(), confidence, delta=delta)
                confidence += 0.1
            else:
                self.assertAlmostEqual(
                    it_tensor.confidence(), 0.77, delta=delta)

    def test_add_object_id_set_get(self):
        self.assertIsNone(self.roi.object_id())

        object_id_in = 13
        self.roi.set_object_id(object_id_in)
        self.assertEqual(object_id_in, self.roi.object_id())

        object_id_new_in = 17
        self.roi.set_object_id(object_id_new_in)
        self.assertEqual(object_id_new_in, self.roi.object_id())

    def tearDown(self):
        pass


if __name__ == '__main__':
    unittest.main()
