# ==============================================================================
# Copyright (C) 2018-2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================
# pylint: disable=invalid-name,wrong-import-position,duplicate-code

"""
This module implements a custom GStreamer Transform element to save video frames
containing detected objects. It reads GstAnalytics detection metadata and saves
annotated frames to disk with bounding boxes and confidence scores.

Replaces the gvapython-based frame saver with a proper GStreamer Python element.
"""

import os
import time
import cv2
import numpy as np

import gi

gi.require_version("Gst", "1.0")
gi.require_version("GstBase", "1.0")
gi.require_version("GstVideo", "1.0")
gi.require_version("GstAnalytics", "1.0")
from gi.repository import (  # pylint: disable=no-name-in-module
    Gst,
    GstBase,
    GObject,
    GLib,
    GstAnalytics,
    GstVideo,
)

Gst.init_python()


class FrameSaver(GstBase.BaseTransform):
    """DLStreamer custom element to save frames with detected objects (ROIs) to disk."""

    __gstmetadata__ = (
        "GVA Frame Saver Python",
        "Transform",
        "Save frames with detected ROI to disk",
        "Intel DLStreamer",
    )

    # Restrict caps to the video formats _convert_to_bgr() actually supports.
    # Using Caps.new_any() would silently accept e.g. RGB/RGBA and produce
    # color-swapped output (the code interprets all 3/4-channel data as BGR).
    _SUPPORTED_FORMATS = "NV12, I420, BGR, BGRA, BGRX"
    _CAPS = Gst.Caps.from_string(f"video/x-raw, format={{ {_SUPPORTED_FORMATS} }}")

    __gsttemplates__ = (
        Gst.PadTemplate.new("src", Gst.PadDirection.SRC, Gst.PadPresence.ALWAYS, _CAPS),
        Gst.PadTemplate.new(
            "sink", Gst.PadDirection.SINK, Gst.PadPresence.ALWAYS, _CAPS
        ),
    )

    # Element properties: default values and setters/getters
    _output_dir = "saved_frames"
    _save_interval = 2.0
    _min_confidence = 0.5

    @GObject.Property(type=str)
    def output_dir(self):
        "Directory to save frames with detections."
        return self._output_dir

    @output_dir.setter
    def output_dir(self, value):
        self._output_dir = value

    @GObject.Property(type=float)
    def save_interval(self):
        "Minimum seconds between frame saves."
        return self._save_interval

    @save_interval.setter
    def save_interval(self, value):
        self._save_interval = value

    @GObject.Property(type=float)
    def min_confidence(self):
        "Minimum detection confidence to trigger frame save."
        return self._min_confidence

    @min_confidence.setter
    def min_confidence(self, value):
        self._min_confidence = value

    def __init__(self):
        super().__init__()
        self._last_save_time = 0
        self._save_count = 0
        self._video_info = None

    def do_set_caps(
        self, incaps, outcaps
    ):  # pylint: disable=arguments-differ,unused-argument
        """Store video info from negotiated caps for frame data access."""
        if hasattr(GstVideo.VideoInfo, "new_from_caps"):
            self._video_info = GstVideo.VideoInfo.new_from_caps(incaps)
        else:
            self._video_info = GstVideo.VideoInfo()
            self._video_info.from_caps(incaps)
        return True

    def _convert_to_bgr(self, data, width, height, format_name):
        """Convert raw frame data to BGR format for OpenCV."""
        if format_name == "NV12":
            yuv = data[: height * 3 // 2 * width].reshape(height * 3 // 2, width)
            return cv2.cvtColor(yuv, cv2.COLOR_YUV2BGR_NV12)
        if format_name == "I420":
            yuv = data[: height * 3 // 2 * width].reshape(height * 3 // 2, width)
            return cv2.cvtColor(yuv, cv2.COLOR_YUV2BGR_I420)
        channels = 4 if format_name in ["BGRA", "BGRX"] else 3
        img = data[: height * width * channels].reshape(height, width, channels)
        if channels == 4:
            return cv2.cvtColor(img, cv2.COLOR_BGRA2BGR)
        return np.copy(img)

    def _collect_detections(self, rmeta):
        """Collect detections above confidence threshold from analytics metadata."""
        detections = []
        for mtd in rmeta:
            if isinstance(mtd, GstAnalytics.ODMtd):
                _, x, y, w, h, confidence = mtd.get_location()
                if confidence > self._min_confidence:
                    obj_type = mtd.get_obj_type()
                    label = GLib.quark_to_string(obj_type) if obj_type else ""
                    detections.append((x, y, w, h, confidence, label))
        return detections

    @staticmethod
    def _draw_detections(img, detections):
        """Draw bounding boxes and labels on image."""
        for x, y, w, h, confidence, label in detections:
            pt1 = (int(x), int(y))
            pt2 = (int(x + w), int(y + h))
            cv2.rectangle(img, pt1, pt2, (0, 255, 0), 2)
            cv2.putText(
                img,
                f"{label} {confidence:.2f}",
                (pt1[0], pt1[1] - 5),
                cv2.FONT_HERSHEY_SIMPLEX,
                0.5,
                (0, 255, 0),
                2,
            )

    def _save_frame(self, buffer, detections):
        """Map buffer, draw detections and save frame to disk."""
        os.makedirs(self._output_dir, exist_ok=True)
        if self._video_info is None:
            return

        success, map_info = buffer.map(Gst.MapFlags.READ)
        if not success:
            return

        width = self._video_info.width
        height = self._video_info.height
        format_name = self._video_info.finfo.name

        data = np.frombuffer(map_info.data, dtype=np.uint8)
        img = self._convert_to_bgr(data, width, height, format_name)
        buffer.unmap(map_info)

        self._draw_detections(img, detections)

        filename = os.path.join(self._output_dir, f"frame_{self._save_count:05d}.jpg")
        cv2.imwrite(filename, img)
        self._save_count += 1
        self._last_save_time = time.time()
        print(f"Saved: {filename} (format: {format_name})")

    def do_transform_ip(self, buffer):  # pylint: disable=arguments-differ
        """Process each frame: save to disk if detections above threshold are present."""
        if (time.time() - self._last_save_time) < self._save_interval:
            return Gst.FlowReturn.OK

        rmeta = GstAnalytics.buffer_get_analytics_relation_meta(buffer)
        if not rmeta:
            return Gst.FlowReturn.OK

        detections = self._collect_detections(rmeta)
        if not detections:
            return Gst.FlowReturn.OK

        try:
            self._save_frame(buffer, detections)
        except (cv2.error, OSError, ValueError, IndexError) as e:
            print(f"Error saving frame: {e}")

        return Gst.FlowReturn.OK


GObject.type_register(FrameSaver)
__gstelementfactory__ = ("gvaframesaver_py", Gst.Rank.NONE, FrameSaver)
