# ==============================================================================
# Copyright (C) 2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================
"""DLStreamer execution path.

Pipelined iGPU decode + zero-copy VA surface sharing + async inference.
"""

import time

import cv2

from common import (CONFIDENCE_THRESHOLD, INFERENCE_DEVICE, NIREQ, QUEUE_SIZE,
                     SNAPSHOT_FRAMES, PipelineError, compute_result, stamp_frame)


def _init_gst():
    """Lazy GStreamer init to avoid conflict with OpenVINO GPU context."""
    import gi  # pylint: disable=import-outside-toplevel
    gi.require_version("Gst", "1.0")
    from gi.repository import Gst  # pylint: disable=import-outside-toplevel,no-name-in-module
    Gst.init(None)
    return Gst


def _build_pipeline_str(model_xml, video):
    """GStreamer pipeline string for benchmarking."""
    return (
        f"filesrc location={video} "
        f"! qtdemux ! h264parse "
        f"! vah264dec "
        f"! video/x-raw(memory:VAMemory),format=NV12 "
        f"! gvadetect model={model_xml} device={INFERENCE_DEVICE} "
        f"pre-process-backend=va-surface-sharing nireq={NIREQ} batch-size=1 "
        f"threshold={CONFIDENCE_THRESHOLD} "
        f"! queue max-size-buffers={QUEUE_SIZE} "
        f"! identity name=tap "
        f"! fakesink async=false sync=false")


def run(model_xml, video, num_frames, warmup):
    """Pipelined iGPU decode + zero-copy inference."""
    Gst = _init_gst()
    total = warmup + num_frames
    timestamps = []
    counter = [0]

    def on_buffer(pad, _info, _user_data):
        counter[0] += 1
        if counter[0] > warmup:
            timestamps.append(time.monotonic())
        if counter[0] >= total:
            pad.get_parent_element().get_parent().send_event(Gst.Event.new_eos())
        return Gst.PadProbeReturn.OK

    pipeline = Gst.parse_launch(_build_pipeline_str(model_xml, video))
    tap = pipeline.get_by_name("tap")
    if tap is None:
        raise PipelineError("Failed to construct DLStreamer pipeline")
    tap.get_static_pad("src").add_probe(Gst.PadProbeType.BUFFER, on_buffer, None)

    pipeline.set_state(Gst.State.PLAYING)
    while True:
        msg = pipeline.get_bus().timed_pop_filtered(
            Gst.CLOCK_TIME_NONE, Gst.MessageType.EOS | Gst.MessageType.ERROR)
        if not msg:
            continue
        if msg.type == Gst.MessageType.ERROR:
            err, dbg = msg.parse_error()
            pipeline.set_state(Gst.State.NULL)
            raise PipelineError(f"GStreamer error: {err.message}\n{dbg}")
        break
    pipeline.set_state(Gst.State.NULL)

    return compute_result(timestamps)


def save_snapshot(model_xml, video, out_path, e2e_ms):
    """Save one watermarked detection frame."""
    Gst = _init_gst()
    pipe = Gst.parse_launch(
        f"filesrc location={video} num-buffers={SNAPSHOT_FRAMES} "
        f"! qtdemux ! h264parse ! vah264dec ! vapostproc "
        f"! video/x-raw(memory:VAMemory),format=NV12 "
        f"! gvadetect model={model_xml} device={INFERENCE_DEVICE} "
        f"pre-process-backend=va-surface-sharing nireq={NIREQ} batch-size=1 "
        f"threshold={CONFIDENCE_THRESHOLD} "
        f"! queue ! vapostproc ! gvawatermark ! videoconvert "
        f"! jpegenc snapshot=true ! filesink location={out_path}")
    pipe.set_state(Gst.State.PLAYING)
    pipe.get_bus().timed_pop_filtered(
        30 * Gst.SECOND, Gst.MessageType.EOS | Gst.MessageType.ERROR)
    pipe.set_state(Gst.State.NULL)

    if out_path.exists():
        img = cv2.imread(str(out_path))
        if img is not None:
            stamp_frame(img, f"E2E via DLStreamer: {e2e_ms:.1f} ms")
            cv2.imwrite(str(out_path), img)
