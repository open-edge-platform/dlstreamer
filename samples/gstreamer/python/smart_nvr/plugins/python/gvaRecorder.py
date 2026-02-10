#!/usr/bin/env python
# -*- Mode: Python -*-
# vi:si:et:sw=4:sts=4:ts=4

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

    __gproperties__ = {
        "fileprefix": (str,
                    "fileprefix",
                    "Output file name prefix (without extension)",
                    "output",
                    GObject.ParamFlags.READWRITE),    
    }               

    def __init__(self):
        super(Recorder, self).__init__()

        # constrruct decode + file store pipeline and connect elements
        self._convert = Gst.ElementFactory.make("videoconvert", "convert")
        self._vah264enc = Gst.ElementFactory.make("vah264enc", "encoder")
        self._h264parse = Gst.ElementFactory.make("h264parse", "h264parse")
        self._filesink = Gst.ElementFactory.make("splitmuxsink", "splitmuxsink")
        self._filesink.set_property("location", "output%02d.mp4")
        self.add(self._convert)
        self.add(self._vah264enc)
        self.add(self._h264parse)
        self.add(self._filesink)
        self._convert.link(self._vah264enc)
        self._vah264enc.link(self._h264parse)
        self._h264parse.link(self._filesink)
        self.add_pad(Gst.GhostPad.new("sink", self._convert.get_static_pad("sink")))

        # register callbacks to generate file split signals and collect prediction data
        self._framecount = 0
        self._objectlist = []
        self._fileprefix = "output"
        self.get_static_pad("sink").add_probe(Gst.PadProbeType.BUFFER, self.buffer_probe, 0)
        self._filesink.connect("format-location", self.format_location_callback, 0)

    def do_get_property(self, prop):
        if prop.name == "fileprefix":
            return self._fileprefix
        else:
            raise AttributeError('unknown property %s' % prop.name)

    def do_set_property(self, prop, value):
        if prop.name == "fileprefix":
            self._fileprefix = value
        else: 
            raise AttributeError('unknown property %s' % prop.name)

    # method called on each new video buffer received by recorder element
    # colled information on prediction medatada and periodically request new video file generation
    def buffer_probe(self, pad, info, u_data):
        # collect detected objects
        buffer = info.get_buffer()
        rmeta = GstAnalytics.buffer_get_analytics_relation_meta(buffer)
        if rmeta:
            for mtd in rmeta:
                if type(mtd) == GstAnalytics.ODMtd:
                    category = GLib.quark_to_string(mtd.get_obj_type())
                    if category not in self._objectlist:
                        self._objectlist.append(category)

        self._framecount += 1
        if self._framecount % 500 == 0:
            print(f"Generating new video segment, objects found in last segment: {self._objectlist}")
            self._filesink.emit("split-after")
            self._objectlist = []

        return Gst.PadProbeReturn.OK

    # method called when a new video file fragment is to be generated
    # use same name for video file and metadata, based on fragment_id
    def format_location_callback(self, splitmux, fragment_id, udata):
        file = open(f"{self._fileprefix}-{fragment_id:02d}.txt", 'w')
        file.write(f"Objects: {self._objectlist}")
        file.close()
        return GLib.strdup(f"{self._fileprefix}-{fragment_id:02d}.mp4")

GObject.type_register(Recorder)
__gstelementfactory__ = ("gvarecorder_py", Gst.Rank.NONE, Recorder)