# ==============================================================================
# Copyright (C) 2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================

"""
Custom GStreamer Transform element for person detection analytics.
Inspects detection metadata and signals whether a person is currently visible.
Passes through frames that contain a person detection, drops frames without.
"""

import gi

gi.require_version("GstBase", "1.0")
gi.require_version("GstAnalytics", "1.0")
from gi.repository import Gst, GstBase, GObject, GLib, GstAnalytics
Gst.init_python()

GST_BASE_TRANSFORM_FLOW_DROPPED = Gst.FlowReturn.CUSTOM_SUCCESS


class PersonDetector(GstBase.BaseTransform):
    """Inspects detection metadata for 'person' objects.

    When a person is detected, sets the 'person-visible' property to True
    and passes the frame. When no person is detected, sets 'person-visible'
    to False and drops the frame (signaling downstream recorder to stop).
    """

    __gstmetadata__ = (
        "GVA Person Detector Python",
        "Transform",
        "Detect person presence from analytics metadata",
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

    _person_visible = False
    _cooldown_frames = 15  # keep recording for N frames after last person detection
    _frames_since_person = 0

    @GObject.Property(type=bool, default=False, flags=GObject.ParamFlags.READABLE)
    def person_visible(self):
        """Whether a person is currently visible in the frame."""
        return self._person_visible

    @GObject.Property(type=int, default=15)
    def cooldown(self):
        """Number of frames to keep recording after person leaves view."""
        return self._cooldown_frames

    @cooldown.setter
    def cooldown(self, value):
        self._cooldown_frames = value

    def do_transform_ip(self, buffer):
        _, state, _ = self.get_state(0)
        if state != Gst.State.PLAYING:
            return Gst.FlowReturn.OK

        rmeta = GstAnalytics.buffer_get_analytics_relation_meta(buffer)
        person_found = False

        if rmeta:
            for mtd in rmeta:
                if isinstance(mtd, GstAnalytics.ODMtd):
                    label = GLib.quark_to_string(mtd.get_obj_type())
                    if label == "person":
                        person_found = True
                        break

        if person_found:
            self._frames_since_person = 0
            if not self._person_visible:
                self._person_visible = True
                self.notify("person-visible")
            return Gst.FlowReturn.OK
        else:
            self._frames_since_person += 1
            if self._frames_since_person > self._cooldown_frames:
                if self._person_visible:
                    self._person_visible = False
                    self.notify("person-visible")
                return GST_BASE_TRANSFORM_FLOW_DROPPED
            # Within cooldown — keep passing frames
            return Gst.FlowReturn.OK


GObject.type_register(PersonDetector)
__gstelementfactory__ = ("gvapersondetect_py", Gst.Rank.NONE, PersonDetector)
