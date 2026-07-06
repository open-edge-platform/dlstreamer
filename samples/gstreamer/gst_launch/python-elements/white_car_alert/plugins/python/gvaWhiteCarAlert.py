# ==============================================================================
# Copyright (C) 2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================
# pylint: disable=invalid-name,wrong-import-position,duplicate-code

"""
This module implements a custom GStreamer Transform element that prints a
terminal notification whenever a car of a given color (default: white) is
detected on the video.

It reads GstAnalytics classification metadata (ClsMtd) produced by
`gvaclassify` with the `vehicle-attributes-recognition-barrier-0039` model.
That model emits two attributes per detected vehicle:
  * color: one of white, gray, yellow, red, green, blue, black
  * type:  one of car, van, truck, bus

The element keys on the color attribute and prints an alert line to stdout,
throttled by stream time so the terminal is not flooded on every frame.
"""

import gi

gi.require_version("Gst", "1.0")
gi.require_version("GstBase", "1.0")
gi.require_version("GstAnalytics", "1.0")
from gi.repository import (  # pylint: disable=no-name-in-module
    Gst,
    GstBase,
    GObject,
    GLib,
    GstAnalytics,
)

Gst.init_python()


class WhiteCarAlert(GstBase.BaseTransform):
    """DLStreamer custom element that prints an alert when a white car is detected."""

    # Color labels produced by vehicle-attributes-recognition-barrier-0039.
    # Used to skip ClsMtd produced by the "type" attribute (car/van/truck/bus)
    # or by other classifier stages in the same pipeline.
    _COLOR_LABELS = frozenset(
        ["white", "gray", "yellow", "red", "green", "blue", "black"]
    )
    # Vehicle "type" labels; used to confirm the classified object is a car.
    _CAR_TYPE_LABEL = "car"

    __gstmetadata__ = (
        "GVA White Car Alert Python",
        "Transform",
        "Print a terminal notification when a car of the given color is detected",
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

    # Element properties: default values and setters/getters
    _color = "white"
    _min_confidence = 0.5
    _alert_interval = 1.0

    @GObject.Property(type=str)
    def color(self):
        "Vehicle color to alert on (e.g. white, red, black)."
        return self._color

    @color.setter
    def color(self, value):
        self._color = value.lower() if value else "white"

    @GObject.Property(type=float)
    def min_confidence(self):
        "Minimum color-classification confidence to trigger an alert."
        return self._min_confidence

    @min_confidence.setter
    def min_confidence(self, value):
        self._min_confidence = value

    @GObject.Property(type=float)
    def alert_interval(self):
        "Minimum stream-time seconds between alerts (0 = alert on every frame)."
        return self._alert_interval

    @alert_interval.setter
    def alert_interval(self, value):
        self._alert_interval = value

    def __init__(self):
        super().__init__()
        self._last_alert_sec = None

    @staticmethod
    def _top_label_and_level(mtd):
        """Return (label, confidence) of the top-1 class of a ClsMtd, or (None, 0)."""
        if mtd.get_length() == 0:
            return None, 0.0
        quark = mtd.get_quark(0)
        if not quark:
            return None, 0.0
        label = GLib.quark_to_string(quark)
        level = mtd.get_level(0)
        return label, level

    def _count_matching_cars(self, rmeta):
        """Count classifications matching the target color and track type/confidence.

        Returns a tuple ``(count, max_confidence, is_car_seen)`` where ``count``
        is the number of ClsMtd whose top color label matches ``self._color``
        above the confidence threshold, ``max_confidence`` is the highest such
        confidence, and ``is_car_seen`` is True if any ClsMtd reports the "car"
        vehicle type in this frame.
        """
        count = 0
        max_confidence = 0.0
        is_car_seen = False

        for mtd in rmeta:
            if not isinstance(mtd, GstAnalytics.ClsMtd):
                continue
            label, level = self._top_label_and_level(mtd)
            if not label:
                continue
            if label == self._CAR_TYPE_LABEL:
                is_car_seen = True
            elif label in self._COLOR_LABELS:
                if label == self._color and level >= self._min_confidence:
                    count += 1
                    max_confidence = max(max_confidence, level)

        return count, max_confidence, is_car_seen

    def _should_alert(self, buffer):
        """Return the current stream time (s) if the throttle allows an alert."""
        pts = buffer.pts
        if pts == Gst.CLOCK_TIME_NONE:
            current_sec = None
        else:
            current_sec = pts / Gst.SECOND

        if self._alert_interval <= 0 or current_sec is None:
            return current_sec, True

        if (
            self._last_alert_sec is None
            or (current_sec - self._last_alert_sec) >= self._alert_interval
        ):
            return current_sec, True
        return current_sec, False

    def do_transform_ip(self, buffer):  # pylint: disable=arguments-differ
        """Inspect classification metadata and print an alert for matching cars."""
        rmeta = GstAnalytics.buffer_get_analytics_relation_meta(buffer)
        if not rmeta:
            return Gst.FlowReturn.OK

        count, confidence, is_car_seen = self._count_matching_cars(rmeta)
        if count == 0:
            return Gst.FlowReturn.OK

        current_sec, allowed = self._should_alert(buffer)
        if not allowed:
            return Gst.FlowReturn.OK

        vehicle = "car" if is_car_seen else "vehicle"
        timestamp = f"t={current_sec:6.2f}s" if current_sec is not None else "t=?"
        count_info = "" if count == 1 else f" (count: {count})"
        print(
            f"[ALERT] {timestamp}: detected {self._color} {vehicle}{count_info} "
            f"(color confidence: {confidence:.2f})",
            flush=True,
        )
        self._last_alert_sec = current_sec

        return Gst.FlowReturn.OK


GObject.type_register(WhiteCarAlert)
__gstelementfactory__ = ("gvawhitecaralert_py", Gst.Rank.NONE, WhiteCarAlert)
