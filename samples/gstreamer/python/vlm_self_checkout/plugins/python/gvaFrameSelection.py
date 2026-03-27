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
    first_seen: int = 0
    last_seen: int = 0
    published: bool = False

# Python equivalent of C macro GST_BASE_TRANSFORM_FLOW_DROPPED
GST_BASE_TRANSFORM_FLOW_DROPPED = Gst.FlowReturn.CUSTOM_SUCCESS

class FrameSelection(GstBase.BaseTransform):
    """Frame selection logic for Loss Prevention."""

    __gstmetadata__ = (
        "GVA Loss Prevention Python",
        "Transform",
        "Passes frames which are tracked for at least N milliseconds and drops the rest",
        "Intel DLStreamer",
    )

    __gsttemplates__ = (
        Gst.PadTemplate.new("src", Gst.PadDirection.SRC,
                            Gst.PadPresence.ALWAYS, Gst.Caps.new_any()),
        Gst.PadTemplate.new("sink", Gst.PadDirection.SINK,
                            Gst.PadPresence.ALWAYS, Gst.Caps.new_any()),
    )

    # Element properties: default values and setters/getters 
    _threshold = 1500  # default threshold in miliseconds
    _genai_name = ""  # name of the gvagenai element to control
    _inventory_file = ""  # path to inventory file
    _excluded_objects_file = ""  # path to excluded objects file

    @GObject.Property(type=int)
    def threshold(self):
        'Number of miliseconds an object must be visible and tracked before publishing.'
        return self._threshold

    @threshold.setter
    def threshold(self, value):
        self._threshold = value

    @GObject.Property(type=str, nick="genai-name",
                      blurb="Name of the gvagenai element whose prompt-path to update dynamically")
    def genai_name(self):
        return self._genai_name

    @genai_name.setter
    def genai_name(self, value):
        self._genai_name = value

    @GObject.Property(type=str, nick="inventory-file",
                      blurb="Path to a text file listing inventory items, one per line")
    def inventory_file(self):
        return self._inventory_file

    @inventory_file.setter
    def inventory_file(self, value):
        self._inventory_file = value
        if value:
            self._inventory = self._parse_file(value, "inventory")

    @GObject.Property(type=str, nick="excluded-objects-file",
                      blurb="Path to a text file listing excluded object types, one per line")
    def excluded_objects_file(self):
        return self._excluded_objects_file

    @excluded_objects_file.setter
    def excluded_objects_file(self, value):
        self._excluded_objects_file = value
        if value:
            self._excluded_objects = self._parse_file(value, "excluded objects")

    def __init__(self):
        super().__init__()
        self._framecount = 0
        self._tracked_objects: dict[int, TrackedObject] = {}  # tracking_id -> TrackedObject
        self._genai_element = None  # resolved reference to the gvagenai element
        self._excluded_objects = []  # object types to exclude from publishing
        self._inventory = []

    @staticmethod
    def _parse_file(path: str, label: str = "items") -> list:
        """Read items from a text file, one item per line."""
        items = []
        try:
            with open(path, "r") as f:
                for line in f:
                    item = line.strip()
                    if item and not item.startswith("#"):
                        items.append(item)
            print(f"[gvalossprevention] Loaded {len(items)} {label} from '{path}'")
        except OSError as e:
            print(f"[gvalossprevention] ERROR: cannot read {label} file '{path}': {e}")
        return items

    def _resolve_genai_element(self):
        """Look up the gvagenai element by name from the parent pipeline."""
        if self._genai_element is not None:
            return self._genai_element
        if not self._genai_name:
            return None
        pipeline = self.get_parent()
        if pipeline is None:
            return None
        self._genai_element = pipeline.get_by_name(self._genai_name)
        if self._genai_element is None:
            print(f"[gvalossprevention] WARNING: could not find element named '{self._genai_name}'")
        return self._genai_element

    def _update_genai_prompt(self, prompt_text: str):
        """Set prompt property on the gvagenai element."""
        if not self._genai_element:
            self._resolve_genai_element()
        if self._genai_element is None:
            print(f"[gvalossprevention] WARNING: cannot update gvagenai prompt because element reference could not be resolved")
            return
        self._genai_element.set_property("prompt", prompt_text)

    def do_transform_ip(self, buffer):
        """Frame selection logic for Loss Prevention.
            1) Select frames with at least one object detected
            2) Select frames with object tracked for > threshold milliseconds
            3) Set gvagenai prompt to classify which item from the inventory is visible in the frame"""
        
        _, state, _ = self.get_state(0)
        if state != Gst.State.PLAYING:
            return Gst.FlowReturn.OK
        
        # Convert frame timestamp to miliseconds
        self._framecount += 1
        total_miliseconds = buffer.pts // Gst.MSECOND if buffer.pts != Gst.CLOCK_TIME_NONE else 0

        # Drop frame if no object detected
        rmeta = GstAnalytics.buffer_get_analytics_relation_meta(buffer)
        if not rmeta:
            return GST_BASE_TRANSFORM_FLOW_DROPPED

        for mtd in rmeta:
            if isinstance(mtd, GstAnalytics.ODMtd):
                # skip objects of excluded types (e.g. person, dining_table)
                if GLib.quark_to_string(mtd.get_obj_type()) in self._excluded_objects:
                    continue
                # find tracker objects associated with detected objects their stats
                for other in rmeta:
                    if not isinstance(other, GstAnalytics.TrackingMtd):
                        continue
                    rel = rmeta.get_relation(mtd.id, other.id)
                    if rel == GstAnalytics.RelTypes.RELATE_TO:
                        success, tracking_id, _, _, _ = other.get_info()
                        if success:
                            if tracking_id not in self._tracked_objects:
                                self._tracked_objects[tracking_id] = TrackedObject(mtd.get_obj_type(), 1, False)
                                self._tracked_objects[tracking_id].first_seen = total_miliseconds
                            elif self._tracked_objects[tracking_id].quark == mtd.get_obj_type():
                                self._tracked_objects[tracking_id].last_seen = total_miliseconds


        # for other frames check if objects are visible for preconfigured number of frames and publish
        for self_tracked_id, tracked_obj in self._tracked_objects.items():
            if (tracked_obj.last_seen - tracked_obj.first_seen) >= self._threshold and not tracked_obj.published:
                tracked_obj.published = True
                obj_name = GLib.quark_to_string(tracked_obj.quark).lower()
                matched_items = [item for item in self._inventory if obj_name in item.lower() or item.lower() in obj_name]
                if matched_items:
                    items_list = ", ".join(matched_items)
                    self._update_genai_prompt(
                        f"Which of the following items is visible in this image: {items_list}? "
                        f"Reply only with names of detected items. If no items from the list are visible, reply None.")
                return Gst.FlowReturn.OK

        return GST_BASE_TRANSFORM_FLOW_DROPPED

GObject.type_register(FrameSelection)
__gstelementfactory__ = ("gvalossprevention_py", Gst.Rank.NONE, FrameSelection)
