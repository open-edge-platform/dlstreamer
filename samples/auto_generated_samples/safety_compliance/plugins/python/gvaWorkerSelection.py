# ==============================================================================
# Copyright (C) 2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================
"""
Custom GStreamer element: worker selection for safety compliance VLM checks.

Selects frames where a new worker (person) appears or where a tracked worker
has not been checked for a configurable duration (default 30 seconds).
Passes exactly one worker per selected frame, tagging it for downstream crop.
"""

from dataclasses import dataclass, field

import gi

gi.require_version("Gst", "1.0")
gi.require_version("GstBase", "1.0")
gi.require_version("GstAnalytics", "1.0")
from gi.repository import Gst, GstBase, GObject, GLib, GstAnalytics

Gst.init_python()

GST_BASE_TRANSFORM_FLOW_DROPPED = Gst.FlowReturn.CUSTOM_SUCCESS


@dataclass
class TrackedWorker:
    tracking_id: int
    first_seen_ms: int = 0
    last_checked_ms: int = -1
    check_count: int = 0


class WorkerSelection(GstBase.BaseTransform):
    """Select workers needing VLM safety compliance checks."""

    __gstmetadata__ = (
        "GVA Worker Selection",
        "Transform",
        "Selects workers for VLM safety compliance checks based on tracking",
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

    _recheck_interval = 30000  # milliseconds between rechecks

    @GObject.Property(type=int, nick="recheck-interval",
                      blurb="Milliseconds between VLM rechecks for a tracked worker")
    def recheck_interval(self):
        return self._recheck_interval

    @recheck_interval.setter
    def recheck_interval(self, value):
        self._recheck_interval = value

    def __init__(self):
        super().__init__()
        self._tracked_workers: dict[int, TrackedWorker] = {}
        self._selected_bbox = None  # (x, y, w, h) of the worker to crop

    def do_transform_ip(self, buffer):
        _, state, _ = self.get_state(0)
        if state != Gst.State.PLAYING:
            return Gst.FlowReturn.OK

        rmeta = GstAnalytics.buffer_get_analytics_relation_meta(buffer)
        if not rmeta:
            return GST_BASE_TRANSFORM_FLOW_DROPPED

        ts_ms = buffer.pts // Gst.MSECOND if buffer.pts != Gst.CLOCK_TIME_NONE else 0

        # Collect all person detections with tracking IDs
        candidates = []
        for mtd in rmeta:
            if not isinstance(mtd, GstAnalytics.ODMtd):
                continue
            label = GLib.quark_to_string(mtd.get_obj_type())
            if label != "person":
                continue

            # Find associated tracking metadata
            tracking_id = None
            for other in rmeta:
                if not isinstance(other, GstAnalytics.TrackingMtd):
                    continue
                if rmeta.get_relation(mtd.id, other.id) != GstAnalytics.RelTypes.RELATE_TO:
                    continue
                success, tid, _, _, _ = other.get_info()
                if success:
                    tracking_id = tid
                    break

            if tracking_id is None:
                continue

            _, x, y, w, h, _ = mtd.get_location()
            candidates.append((tracking_id, x, y, w, h))

        if not candidates:
            return GST_BASE_TRANSFORM_FLOW_DROPPED

        # Update tracking state and find worker needing check
        selected = None
        for tracking_id, x, y, w, h in candidates:
            if tracking_id not in self._tracked_workers:
                # New worker — needs immediate check
                worker = TrackedWorker(tracking_id=tracking_id, first_seen_ms=ts_ms)
                self._tracked_workers[tracking_id] = worker
                if selected is None:
                    selected = (tracking_id, x, y, w, h)
            else:
                worker = self._tracked_workers[tracking_id]
                # Check if recheck interval has elapsed
                if worker.last_checked_ms < 0:
                    # Never checked — new worker
                    if selected is None:
                        selected = (tracking_id, x, y, w, h)
                elif (ts_ms - worker.last_checked_ms) >= self._recheck_interval:
                    if selected is None:
                        selected = (tracking_id, x, y, w, h)

        if selected is None:
            # Cleanup old entries
            for tid in list(self._tracked_workers.keys()):
                w = self._tracked_workers[tid]
                if (ts_ms - w.first_seen_ms) > self._recheck_interval * 10:
                    active = any(t == tid for t, _, _, _, _ in candidates)
                    if not active:
                        del self._tracked_workers[tid]
            return GST_BASE_TRANSFORM_FLOW_DROPPED

        tracking_id, x, y, w, h = selected
        self._tracked_workers[tracking_id].last_checked_ms = ts_ms
        self._tracked_workers[tracking_id].check_count += 1

        # Tag the selected worker bbox as overlay metadata for downstream crop element
        # Store bbox coordinates in a special quark-tagged ODMtd entry
        tag = f"__crop__:{x},{y},{w},{h}"
        rmeta.add_od_mtd(GLib.quark_from_string(tag), x, y, w, h, 1.0)

        return Gst.FlowReturn.OK


GObject.type_register(WorkerSelection)
__gstelementfactory__ = ("gvaworkerselection_py", Gst.Rank.NONE, WorkerSelection)
