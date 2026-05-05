# ==============================================================================
# Copyright (C) 2018-2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================
# pylint: disable=invalid-name,wrong-import-position,duplicate-code

"""
This module implements a custom GStreamer Transform element to log detected ages
from classification metadata to a file. It reads GstAnalytics classification
metadata (ClsMtd) produced by gvaclassify and writes age values to a log file.

Replaces the gvapython-based AgeLogger with a proper GStreamer Python element.
"""

import re

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


class AgeLogger(GstBase.BaseTransform):
    """DLStreamer custom element to log detected ages from classification metadata."""

    # Age-group labels produced by the fairface age model, e.g. "0-2", "3-9",
    # "20-29", "more than 70". Used to skip ClsMtd produced by other classifier
    # stages in the same pipeline (e.g. gender: "Male"/"Female").
    _AGE_LABEL_RE = re.compile(r"^(\d+-\d+|more than \d+)$")

    __gstmetadata__ = (
        "GVA Age Logger Python",
        "Transform",
        "Log detected ages from classification metadata to a file",
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
    _log_file_path = "/tmp/age_log.txt"

    @GObject.Property(type=str)
    def log_file_path(self):
        "Path to the log file for age values."
        return self._log_file_path

    @log_file_path.setter
    def log_file_path(self, value):
        self._log_file_path = value

    def __init__(self):
        super().__init__()
        self._log_file = None

    def do_start(self):  # pylint: disable=arguments-differ
        """Open log file when element starts."""
        self._log_file = open(  # pylint: disable=consider-using-with
            self._log_file_path, "a", encoding="utf-8"
        )
        return True

    def do_stop(self):  # pylint: disable=arguments-differ
        """Close log file when element stops."""
        if self._log_file:
            self._log_file.close()
            self._log_file = None
        return True

    def do_transform_ip(self, buffer):  # pylint: disable=arguments-differ
        """Read classification metadata and log age values to file."""
        rmeta = GstAnalytics.buffer_get_analytics_relation_meta(buffer)
        if not rmeta:
            return Gst.FlowReturn.OK

        for mtd in rmeta:
            if isinstance(mtd, GstAnalytics.ClsMtd):
                # Pick the top-1 class only (highest confidence) to avoid
                # logging every candidate label for the same ROI.
                if mtd.get_length() == 0:
                    continue
                quark = mtd.get_quark(0)
                if not quark:
                    continue
                label = GLib.quark_to_string(quark)
                # The fairface age model emits age-group labels such as
                # "0-2", "3-9", ..., "20-29", "more than 70". Filter out
                # ClsMtd from other classifier stages (e.g. gender) by
                # matching the expected age-group format.
                if label and self._AGE_LABEL_RE.match(label):
                    self._log_file.write(label + "\n")

        return Gst.FlowReturn.OK


GObject.type_register(AgeLogger)
__gstelementfactory__ = ("gvaagelogger_py", Gst.Rank.NONE, AgeLogger)
