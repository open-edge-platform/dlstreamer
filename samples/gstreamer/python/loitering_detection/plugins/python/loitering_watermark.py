#!/usr/bin/env python3
from typing import List,TypedDict
from tinydb import TinyDB, Query
from tinydb.storages import MemoryStorage

import gi

gi.require_version("Gst", "1.0")
gi.require_version("GstBase", "1.0")
gi.require_version("GstAnalytics", "1.0")
gi.require_version("DLStreamerMeta", "1.0")
gi.require_version("DLStreamerWatermarkMeta", "1.0")
from gi.repository import Gst, GstBase, GObject, GLib, GstAnalytics, DLStreamerMeta, DLStreamerWatermarkMeta  

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

    # Property: quiet_mode (boolean) 
    # - If True, suppresses watermarking

    class DwellingRecord(TypedDict):
        zone_id: str
        object_type: str
        track_id: int
        first_seen_timestamp: float
        last_seen_timestamp: float
        dwelling_time: float

    _stale_threshold = 5.0  # seconds
    _object_db = TinyDB(storage=MemoryStorage)  # In-memory database to store loitering records
    
    _quiet_mode = False

    _dashboard_position = [800,60] # Place dashboard text at (800, 60) in the video frame
    _dashboard_color = [65, 65, 65] # Gray color for dashboard text
    _loitering_threshold = 5.0  # seconds
    _loitering_dashboard_color = [255, 0, 0]  # Red color for loitering text

    @GObject.Property(type=str, nick="quiet-mode", blurb="Suppress watermarking if True")
    def quiet_mode(self):
        return self._quiet_mode

    @quiet_mode.setter
    def quiet_mode(self, value:str):
        if value.lower() in ["true", "1", "yes"]:
            self._quiet_mode = True
        elif value.lower() in ["false", "0", "no"]:
            self._quiet_mode = False

    @GObject.Property(type=str, nick="dashboard-pos", blurb="Position of dashboard text in the video frame (x,y)", default="800,60")
    def dashboard_pos(self):
        return ",".join(map(str, self._dashboard_position))

    @dashboard_pos.setter
    def dashboard_pos(self, value:str):
        try:
            x, y = map(int, value.split(","))
            self._dashboard_position = [x, y]
        except ValueError:
            pass  # Ignore invalid values

    @GObject.Property(type=float, nick="loitering-threshold", blurb="Time in seconds to consider an object as loitering", minimum=0.0, maximum=10.0, default=5.0)
    def loitering_threshold(self):
        return self._loitering_threshold

    @loitering_threshold.setter
    def loitering_threshold(self, value:float):
        if value >= 0:
            self._loitering_threshold = value    

    def __init__(self):
        super().__init__()
        self.set_in_place(True)
        self.set_passthrough(False)
        self._allowed_types = set("person")

    def do_transform_ip(self, buffer):

        time_now = round(buffer.pts/Gst.SECOND,2)  # Convert nanoseconds to seconds

        """Process buffer, record in-zone objects, and add watermark text."""
        relation_meta = GstAnalytics.buffer_get_analytics_relation_meta(buffer)

        if relation_meta:
            for od_mtd in relation_meta.iter_on_type(GstAnalytics.ODMtd):
                obj_type = GLib.quark_to_string(od_mtd.get_obj_type())
                if not obj_type and obj_type.lower() not in self._allowed_types:
                    continue
                for trk_mtd in od_mtd.iter_direct_related(GstAnalytics.RelTypes.RELATE_TO, GstAnalytics.TrackingMtd):
                    track_ok, track_id, _ , _, _ = trk_mtd.get_info()
                    if not track_ok:
                        continue
                    for zone_mtd in od_mtd.iter_direct_related(GstAnalytics.RelTypes.RELATE_TO, DLStreamerMeta.ZoneMtd):
                        zone_ok, zone_id = zone_mtd.get_info()
                        if not zone_ok:
                            continue

                        query_record = Query()
                        objects_in_zone = self._object_db.search(query_record.track_id == track_id)
                        if len(objects_in_zone) == 0:
                            new_record: LoiteringWatermark.DwellingRecord = {
                                "zone_id": zone_id,
                                "track_id": track_id,
                                "object_type": obj_type,
                                "first_seen_timestamp": time_now,
                                "last_seen_timestamp": time_now,
                                "dwelling_time": 0.0
                            }
                            self._object_db.insert(new_record)
                        else:
                            # If the object is already in the zone database, we update its dwelling time.
                            dwelling_time = time_now - objects_in_zone[0]["first_seen_timestamp"]
                            self._object_db.update({
                                "last_seen_timestamp": time_now, 
                                "dwelling_time": dwelling_time
                                }, query_record.track_id == track_id)

        # Remove stale records from the object database
        query_record = Query()
        self._object_db.remove(query_record.last_seen_timestamp < time_now)
        
        if not self._quiet_mode:
            for idx, record in enumerate(self._object_db.all()):
                text=f"{record['zone_id']}: {record['object_type']}-{record['track_id']:02d} : {record['dwelling_time']:.01f}s"
                if record['dwelling_time'] >= self._loitering_threshold:
                    DLStreamerWatermarkMeta.text_meta_add(
                        buffer, x=self._dashboard_position[0], y=self._dashboard_position[1]+idx*30,
                        text=text,
                        font_scale=1.0, font_type=0,  # cv::FONT_HERSHEY_SIMPLEX
                        r=self._loitering_dashboard_color[0], g=self._loitering_dashboard_color[1], b=self._loitering_dashboard_color[2],
                        thickness=1, draw_bg=True)
                else:
                    DLStreamerWatermarkMeta.text_meta_add(
                        buffer, x=self._dashboard_position[0], y=self._dashboard_position[1]+idx*30,
                        text=text,
                        font_scale=1.0, font_type=0,  # cv::FONT_HERSHEY_SIMPLEX
                        r=self._dashboard_color[0], g=self._dashboard_color[1], b=self._dashboard_color[2],
                        thickness=1, draw_bg=True)
                
        return Gst.FlowReturn.OK


GObject.type_register(LoiteringWatermark)
__gstelementfactory__ = ("loitering_watermark", Gst.Rank.NONE, LoiteringWatermark)
