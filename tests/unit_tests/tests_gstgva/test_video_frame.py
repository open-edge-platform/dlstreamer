# ==============================================================================
# Copyright (C) 2018-2025 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================

import sys
import unittest
import numpy as np

import gi
import gstgva as va
gi.require_version('Gst', '1.0')
gi.require_version("GstVideo", "1.0")
gi.require_version("GLib", "2.0")
# pylint: disable=no-name-in-module, wrong-import-position
from gi.repository import Gst, GstVideo, GLib
# pylint: enable=no-name-in-module, wrong-import-position


Gst.init(sys.argv)

from tests_gstgva import register_metadata


class VideoFrameTestCase(unittest.TestCase):
    def setUp(self):
        register_metadata()

        self.buffer = Gst.Buffer.new_allocate(None, 0, None)
        self.video_info_nv12 = GstVideo.VideoInfo.new()
        self.video_info_nv12.set_format(
            GstVideo.VideoFormat.NV12, 1920, 1080)  # FullHD

        self.video_frame_nv12 = va.VideoFrame(self.buffer, self.video_info_nv12)

        self.video_info_i420 = GstVideo.VideoInfo.new()
        self.video_info_i420.set_format(
            GstVideo.VideoFormat.I420, 1920, 1080)  # FullHD

        self.video_frame_i420 = va.VideoFrame(self.buffer, self.video_info_i420)

        self.video_info_bgrx = GstVideo.VideoInfo.new()
        self.video_info_bgrx.set_format(
            GstVideo.VideoFormat.BGRX, 1920, 1080)  # FullHD

        self.video_frame_bgrx = va.VideoFrame(self.buffer, self.video_info_bgrx)
        

    def tearDown(self):
        pass

    def test_regions(self):
        self.assertEqual(len(list(self.video_frame_nv12.regions())), 0)

        rois_num = 100
        for i in range(rois_num):
            self.video_frame_nv12.add_region(i, i, i + 100, i + 100, "label", i / 100.0)
        regions = [region for region in self.video_frame_nv12.regions()]
        self.assertEqual(len(regions), rois_num)

        counter = 0
        for i in range(rois_num):
            region = next(
                (
                    region
                    for region in self.video_frame_nv12.regions()
                    if (i, i, i + 100, i + 100, "label", np.float32(i / 100.0))
                    == (
                        region.rect().x,
                        region.rect().y,
                        region.rect().w,
                        region.rect().h,
                        region.label(),
                        region.confidence(),
                    )
                ),
                None,
            )
            if region:
                counter += 1
        self.assertEqual(counter, rois_num)

        self.video_frame_nv12.add_region(
            0.0, 0.0, 0.3, 0.6, "label", 0.8, normalized=True
        )
        self.assertEqual(len(list(self.video_frame_nv12.regions())), rois_num + 1)
        self.assertEqual(len(regions), rois_num)

    def test_tensors(self):
        self.assertEqual(len(list(self.video_frame_nv12.tensors())), 0)

        tensor_meta_size = 10
        field_name = "model_name"
        model_name = "test_model"
        for i in range(tensor_meta_size):
            tensor = self.video_frame_nv12.add_tensor()
            test_model = model_name + str(i)
            tensor["model_name"] = test_model

        tensors = [tensor for tensor in self.video_frame_nv12.tensors()]
        self.assertEqual(len(tensors), tensor_meta_size)

        for ind in range(tensor_meta_size):
            test_model = model_name + str(ind)
            tensor_ind = next(i for i, tensor in enumerate(tensors)
                              if tensor.model_name() == test_model)
            del tensors[tensor_ind]

        self.assertEqual(len(tensors), 0)

    def test_messages(self):
        self.assertEqual(len(self.video_frame_nv12.messages()), 0)

        messages_num = 10
        test_message = "test_messages"
        for i in range(messages_num):
            self.video_frame_nv12.add_message(test_message + str(i))
        messages = self.video_frame_nv12.messages()
        self.assertEqual(len(messages), messages_num)

        for ind in range(messages_num):
            message_ind = next(i for i, message in enumerate(messages)
                               if message == test_message + str(ind))
            messages.pop(message_ind)
            pass
        self.assertEqual(len(messages), 0)

        messages = self.video_frame_nv12.messages()
        self.assertEqual(len(messages), messages_num)

        for i in range(len(messages)):
            to_remove_message = test_message + str(i)
            if (to_remove_message) in messages:
                messages.remove(to_remove_message)
        self.assertEqual(len(messages), 0)

    def test_accuracy_test_cases(self):
        empty_frame = va.VideoFrame(self.buffer)
        empty_frame.add_message("some_message")

        full_frame = va.VideoFrame(self.buffer, self.video_info_nv12)
        messages = full_frame.messages()
        self.assertEqual(len(messages), 1)
        for test_messageage in messages:
            self.assertEqual(test_messageage, "some_message")

    def test_data(self):
        info_list = [self.video_info_nv12, self.video_info_i420, self.video_info_bgrx]
        for info in info_list:
            frame_from_buf_caps = va.VideoFrame(self.buffer, info)
            self.assertNotEqual(frame_from_buf_caps.data(), None)

            caps = info.to_caps()
            frame_from_buf_caps = va.VideoFrame(self.buffer, caps=caps)
            self.assertNotEqual(frame_from_buf_caps.data(), None)

            frame_from_buf = va.VideoFrame(self.buffer)
            self.assertRaises(Exception, frame_from_buf.data())


if __name__ == '__main__':
    unittest.main(verbosity=3)
