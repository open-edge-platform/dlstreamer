# ==============================================================================
# Copyright (C) 2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================

"""
Custom GStreamer Bin element for event-triggered video recording.
Records video to sequentially numbered files (save-1.mp4, save-2.mp4, ...)
when upstream person detection element signals person presence.
Uses an internal sub-pipeline: appsrc → videoconvert → vah264enc → h264parse → mp4mux → filesink.
"""

import gi

gi.require_version("Gst", "1.0")
gi.require_version("GstApp", "1.0")
gi.require_version("GstAnalytics", "1.0")
from gi.repository import Gst, GObject, GLib, GstApp, GstAnalytics
Gst.init_python()


class EventRecorder(Gst.Bin):
    """Records video segments triggered by person detection events.

    Listens to the 'person-visible' property on the upstream gvapersondetect_py
    element. When a person appears, starts recording to save-N.mp4.
    When the person leaves, finalizes the current file and increments the counter.
    """

    __gstmetadata__ = (
        "GVA Event Recorder Python",
        "Sink",
        "Event-triggered video recorder with sequential file output",
        "Intel DLStreamer",
    )

    _location = "results/save"
    _segment_count = 0
    _recording = False
    _rec_pipeline = None
    _appsrc = None
    _caps_set = False
    _current_caps = None

    @GObject.Property(type=str)
    def location(self):
        """Base path for output files (e.g. 'results/save' → save-1.mp4, save-2.mp4)."""
        return self._location

    @location.setter
    def location(self, value):
        self._location = value

    def __init__(self):
        super().__init__()

        # Create a fakesink as the primary sink for the bin
        self._fakesink = Gst.ElementFactory.make("fakesink", "event_fakesink")
        self._fakesink.set_property("sync", False)
        self._fakesink.set_property("async", False)
        self.add(self._fakesink)

        ghost = Gst.GhostPad.new("sink", self._fakesink.get_static_pad("sink"))
        ghost.add_probe(Gst.PadProbeType.BUFFER, self._on_buffer, None)
        ghost.add_probe(Gst.PadProbeType.EVENT_DOWNSTREAM, self._on_event, None)
        self.add_pad(ghost)

    def _get_person_visible(self, buffer):
        """Check if person is detected in current frame metadata."""
        rmeta = GstAnalytics.buffer_get_analytics_relation_meta(buffer)
        if rmeta:
            for mtd in rmeta:
                if isinstance(mtd, GstAnalytics.ODMtd):
                    label = GLib.quark_to_string(mtd.get_obj_type())
                    if label == "person":
                        return True
        return False

    def _on_buffer(self, pad, info, user_data):
        """Process each buffer — start/stop recording based on person presence."""
        buffer = info.get_buffer()
        if buffer is None:
            return Gst.PadProbeReturn.OK

        # Capture caps from the first buffer
        if not self._caps_set:
            caps = pad.get_current_caps()
            if caps:
                self._current_caps = caps
                self._caps_set = True

        person_visible = self._get_person_visible(buffer)

        if person_visible and not self._recording:
            self._start_recording()
        elif not person_visible and self._recording:
            self._stop_recording()

        if self._recording and self._appsrc:
            buf_copy = buffer.copy_deep()
            self._appsrc.emit("push-buffer", buf_copy)

        return Gst.PadProbeReturn.OK

    def _on_event(self, pad, info, user_data):
        """Handle EOS — finalize any active recording."""
        event = info.get_event()
        if event.type == Gst.EventType.EOS:
            if self._recording:
                self._stop_recording()
        return Gst.PadProbeReturn.OK

    def _start_recording(self):
        """Start a new recording sub-pipeline."""
        self._segment_count += 1
        filename = f"{self._location}-{self._segment_count}.mp4"
        print(f"[EventRecorder] Recording started: {filename}")

        pipe_str = (
            "appsrc name=src format=time is-live=true ! "
            "videoconvert ! vah264enc ! h264parse ! "
            "mp4mux fragment-duration=1000 ! "
            f'filesink location="{filename}"'
        )

        self._rec_pipeline = Gst.parse_launch(pipe_str)
        self._appsrc = self._rec_pipeline.get_by_name("src")

        if self._current_caps:
            self._appsrc.set_property("caps", self._current_caps)

        self._rec_pipeline.set_state(Gst.State.PLAYING)
        self._recording = True

    def _stop_recording(self):
        """Stop the current recording sub-pipeline."""
        if self._appsrc:
            self._appsrc.emit("end-of-stream")

        if self._rec_pipeline:
            bus = self._rec_pipeline.get_bus()
            # Wait for EOS to propagate (up to 5 seconds)
            bus.timed_pop_filtered(5 * Gst.SECOND, Gst.MessageType.EOS | Gst.MessageType.ERROR)
            self._rec_pipeline.set_state(Gst.State.NULL)

        self._rec_pipeline = None
        self._appsrc = None
        self._recording = False
        print(f"[EventRecorder] Recording stopped: {self._location}-{self._segment_count}.mp4")


GObject.type_register(EventRecorder)
__gstelementfactory__ = ("gvaeventrecorder_py", Gst.Rank.NONE, EventRecorder)
