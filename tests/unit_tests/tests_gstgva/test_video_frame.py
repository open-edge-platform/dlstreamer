# ==============================================================================
# Copyright (C) 2018-2026 Intel Corporation
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
gi.require_version("GstAnalytics", "1.0")

# pylint: disable=no-name-in-module, wrong-import-position
from gi.repository import Gst, GstVideo, GLib, GstAnalytics
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

    def test_analytics_only_fallback(self):
        """Test that regions() creates GstVideoRegionOfInterestMeta from GstAnalytics-only metadata
        and converts related classification metadata to ROI params.
        Same test data as C++ VideoFrameTestAnalyticsOnlyFallback."""
        # Test constants — same values as C++ test
        OD_X, OD_Y, OD_W, OD_H = 100, 50, 200, 400
        OD_CONF = 0.85
        OD_LABEL = "person"
        CLS_LABEL = "neutral"
        CLS_CONF = 0.75

        buffer = Gst.Buffer.new_allocate(None, 0, None)
        video_info = GstVideo.VideoInfo.new()
        video_info.set_format(GstVideo.VideoFormat.NV12, 1920, 1080)
        frame = va.VideoFrame(buffer, video_info)

        # Add only GstAnalytics metadata (no GstVideoRegionOfInterestMeta)
        relation_meta = GstAnalytics.buffer_add_analytics_relation_meta(buffer)
        self.assertIsNotNone(relation_meta)

        # Add object detection metadata
        label_quark = GLib.quark_from_string(OD_LABEL)
        success, od_mtd = relation_meta.add_od_mtd(label_quark, OD_X, OD_Y, OD_W, OD_H, OD_CONF)
        self.assertTrue(success)

        # Add classification metadata with CONTAIN relation to OD
        cls_quark = GLib.quark_from_string(CLS_LABEL)
        success, cls_mtd = relation_meta.add_cls_mtd([CLS_CONF], [cls_quark])
        self.assertTrue(success)
        self.assertTrue(relation_meta.set_relation(
            GstAnalytics.RelTypes.CONTAIN, od_mtd.id, cls_mtd.id))

        # Call regions() — should trigger fallback
        regions = list(frame.regions())
        self.assertEqual(len(regions), 1)

        roi = regions[0]

        # Verify GstVideoRegionOfInterestMeta directly
        roi_meta = roi.meta()
        self.assertEqual(roi_meta.x, OD_X)
        self.assertEqual(roi_meta.y, OD_Y)
        self.assertEqual(roi_meta.w, OD_W)
        self.assertEqual(roi_meta.h, OD_H)
        self.assertEqual(roi_meta.id, od_mtd.id)
        self.assertEqual(GLib.quark_to_string(roi_meta.roi_type), OD_LABEL)

        # Verify detection tensor in params
        params = roi.get_gst_roi_params()
        det_params = [p for p in params if p.name() == "detection"]
        self.assertEqual(len(det_params), 1)
        det = det_params[0]
        self.assertAlmostEqual(det["confidence"], OD_CONF, places=5)
        self.assertAlmostEqual(det["x_min"], float(OD_X))
        self.assertAlmostEqual(det["x_max"], float(OD_X + OD_W))
        self.assertAlmostEqual(det["y_min"], float(OD_Y))
        self.assertAlmostEqual(det["y_max"], float(OD_Y + OD_H))

        # Verify classification tensor was converted and added to params
        cls_params = [p for p in params if p.name() == "classification"]
        self.assertEqual(len(cls_params), 1)
        cls = cls_params[0]
        self.assertEqual(cls["label"], CLS_LABEL)
        self.assertAlmostEqual(cls["confidence"], CLS_CONF, places=5)

    def test_analytics_only_fallback_relate_to_relation(self):
        """Test that RELATE_TO relations are also converted in the fallback path.
        Same test data as C++ VideoFrameTestAnalyticsOnlyFallbackRelateToRelation."""
        OD_X, OD_Y, OD_W, OD_H = 100, 50, 200, 400
        OD_CONF = 0.85
        OD_LABEL = "person"
        CLS_LABEL_CONTAIN = "happy"
        CLS_CONF_CONTAIN = 0.9
        CLS_LABEL_RELATE = "male"
        CLS_CONF_RELATE = 0.8

        buffer = Gst.Buffer.new_allocate(None, 0, None)
        video_info = GstVideo.VideoInfo.new()
        video_info.set_format(GstVideo.VideoFormat.NV12, 1920, 1080)
        frame = va.VideoFrame(buffer, video_info)

        relation_meta = GstAnalytics.buffer_add_analytics_relation_meta(buffer)
        self.assertIsNotNone(relation_meta)

        label_quark = GLib.quark_from_string(OD_LABEL)
        success, od_mtd = relation_meta.add_od_mtd(label_quark, OD_X, OD_Y, OD_W, OD_H, OD_CONF)
        self.assertTrue(success)

        # Classification with CONTAIN relation
        q1 = GLib.quark_from_string(CLS_LABEL_CONTAIN)
        success, cls_contain = relation_meta.add_cls_mtd([CLS_CONF_CONTAIN], [q1])
        self.assertTrue(success)
        self.assertTrue(relation_meta.set_relation(
            GstAnalytics.RelTypes.CONTAIN, od_mtd.id, cls_contain.id))

        # Classification with RELATE_TO relation
        q2 = GLib.quark_from_string(CLS_LABEL_RELATE)
        success, cls_relate = relation_meta.add_cls_mtd([CLS_CONF_RELATE], [q2])
        self.assertTrue(success)
        self.assertTrue(relation_meta.set_relation(
            GstAnalytics.RelTypes.RELATE_TO, od_mtd.id, cls_relate.id))

        regions = list(frame.regions())
        self.assertEqual(len(regions), 1)

        # Verify both classifications are in params
        params = regions[0].get_gst_roi_params()
        cls_params = [p for p in params if p.name() == "classification"]
        self.assertEqual(len(cls_params), 2,
                         "Both CONTAIN and RELATE_TO classifications should be added to params")

        labels_found = {p["label"] for p in cls_params}
        self.assertIn(CLS_LABEL_CONTAIN, labels_found,
                      "CONTAIN classification not found in ROI params")
        self.assertIn(CLS_LABEL_RELATE, labels_found,
                      "RELATE_TO classification not found in ROI params")


if __name__ == '__main__':
    unittest.main(verbosity=3)
