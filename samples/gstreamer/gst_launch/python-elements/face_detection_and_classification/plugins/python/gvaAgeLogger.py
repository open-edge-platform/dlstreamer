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
                for i in range(mtd.get_length()):
                    quark = mtd.get_quark(i)
                    if quark:
                        label = GLib.quark_to_string(quark)
                        # Age values from model-proc text converter are numeric strings
                        if label and label.isdigit():
                            self._log_file.write(label + "\n")

        return Gst.FlowReturn.OK


GObject.type_register(AgeLogger)
__gstelementfactory__ = ("gvaagelogger_py", Gst.Rank.NONE, AgeLogger)
