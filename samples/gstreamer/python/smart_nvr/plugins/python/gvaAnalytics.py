# ==============================================================================
# Copyright (C) 2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================

"""
This module implements a custom GStreamer Transform element to detect lane hogging events.
It identifies cars and trucks that intersect with a predefined outer-lane zone and classifies them as 'hogging'
if no neighboring vehicles are present within a specified distance and angle threshold.
"""

import gi
import math
import numpy as np
import cv2

gi.require_version('GstBase', '1.0')
gi.require_version("GstAnalytics", "1.0")
from gi.repository import Gst, GstBase, GObject, GLib, GstAnalytics # pylint: disable=no-name-in-module
Gst.init_python()

class Analytics(GstBase.BaseTransform):
    """DLStreamer custom Analytics element to detect lane hogging events."""
    __gstmetadata__ = ('GVA Analytics Python','Transform', \
                      'Single-camera analytics element to detect hogging lane cars', \
                      'Intel DLStreamer')

    __gsttemplates__ = (Gst.PadTemplate.new("src",
                                           Gst.PadDirection.SRC,
                                           Gst.PadPresence.ALWAYS,
                                           Gst.Caps.new_any()),
                       Gst.PadTemplate.new("sink",
                                           Gst.PadDirection.SINK,
                                           Gst.PadPresence.ALWAYS,
                                           Gst.Caps.new_any()))

    # Element properties: default values and setters/getters 
    _distance = 300
    _angle = [-135, -45]
    _zone = np.array([[300, 800], [750, 800], [875, 450], [725, 450]], dtype=np.float32) 

    @GObject.Property(type=str)
    def zone(self):
        'Zone to analze. List of points in format x1,y1,x2,y2,... defining a polygon.'
        return self._zone

    @zone.setter
    def zone(self, value):
        self._zone = np.array([int(coord) for coord in value.split(",")],
                              dtype=np.float32).reshape(-1, 2)

    @GObject.Property(type=int)
    def distance(self):
        'Distance to neighbor cars to consider for hogging classification.'
        return self._distance

    @distance.setter
    def distance(self, value):
        self._distance = value

    @GObject.Property(type=str)
    def angle(self):
        'angle_min,angle_max pair to consider for hogging classification.'
        return self._angle

    @angle.setter
    def angle(self, value):
        self._angle = np.array([int(coord) for coord in value.split(",")], dtype=np.float32)

    def __init__(self):
        super(Analytics, self).__init__()
        self._framecount = 0

    def do_transform_ip(self, buffer):
        """Detect cars in the inspection zone and classify as 'hogging' if no neighbour cars present."""
        self._framecount = self._framecount + 1

        # iterate over analytics metadata attached to a frame buffer
        rmeta = GstAnalytics.buffer_get_analytics_relation_meta(buffer)
        if not rmeta:
            return Gst.FlowReturn.OK

        # filter detected car/trucks objects
        for mtd in rmeta:
            if (isinstance(mtd, GstAnalytics.ODMtd) and
                GLib.quark_to_string(mtd.get_obj_type()) in ["car", "truck"]
            ):
                _, x, y, w, h, confidence = mtd.get_location()
                box = np.array([[x, y], [x + w, y], [x + w, y + h], [x, y + h]], dtype=np.float32)

                # filter cars/trucks ovelaping with inspection zone
                area, _ = cv2.intersectConvexConvex(self._zone, box, True)
                if area > w * h / 2:
                    # check if another car/truck is within distance and angle to be considered neighbour
                    neighbour_found = False
                    for mtd in rmeta:
                        if (isinstance(mtd, GstAnalytics.ODMtd) and
                             GLib.quark_to_string(mtd.get_obj_type()) in ["car", "truck"]
                        ):
                            # compute distance and angle between centers of two objects
                            _, x2, y2, w2, h2, _ = mtd.get_location()
                            center1 = np.array([x + w/2, y + h/2])
                            center2 = np.array([x2 + w2/2, y2 + h2/2])
                            distance = np.linalg.norm(center2 - center1)
                            angle = math.degrees(math.atan2(center2[0] - center1[0], center2[1] - center1[1]))
                            if (0 < distance < self._distance and self._angle[0] < angle < self._angle[1]):
                                neighbour_found = True
                                        
                    # if car in inspection zone and no neigbour cars, classify as 'hogging lane'
                    if not neighbour_found:
                        rmeta.add_od_mtd(GLib.quark_from_string("hogging"), x, y, w, h, confidence)

        # draw top and bottom lines of the inspection zone as metadata to be used by gvawatermark element
        rmeta.add_od_mtd(GLib.quark_from_string("start"),
                         self._zone[0,0], self._zone[0,1], self._zone[1,0] - self._zone[0,0], 2, 0)
        rmeta.add_od_mtd(GLib.quark_from_string("stop"),
                         self._zone[3,0], self._zone[3,1], self._zone[2,0] - self._zone[3,0], 2, 0)

        return Gst.FlowReturn.OK

GObject.type_register(Analytics)
__gstelementfactory__ = ("gvaanalytics_py", Gst.Rank.NONE, Analytics)