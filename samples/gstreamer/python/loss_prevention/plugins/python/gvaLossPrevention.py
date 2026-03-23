# ==============================================================================
# Copyright (C) 2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================

"""
Custom GStreamer element for loss-prevention analytics.
"""

from dataclasses import dataclass, field

import gi

gi.require_version("Gst", "1.0")
gi.require_version("GstBase", "1.0")
gi.require_version("GstAnalytics", "1.0")
from gi.repository import Gst, GstBase, GObject, GLib, GstAnalytics # pylint: disable=no-name-in-module
Gst.init_python()

@dataclass
class TrackedObject:
    quark: int  # GQuark (obj_type)
    count: int = 0
    published: bool = False

# Python equivalent of C macro GST_BASE_TRANSFORM_FLOW_DROPPED
GST_BASE_TRANSFORM_FLOW_DROPPED = Gst.FlowReturn.CUSTOM_SUCCESS

class LossPreventionAnalytics(GstBase.BaseTransform):
    """Frame selection logic for Loss Prevention."""

    __gstmetadata__ = (
        "GVA Loss Prevention Analytics Python",
        "Transform",
        "Passes frames which are tracked for at least N frames and drops the rest",
        "Intel DLStreamer",
    )

    __gsttemplates__ = (
        Gst.PadTemplate.new("src", Gst.PadDirection.SRC,
                            Gst.PadPresence.ALWAYS, Gst.Caps.new_any()),
        Gst.PadTemplate.new("sink", Gst.PadDirection.SINK,
                            Gst.PadPresence.ALWAYS, Gst.Caps.new_any()),
    )

    # Element properties: default values and setters/getters 
    _threshold = 30

    @GObject.Property(type=int)
    def threshold(self):
        'Number of frames an object must be visible and tracked before publishing.'
        return self._threshold

    @threshold.setter
    def threshold(self, value):
        self._threshold = value

    def __init__(self):
        super().__init__()
        self._framecount = 0
        self._tracked_objects: dict[int, TrackedObject] = {}  # tracking_id -> TrackedObject

    def do_transform_ip(self, buffer):
        """Frame selection logic for Loss Prevention.
            1) Select frames with at least one detected object
            2) Select frames without movement in last N frames
            3) Select frames with new object id (tracker)"""
        
        _, state, _ = self.get_state(0)
        if state != Gst.State.PLAYING:
            return Gst.FlowReturn.OK
        
        # Drop frame if no object detected
        self._framecount += 1
        rmeta = GstAnalytics.buffer_get_analytics_relation_meta(buffer)
        if not rmeta:
            # print(f"No objects detected, dropping frame")
            return GST_BASE_TRANSFORM_FLOW_DROPPED

        person_detected = False
        for mtd in rmeta:
            if isinstance(mtd, GstAnalytics.ODMtd):
                if mtd.get_obj_type() == GLib.quark_from_string("person"):
                    person_detected = True
                    continue
                # find associated tracker objects with objets other than person and update their count
                for other in rmeta:
                    if not isinstance(other, GstAnalytics.TrackingMtd):
                        continue
                    rel = rmeta.get_relation(mtd.id, other.id)
                    if rel == GstAnalytics.RelTypes.RELATE_TO:
                        success, tracking_id, _, _, _ = other.get_info()
                        if success:
                            if tracking_id not in self._tracked_objects:
                                self._tracked_objects[tracking_id] = TrackedObject(mtd.get_obj_type(), 1, False)
                            elif self._tracked_objects[tracking_id].quark == mtd.get_obj_type():
                                self._tracked_objects[tracking_id].count+= 1
                            else:
                                self._tracked_objects[tracking_id].quark = mtd.get_obj_type()
                                self._tracked_objects[tracking_id].count = 1

        # do not store frames with person detected
        if person_detected:
            return GST_BASE_TRANSFORM_FLOW_DROPPED

        # for other frames check if objects are visible for preconfigured number of frames and publish
        for self_tracked_id, tracked_obj in self._tracked_objects.items():
            if tracked_obj.count >= self._threshold and not tracked_obj.published:
                tracked_obj.published = True
                print(f"Frame {self._framecount}: Tracker {self_tracked_id} of type {GLib.quark_to_string(tracked_obj.quark)} has been seen for {tracked_obj.count} frames, passing frame")
                return Gst.FlowReturn.OK

        return GST_BASE_TRANSFORM_FLOW_DROPPED

GObject.type_register(LossPreventionAnalytics)
__gstelementfactory__ = ("gvalossprevention_py", Gst.Rank.NONE, LossPreventionAnalytics)
