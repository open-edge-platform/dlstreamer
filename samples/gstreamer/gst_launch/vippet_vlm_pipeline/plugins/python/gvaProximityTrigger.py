# ==============================================================================
# Copyright (C) 2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================

"""
Custom GStreamer element: Proximity Trigger for VLM inference.

Monitors object detection metadata for proximity between two object classes.
When objects of classA and classB are within 'distance' pixels (center-to-center)
for 'frames' consecutive frames, passes one frame downstream for VLM processing.
All other frames are dropped.
"""

import math

import gi

gi.require_version("Gst", "1.0")
gi.require_version("GstBase", "1.0")
gi.require_version("GstAnalytics", "1.0")
from gi.repository import Gst, GstBase, GObject, GLib, GstAnalytics  # pylint: disable=no-name-in-module

Gst.init_python()

GST_BASE_TRANSFORM_FLOW_DROPPED = Gst.FlowReturn.CUSTOM_SUCCESS


class ProximityTrigger(GstBase.BaseTransform):
    """Drop all frames unless classA and classB objects are within distance for N consecutive frames."""

    __gstmetadata__ = (
        "GVA Proximity Trigger",
        "Transform",
        "Passes one frame when objects of two classes are in proximity for N consecutive frames",
        "Intel DLStreamer",
    )

    __gsttemplates__ = (
        Gst.PadTemplate.new(
            "src", Gst.PadDirection.SRC, Gst.PadPresence.ALWAYS, Gst.Caps.new_any()
        ),
        Gst.PadTemplate.new(
            "sink", Gst.PadDirection.SINK, Gst.PadPresence.ALWAYS, Gst.Caps.new_any()
        ),
    )

    _class_a = "person"
    _class_b = "bicycle"
    _distance = 30
    _frames = 10

    @GObject.Property(type=str, nick="class-a", blurb="First object class to monitor")
    def class_a(self):
        return self._class_a

    @class_a.setter
    def class_a(self, value):
        self._class_a = value

    @GObject.Property(type=str, nick="class-b", blurb="Second object class to monitor")
    def class_b(self):
        return self._class_b

    @class_b.setter
    def class_b(self, value):
        self._class_b = value

    @GObject.Property(type=int, nick="distance",
                      blurb="Maximum center-to-center distance in pixels to trigger",
                      minimum=1, maximum=10000, default=30)
    def distance(self):
        return self._distance

    @distance.setter
    def distance(self, value):
        self._distance = value

    @GObject.Property(type=int, nick="frames",
                      blurb="Number of consecutive frames proximity must hold before trigger",
                      minimum=1, maximum=10000, default=10)
    def frames(self):
        return self._frames

    @frames.setter
    def frames(self, value):
        self._frames = value

    def __init__(self):
        super().__init__()
        self._consecutive_count = 0

    @staticmethod
    def _get_center(x, y, w, h):
        """Return center point of a bounding box."""
        return (x + w / 2.0, y + h / 2.0)

    def _check_proximity(self, rmeta):
        """Check if any classA object is within distance of any classB object."""
        class_a_centers = []
        class_b_centers = []

        for mtd in rmeta:
            if not isinstance(mtd, GstAnalytics.ODMtd):
                continue
            label = GLib.quark_to_string(mtd.get_obj_type())
            if label is None:
                continue
            success, x, y, w, h, _ = mtd.get_location()
            if not success:
                continue
            center = self._get_center(x, y, w, h)

            if label == self._class_a:
                class_a_centers.append(center)
            elif label == self._class_b:
                class_b_centers.append(center)

        for ca in class_a_centers:
            for cb in class_b_centers:
                dist = math.sqrt((ca[0] - cb[0]) ** 2 + (ca[1] - cb[1]) ** 2)
                if dist <= self._distance:
                    return True
        return False

    def do_transform_ip(self, buffer):
        _, state, _ = self.get_state(0)
        if state != Gst.State.PLAYING:
            return GST_BASE_TRANSFORM_FLOW_DROPPED

        rmeta = GstAnalytics.buffer_get_analytics_relation_meta(buffer)
        if not rmeta:
            self._consecutive_count = 0
            return GST_BASE_TRANSFORM_FLOW_DROPPED

        if self._check_proximity(rmeta):
            self._consecutive_count += 1
        else:
            self._consecutive_count = 0
            return GST_BASE_TRANSFORM_FLOW_DROPPED

        if self._consecutive_count >= self._frames:
            self._consecutive_count = 0
            pts_sec = buffer.pts / Gst.SECOND if buffer.pts != Gst.CLOCK_TIME_NONE else 0
            print(f"[proximity] Trigger fired at {pts_sec:.2f}s - "
                  f"{self._class_a} near {self._class_b} for {self._frames} frames")
            return Gst.FlowReturn.OK

        return GST_BASE_TRANSFORM_FLOW_DROPPED


GObject.type_register(ProximityTrigger)
__gstelementfactory__ = ("gvaproximitytrigger_py", Gst.Rank.NONE, ProximityTrigger)
