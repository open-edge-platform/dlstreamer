#!/usr/bin/env python
# -*- Mode: Python -*-
# vi:si:et:sw=4:sts=4:ts=4

from multiprocessing.util import info

import gi
gi.require_version("Gst", "1.0")
gi.require_version("GstAnalytics", "1.0")

from gi.repository import Gst, GObject, GLib, GstAnalytics
Gst.init_python()

#
# DLStreamer smart recorder element
#
class Recorder(Gst.Bin):
    __gstmetadata__ = ('GVA Smart Recorder Python','Sink', \
                      'Record video stream to a file', \
                      'Intel DLStreamer')

    # Element properties: default values and setters/getters
    _location = "output.mp4"
    _fileprefix = "output"
    _max_time = 10

    @GObject.Property(type=str)
    def location(self):
        'File location to store recorded video.'
        return self._location

    @location.setter
    def location(self, value):
        self._location = value
        self._filesink.set_property("location", self._location)
        self._fileprefix = self._location.rsplit(".", 1)[0]

    @GObject.Property(type=int)
    def max_time(self):
        'Interval to split recorded video files, in seconds.'
        return self._max_time

    @max_time.setter
    def max_time(self, value):
        self._max_time = value
        self._filesink.set_property("max-size-time", self._max_time * Gst.SECOND)

    def __init__(self):
        super(Recorder, self).__init__()

        # construct pipeline: videoconvert -> vah264enc -> h264parse -> splitmuxsink
        self._convert = Gst.ElementFactory.make("videoconvert", "convert")
        self._vah264enc = Gst.ElementFactory.make("vah264enc", "encoder")
        self._h264parse = Gst.ElementFactory.make("h264parse", "h264parse")
        self._filesink = Gst.ElementFactory.make("splitmuxsink", "splitmuxsink")
        self.add(self._convert)
        self.add(self._vah264enc)
        self.add(self._h264parse)
        self.add(self._filesink)
        self._convert.link(self._vah264enc)
        self._vah264enc.link(self._h264parse)
        self._h264parse.link(self._filesink)
        self.add_pad(Gst.GhostPad.new("sink", self._convert.get_static_pad("sink")))

        # register callbacks to collect prediction data
        self._objectlist = []
        self._last_fragment_id = -1
        self.get_static_pad("sink").add_probe(Gst.PadProbeType.BUFFER, self.buffer_probe, 0)
        self.get_static_pad("sink").add_probe(Gst.PadProbeType.EVENT_DOWNSTREAM, self.event_probe, 0)
        self._filesink.connect("format-location", self.format_location_callback, 0)

    # store prediction metadata and clear list of detectd objects
    def save_metadata(self, fragment_id):
        file = open(f"{self._fileprefix}-{fragment_id:02d}.txt", 'w')
        file.write(f"Objects: {self._objectlist}")
        file.close()
        self._objectlist = []

    # method called on each new video buffer received by recorder element
    # collect information on prediction metadata
    def buffer_probe(self, pad, info, u_data):
        buffer = info.get_buffer()
        rmeta = GstAnalytics.buffer_get_analytics_relation_meta(buffer)
        if rmeta:
            for mtd in rmeta:
                if type(mtd) == GstAnalytics.ODMtd:
                    category = GLib.quark_to_string(mtd.get_obj_type())
                    _, confidence = mtd.get_confidence_lvl()
                    if confidence > 0 and category not in self._objectlist:
                        self._objectlist.append(category)

        return Gst.PadProbeReturn.OK

    # save metadata for the last video fragment
    def event_probe(self, pad, info, u_data):
        event = info.get_event()
        if event.type == Gst.EventType.EOS:
            self.save_metadata(self._last_fragment_id)
        return Gst.PadProbeReturn.OK

    # method called when a new video file fragment is to be generated
    # save metadata for previous video fragment and generate file name for next fragment
    def format_location_callback(self, splitmux, fragment_id, udata):
        self._last_fragment_id = fragment_id
        if fragment_id > 0:
            self.save_metadata(fragment_id - 1)

        return GLib.strdup(f"{self._fileprefix}-{fragment_id:02d}.mp4")

GObject.type_register(Recorder)
__gstelementfactory__ = ("gvarecorder_py", Gst.Rank.NONE, Recorder)