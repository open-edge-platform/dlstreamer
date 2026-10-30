# ==============================================================================
# Copyright (C) 2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================

"""
This sample shows how an application reads pipeline latency from the bus.

latency_tracer is started with post-metrics=true, so it posts a "dlstreamer/latency-window"
element message for every interval window of every source-to-sink branch. The application
reads the typed fields of each message, prints the window's frame latency (mean, p95, max)
and frame rate, and merges all windows into a summary for the whole run when the stream ends.

Usage: python3 latency_metrics.py ["<pipeline description>"]
"""

import math
import os
import sys

# Tracers are created by Gst.init(), so they have to be requested before it runs.
os.environ.setdefault("GST_TRACERS", "latency_tracer(flags=pipeline,interval=1000,post-metrics=true)")

import gi  # pylint: disable=wrong-import-position

gi.require_version("Gst", "1.0")
from gi.repository import GLib, Gst  # pylint: disable=no-name-in-module, wrong-import-position

WINDOW_MESSAGE = "dlstreamer/latency-window"
SUPPORTED_VERSION = 1

DEFAULT_PIPELINE = (
    "videotestsrc num-buffers=300 ! video/x-raw,width=1280,height=720,framerate=30/1 ! "
    "videoscale ! video/x-raw,width=320,height=240 ! fakesink sync=true"
)


def percentile(bounds, counts, q, min_ms, max_ms):
    """Estimates the q-quantile of the frame latency from histogram buckets, within one bucket's width."""
    rank = q * sum(counts)
    seen = 0
    for i, count in enumerate(counts):
        if count and seen + count >= rank:
            lower = min_ms if i == 0 else max(bounds[i - 1], min_ms)
            upper = max_ms if i == len(bounds) else min(bounds[i], max_ms)
            return lower + (upper - lower) * (rank - seen) / count
        seen += count
    return max_ms


class LatencyTotals:
    """Totals of one branch. Sums, counts and bucket counts merge exactly; averages of averages would not."""

    def __init__(self):
        self.count = 0
        self.sum_ms = 0.0
        self.sum_sq_ms = 0.0
        self.window_ms = 0.0
        self.min_ms = float("inf")
        self.max_ms = 0.0
        self.histogram = None  # (bucket bounds, merged bucket counts)

    def add(self, window):
        """Adds one dlstreamer/latency-window message."""
        self.count += window.get_value("count")
        self.sum_ms += window.get_value("sum_ms")
        self.sum_sq_ms += window.get_value("sum_sq_ms")
        self.window_ms += window.get_value("window_ms")
        self.min_ms = min(self.min_ms, window.get_value("min_ms"))
        self.max_ms = max(self.max_ms, window.get_value("max_ms"))
        counts = list(window.get_value("bucket_counts"))
        if self.histogram is None:
            self.histogram = (list(window.get_value("bucket_bounds_ms")), counts)
        else:
            self.histogram = (self.histogram[0], [a + b for a, b in zip(self.histogram[1], counts)])

    def summary(self):
        """Frame latency statistics and frame rate over all windows added so far."""
        mean = self.sum_ms / self.count
        stddev = math.sqrt(max(0.0, self.sum_sq_ms / self.count - mean * mean))
        p50, p95, p99 = (percentile(*self.histogram, q, self.min_ms, self.max_ms) for q in (0.5, 0.95, 0.99))
        return (
            f"{self.count} frames, latency mean {mean:.2f} ms, stddev {stddev:.2f}, "
            f"p50 {p50:.2f}, p95 {p95:.2f}, p99 {p99:.2f}, min {self.min_ms:.2f}, max {self.max_ms:.2f}; "
            f"{1000.0 * self.count / self.window_ms:.1f} fps"
        )


def on_message(_bus, message, loop, totals):
    """Bus watch: handles latency windows, stops the main loop at EOS or on error."""
    if message.type == Gst.MessageType.ELEMENT:
        window = message.get_structure()
        if window.get_name() != WINDOW_MESSAGE:
            return True
        if window.get_value("version") != SUPPORTED_VERSION:
            print(f"Skipping {WINDOW_MESSAGE} version {window.get_value('version')}")
            return True
        branch = (window.get_value("source"), window.get_value("sink"))
        count = window.get_value("count")
        p95 = percentile(
            list(window.get_value("bucket_bounds_ms")),
            list(window.get_value("bucket_counts")),
            0.95,
            window.get_value("min_ms"),
            window.get_value("max_ms"),
        )
        print(
            f"{branch[0]} -> {branch[1]}: {count} frames in {window.get_value('window_ms'):.0f} ms, "
            f"latency mean {window.get_value('sum_ms') / count:.2f} ms, p95 {p95:.2f}, "
            f"max {window.get_value('max_ms'):.2f}; {1000.0 * count / window.get_value('window_ms'):.1f} fps"
        )
        totals.setdefault(branch, LatencyTotals()).add(window)
    elif message.type == Gst.MessageType.ERROR:
        err, debug = message.parse_error()
        print(f"Error: {err.message} ({debug})")
        loop.quit()
    elif message.type == Gst.MessageType.EOS:
        loop.quit()
    return True


def main():
    """Runs the pipeline and prints its latency windows and a summary per branch."""
    Gst.init(None)
    pipeline = Gst.parse_launch(sys.argv[1] if len(sys.argv) > 1 else DEFAULT_PIPELINE)
    loop = GLib.MainLoop()
    totals = {}

    bus = pipeline.get_bus()
    bus.add_signal_watch()
    bus.connect("message", on_message, loop, totals)

    pipeline.set_state(Gst.State.PLAYING)
    try:
        loop.run()
    finally:
        pipeline.set_state(Gst.State.NULL)
        bus.remove_signal_watch()

    for (source, sink), branch in totals.items():
        print(f"Total {source} -> {sink}: {branch.summary()}")
    if not totals:
        print("No latency windows were posted. Is latency_tracer with post-metrics=true in GST_TRACERS?")


if __name__ == "__main__":
    main()
