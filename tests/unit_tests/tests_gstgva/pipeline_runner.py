# ==============================================================================
# Copyright (C) 2020-2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================

from utils import BBox
from os import listdir, environ
from os.path import isfile, isdir, join

import unittest

import gi
import gstgva as va  # noqa
gi.require_version('Gst', '1.0')
gi.require_version("GLib", "2.0")
gi.require_version('GstApp', '1.0')
gi.require_version("GstVideo", "1.0")
# pylint: disable=no-name-in-module
from gi.repository import GLib, Gst, GstApp, GstVideo  # noqa
# pylint: enable=no-name-in-module

Gst.init(None)


class TestGenericPipelineRunner(unittest.TestCase):
    def set_pipeline(self, pipeline):
        self.exceptions = []

        self._mainloop = GLib.MainLoop()
        self._pipeline_str = pipeline
        print(self._pipeline_str)
        self._pipeline = Gst.parse_launch(self._pipeline_str)

        self._bus = self._pipeline.get_bus()
        self._bus.add_signal_watch()
        self._bus.connect('message', self.on_message)

    def run_pipeline(self):
        self._state = self._pipeline.set_state(Gst.State.PLAYING)
        print(self._state)
        self._mainloop.run()

    def kill(self):
        self._pipeline.set_state(Gst.State.PAUSED)
        self._state = self._pipeline.get_state(Gst.CLOCK_TIME_NONE)[1]
        print(self._state)
        self._pipeline.set_state(Gst.State.READY)
        self._state = self._pipeline.get_state(Gst.CLOCK_TIME_NONE)[1]
        print(self._state)
        self._pipeline.set_state(Gst.State.NULL)
        self._state = self._pipeline.get_state(Gst.CLOCK_TIME_NONE)[1]
        print(self._state)

        self._bus = None
        self._pipeline = None
        self._mainloop.quit()
        self._mainloop = None

    def on_message(self, bus, msg):
        t = msg.type
        if t is Gst.MessageType.EOS:
            self.kill()
        elif t is Gst.MessageType.ERROR:
            self.kill()
            self.exceptions.append(msg.parse_error())


class TestPipelineRunner(TestGenericPipelineRunner):
    def set_pipeline(self, pipeline, image_path, ground_truth,
                     check_only_bbox_number=False, check_additional_info=True, check_frame_data=True,
                     ground_truth_per_frame=False, image_repeat_num=7, check_first_skip=0, check_format=True):
        self.exceptions = []
        self._ground_truth = ground_truth
        self._check_only_bbox_number = check_only_bbox_number
        self._check_frame_data = check_frame_data
        self._ground_truth_per_frame = ground_truth_per_frame
        self._check_format = check_format
        self._check_first_skip = check_first_skip
        self._check_additional_info = check_additional_info

        self._mainloop = GLib.MainLoop()
        self._pipeline_str = pipeline
        print(self._pipeline_str)
        self._pipeline = Gst.parse_launch(self._pipeline_str)

        self._bus = self._pipeline.get_bus()
        self._bus.add_signal_watch()
        self._bus.connect('message', self.on_message)

        self._mysink = self._pipeline.get_by_name("mysink")
        self._mysink.connect('new-sample', self.on_new_buffer)

        self._mysrc = self._pipeline.get_by_name("mysrc")
        self._mysrc.connect("need-data", self.need_data)

        self._image_paths_to_src = []
        if isdir(image_path):
            for i, file_name in enumerate(listdir(image_path)):
                self._image_paths_to_src.append(
                    join(image_path, file_name))
        elif isfile(image_path):
            self._image_paths_to_src.append(image_path)
        self._image_paths_to_src *= image_repeat_num
        self._image_paths_to_src = sorted(self._image_paths_to_src)
        self._expected_frames_num = len(self._image_paths_to_src)
        self._current_frame = 0

    def need_data(self, app_src, size):
        if not self._image_paths_to_src:
            # EOS will be emitted when appsink processes all frames
            return Gst.PadProbeReturn.OK

        image_path = self._image_paths_to_src.pop(0)

        with open(image_path, "rb") as f:
            image_data = bytearray(f.read())
        # Check for GST 1.20 API
        if hasattr(Gst.Buffer, 'new_memdup'):
            buff = Gst.Buffer.new_memdup(image_data)
        else:
            buff = Gst.Buffer.new_allocate(None, len(image_data), None)
            buff.fill(0, image_data)
        app_src.emit("push-buffer", buff)
        return Gst.PadProbeReturn.OK

    def on_new_buffer(self, appsink):
        appsink_sample = GstApp.AppSink.pull_sample(self._mysink)
        self._current_frame += 1
        buff = appsink_sample.get_buffer()
        caps = appsink_sample.get_caps()
        frame = va.VideoFrame(buff, caps=caps)

        if self._current_frame <= self._check_first_skip:
            return Gst.FlowReturn.OK

        if self._check_format:
            caps_str = caps.get_structure(0)
            format_str = caps_str.get_string("format")
            supported_formats = ["BGR", "BGRx", "BGRA", "I420", "NV12"]
            try:
                self.assertTrue(format_str in supported_formats)
            except AssertionError as e:
                self.exceptions.append(e)

        if self._check_frame_data:
            try:
                with frame.data(flag=Gst.MapFlags.READ):
                    pass
            except Exception as e:
                self.exceptions.append(e)

        regions = list()
        try:
            for region in frame.regions():
                detection_tensor = region.detection()
                bbox = BBox(detection_tensor['x_min'],
                            detection_tensor['y_min'],
                            detection_tensor['x_max'],
                            detection_tensor['y_max'],
                            list(), tracker_id=region.object_id(), class_id=region.label_id())
                for tensor in region.get_gst_roi_params():
                    if tensor.is_detection():
                        continue
                    else:
                        bbox.additional_info.append({
                            'label': tensor.label(),
                            'layer_name': tensor.layer_name(),
                            'data': tensor.data(),
                            'name': tensor.name(),
                            'format': tensor.format(),
                            'keypoints_data': tensor['keypoints_data']
                        })
                regions.append(bbox)
            for tensor in frame.tensors():
                # TODO: add 'is_classification' check for the Tensor using the 'type' field of this tensor
                bbox = BBox(0, 0, 1, 1, list())
                bbox.additional_info.append({
                    'label': tensor.label(),
                    'layer_name': tensor.layer_name(),
                    'data': tensor.data(),
                    'name': tensor.name(),
                    'format': tensor.format()
                })
                regions.append(bbox)
        except Exception as e:
            self.exceptions.append(e)

        try:
            gt = self._ground_truth[:] if not self._ground_truth_per_frame else self._ground_truth[self._current_frame - 1]
            self.assertTrue(BBox.bboxes_is_equal(
                regions[:], gt,
                self._check_only_bbox_number, self._check_additional_info))
        except Exception as e:
            self.exceptions.append(e)

        # Wait till all frames are processed on appsink
        if self._expected_frames_num == self._current_frame:
            self._mysrc.emit("end-of-stream")

        return Gst.FlowReturn.OK
