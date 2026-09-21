# ==============================================================================
# Copyright (C) 2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================

"""
Custom GStreamer element that counts loitering events and adds watermark text.
"""
import gi

gi.require_version("Gst", "1.0")
gi.require_version("GstBase", "1.0")
gi.require_version("GstAnalytics", "1.0")
gi.require_version("DLStreamerMeta", "1.0")
gi.require_version("DLStreamerWatermarkMeta", "1.0")
from gi.repository import Gst, GstBase, GObject, GLib, GstAnalytics, DLStreamerMeta, DLStreamerWatermarkMeta # pylint: disable=no-name-in-module, wrong-import-position

from gstgva.region_of_interest import RegionOfInterest

Gst.init([])

class LoiteringWatermark(GstBase.BaseTransform):
    """Custom GStreamer element that counts tripwire crossings and adds watermark text."""

    __gstmetadata__ = (
        "Loitering Watermark",
        "Filter/Analytics",
        "Counts loitering events and displays counters as watermark text",
        "Intel Corporation",
    )

    __gsttemplates__ = (
        Gst.PadTemplate.new("sink", Gst.PadDirection.SINK, Gst.PadPresence.ALWAYS,
                            Gst.Caps.new_any()),
        Gst.PadTemplate.new("src",  Gst.PadDirection.SRC,  Gst.PadPresence.ALWAYS,
                            Gst.Caps.new_any()),
    )

    # - Color of the dashboard text in BGR format (Default is gray)
    _dashboard_color = [65, 65, 65]

    # - Color of the loitering text in BGR format (Default is red)
    _loitering_dashboard_color = [255, 0, 0]

    # Property: quiet_mode (boolean)
    _quiet_mode = False
    @GObject.Property(type=str, nick="quiet-mode", blurb="Suppress watermarking if True")
    def quiet_mode(self):
        """
        Get the quiet mode status.
        Returns:
            bool: True if quiet mode is enabled, False otherwise.
        """
        return self._quiet_mode

    @quiet_mode.setter
    def quiet_mode(self, value:str):
        """
        Set the quiet mode status.
        Args:
            value (str): A string representation of the quiet mode status. Accepts "true", "1", "yes" for True and "false", "0", "no" for False
        """
        if value.lower() in ["true", "1", "yes"]:
            self._quiet_mode = True
        elif value.lower() in ["false", "0", "no"]:
            self._quiet_mode = False

    # Property: dashboard_pos (tuple of int)
    _dashboard_position = [800,60]
    @GObject.Property(type=str, nick="dashboard-pos", blurb="Position of dashboard text in the video frame (x,y)", default="800,60")
    def dashboard_pos(self):
        """
        Get the position of the dashboard text in the video frame.
        Returns:
            str: A string representation of the dashboard position in the format x,y.
        """
        return ",".join(map(str, self._dashboard_position))

    @dashboard_pos.setter
    def dashboard_pos(self, value:str):
        """
        Set the position of the dashboard text in the video frame.
        Args:
            value (str): A string representation of the dashboard position in the format x,y.
        """
        try:
            x, y = map(int, value.split(","))
            self._dashboard_position = [x, y]
        except ValueError:
            pass  # Ignore invalid values

    # Property: loitering_threshold (float)
    _loitering_threshold = 5.0  # seconds
    @GObject.Property(type=float, nick="loitering-threshold",
                      blurb="Time in seconds to consider an object as loitering",
                      minimum=0.0, maximum=10.0, default=5.0)
    def loitering_threshold(self):
        """
        Get the loitering threshold in seconds.
        Returns:
            float: The loitering threshold in seconds.
        """
        return self._loitering_threshold

    @loitering_threshold.setter
    def loitering_threshold(self, value:float):
        """
        Set the loitering threshold in seconds.
        Args:
            value (float): The loitering threshold in seconds. Must be non-negative.
        """
        if value >= 0:
            self._loitering_threshold = value

    def __init__(self):
        super().__init__()
        self.set_in_place(True)
        self.set_passthrough(False)
        self._allowed_types = ["car", "person"]

    def do_transform_ip(self, buffer):
        """Process buffer, read DwellTimeMtd from gvaanalytics, and add watermark text."""
        relation_meta = GstAnalytics.buffer_get_analytics_relation_meta(buffer)

        # track_id → {zone_id, object_type, dwelling_time}; populated from DwellTimeMtd written by gvaanalytics
        tracked_objects: dict = {}

        if relation_meta:
            for od_mtd in relation_meta.iter_on_type(GstAnalytics.ODMtd):
                obj_type = GLib.quark_to_string(od_mtd.get_obj_type())
                if not obj_type or (obj_type.lower() not in self._allowed_types):
                    continue
                for trk_mtd in od_mtd.iter_direct_related(GstAnalytics.RelTypes.RELATE_TO, GstAnalytics.TrackingMtd):
                    track_ok, track_id, _, _, _ = trk_mtd.get_info()
                    if not track_ok:
                        continue
                    for dwell_mtd in od_mtd.iter_direct_related(GstAnalytics.RelTypes.RELATE_TO, DLStreamerMeta.DwellTimeMtd):
                        ok, zone_id, dwelling_time, _ = dwell_mtd.get_info()
                        if not ok:
                            continue
                        tracked_objects[track_id] = {
                            "zone_id": zone_id,
                            "object_type": obj_type,
                            "dwelling_time": dwelling_time,
                        }

        if not self._quiet_mode:
            for idx, (track_id, record) in enumerate(tracked_objects.items()):
                text = f"{record['zone_id']}: {record['object_type']}-{track_id:02d} : {record['dwelling_time']:.01f}s"
                color = self._loitering_dashboard_color if record['dwelling_time'] >= self._loitering_threshold else self._dashboard_color
                DLStreamerWatermarkMeta.text_meta_add(
                    buffer, x=self._dashboard_position[0], y=self._dashboard_position[1] + idx * 30,
                    text=text,
                    font_scale=1.0, font_type=0,  # cv::FONT_HERSHEY_SIMPLEX
                    r=color[0], g=color[1], b=color[2],
                    thickness=1, draw_bg=True)

        return Gst.FlowReturn.OK


GObject.type_register(LoiteringWatermark)
__gstelementfactory__ = ("loitering_watermark", Gst.Rank.NONE, LoiteringWatermark)
