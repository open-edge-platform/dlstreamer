# ==============================================================================
# Copyright (C) 2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================
"""DLStreamer execution path: pipelined iGPU decode, zero-copy, async inference."""

import time
from pathlib import Path
from perf_comparison import (CONFIDENCE_THRESHOLD, INFERENCE_DEVICE, NIREQ,
                              QUEUE_SIZE, SNAPSHOT_FRAMES, PipelineError,
                              compute_throughput)

_Gst = None


def _gst():
    """GStreamer initialization"""
    global _Gst  # pylint: disable=global-statement
    if _Gst is None:
        import gi  # pylint: disable=import-outside-toplevel
        gi.require_version("Gst", "1.0")
        from gi.repository import Gst  # pylint: disable=import-outside-toplevel,no-name-in-module
        Gst.init(None)
        _Gst = Gst
    return _Gst


def _pipeline(model_xml: Path, video: Path, sink: str, *, num_buffers: int = 0) -> str:
    """Single pipeline builder shared by run() and save_snapshot()."""
    src = f"filesrc location={video}"
    if num_buffers:
        src += f" num-buffers={num_buffers}"
    return (f"{src} ! qtdemux ! h264parse ! vah264dec ! vapostproc "
            f"! video/x-raw(memory:VAMemory),format=NV12 "
            f"! gvadetect model={model_xml} device={INFERENCE_DEVICE} "
            f"pre-process-backend=va-surface-sharing nireq={NIREQ} "
            f"batch-size=1 threshold={CONFIDENCE_THRESHOLD} "
            f"! queue max-size-buffers={QUEUE_SIZE} ! {sink}")


def _run_pipeline(pipeline, timeout_sec: int = 60):
    """Play pipeline to EOS, raise on error."""
    Gst = _gst()
    pipeline.set_state(Gst.State.PLAYING)
    msg = pipeline.get_bus().timed_pop_filtered(
        timeout_sec * Gst.SECOND,
        Gst.MessageType.EOS | Gst.MessageType.ERROR)
    if msg and msg.type == Gst.MessageType.ERROR:
        err, dbg = msg.parse_error()
        pipeline.set_state(Gst.State.NULL)
        raise PipelineError(f"{err.message}\n{dbg}")
    pipeline.set_state(Gst.State.NULL)


def run(model_xml: Path, video: Path, num_frames: int, warmup: int):
    """Pipelined iGPU decode + zero-copy inference."""
    Gst = _gst()
    timestamps: list[float] = []
    n = [0]

    def _on_buffer(pad, _info, _data):
        n[0] += 1
        if n[0] <= warmup:
            return Gst.PadProbeReturn.OK
        timestamps.append(time.monotonic())
        if len(timestamps) >= num_frames:
            pad.get_parent_element().get_parent().send_event(Gst.Event.new_eos())
        return Gst.PadProbeReturn.OK

    pipe = Gst.parse_launch(
        _pipeline(model_xml, video, "identity name=tap ! fakesink async=false sync=false"))
    tap = pipe.get_by_name("tap")
    if not tap:
        raise PipelineError("Failed to construct pipeline")
    tap.get_static_pad("src").add_probe(Gst.PadProbeType.BUFFER, _on_buffer, None)
    _run_pipeline(pipe)
    return compute_throughput(timestamps[:num_frames])


def save_snapshot(model_xml: Path, video: Path, out_path: Path):
    """Save first annotated detection frame via gvawatermark through GStreamer."""
    Gst = _gst()
    sink = (f"vapostproc ! gvawatermark ! videoconvert "
            f"! jpegenc snapshot=true ! filesink location={out_path}")
    pipe = Gst.parse_launch(_pipeline(model_xml, video, sink, num_buffers=SNAPSHOT_FRAMES))
    _run_pipeline(pipe)
