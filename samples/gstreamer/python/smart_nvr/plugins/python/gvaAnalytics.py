#!/usr/bin/env python
# -*- Mode: Python -*-
# vi:si:et:sw=4:sts=4:ts=4

import gi
gi.require_version('GstBase', '1.0')

from gi.repository import Gst, GObject, GstBase
Gst.init_python()

#
# DLStreamer analytics element
#
class Analytics(GstBase.BaseTransform):
    __gstmetadata__ = ('GVA Analytics Python','Transform', \
                      'Single-camera analytics element to count objects and generate tripwire events', \
                      'Intel DLStreamer')

    __gsttemplates__ = (Gst.PadTemplate.new("src",
                                           Gst.PadDirection.SRC,
                                           Gst.PadPresence.ALWAYS,
                                           Gst.Caps.new_any()),
                       Gst.PadTemplate.new("sink",
                                           Gst.PadDirection.SINK,
                                           Gst.PadPresence.ALWAYS,
                                           Gst.Caps.new_any()))

    def __init__(self):
        super(Analytics, self).__init__()
        self._framecount = 0

    def do_transform_ip(self, buffer):
        # empty body - just an example of how to access metadata and implement custom analytics logic
        self._framecount = self._framecount + 1
        return Gst.FlowReturn.OK

GObject.type_register(Analytics)
__gstelementfactory__ = ("gvaanalytics_py", Gst.Rank.NONE, Analytics)