# ==============================================================================
# Copyright (C) 2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================

import argparse
import sys
import os

import gi
gi.require_version("Gst", "1.0")
gi.require_version("GstAnalytics", "1.0")
from gi.repository import GLib, Gst, GstAnalytics # pylint: disable=no-name-in-module, wrong-import-position

# Prompt-free model variant has a built-in 4,585-class LVIS vocabulary
WEIGHTS = "yoloe-26s-seg-pf"
# Default model location relative to $MODELS_PATH
DEFAULT_MODEL_REL = f"public/{WEIGHTS}/FP16/{WEIGHTS}.xml"


def ensure_file(path, description):
    """Return resolved path if file exists; exit with error otherwise."""
    if not os.path.isfile(path):
        sys.stderr.write(f"Error: {description} not found: {path}\n")
        sys.stderr.write("Set MODELS_PATH or prepare the model with download_ultralytics_models.py\n")
        sys.exit(1)
    return os.path.abspath(path)


# wrapper to run the gstreamer pipeline loop
def pipeline_loop(pipeline):
    print("Starting Pipeline \n")
    bus = pipeline.get_bus()
    pipeline.set_state(Gst.State.PLAYING)
    terminate = False
    while not terminate:
        msg = bus.timed_pop_filtered(Gst.CLOCK_TIME_NONE, Gst.MessageType.EOS | Gst.MessageType.ERROR)
        if msg:
            if msg.type == Gst.MessageType.ERROR:
                _, debug_info = msg.parse_error()
                print(f"Error received from element {msg.src.get_name()}")
                print(f"Debug info: {debug_info}")
                terminate = True
            if msg.type == Gst.MessageType.EOS:
                print("Pipeline complete.")
                terminate = True
    pipeline.set_state(Gst.State.NULL)

# called for each new frame received by appsink
# implements user-defined processing of detection results
def on_new_sample(sink, object_to_find):
    sample = sink.emit('pull-sample')
    if sample:
        # get analytics metadata attached to frame buffer
        buffer = sample.get_buffer()
        rmeta = GstAnalytics.buffer_get_analytics_relation_meta(buffer)
        # check if any objects were detected in the frame
        if rmeta:
            for mtd in rmeta:
                if type(mtd) == GstAnalytics.ODMtd:
                    category = GLib.quark_to_string(mtd.get_obj_type())
                    if object_to_find.lower() in category.lower():
                        print(f"Detected {category} in frame at {buffer.pts}")
        return Gst.FlowReturn.OK

    return Gst.FlowReturn.Flushing

# create and run gstreamer pipeline
def main(args):
    p = argparse.ArgumentParser(description="Prompt-based object detection")
    p.add_argument("--input", required=True, help="Path to input video file")
    p.add_argument("--prompt", required=True, help="Object to detect (e.g. 'dog', 'white car')")
    p.add_argument("--device", default="GPU", choices=["CPU", "GPU", "NPU"],
                   help="Inference device (default: GPU)")
    p.add_argument("--output", default="appsink", choices=["appsink", "json", "file"],
                   help="Output mode (default: appsink)")
    p.add_argument("--model", default=None,
                   help="Path to detection model .xml (default: $MODELS_PATH/" + DEFAULT_MODEL_REL + ")")
    parsed = p.parse_args(args[1:])

    video_file = ensure_file(parsed.input, "input video")
    object_to_find = parsed.prompt
    device = parsed.device
    output = parsed.output

    model_path = parsed.model or os.path.join(os.environ.get("MODELS_PATH", "./models"), DEFAULT_MODEL_REL)
    model_file = ensure_file(model_path, "detection model")

    if output == "json":
        # Deterministic json-lines output (used for ground-truth comparison)
        output_json = os.path.join(os.getcwd(), "output.json")
        if os.path.isfile(output_json):
            os.remove(output_json)
        sink = (
            "gvametaconvert add-tensor-data=true ! "
            "gvametapublish file-format=json-lines file-path=output.json ! "
            "fakesink async=false"
        )
    elif output == "file":
        output_file = os.path.splitext(os.path.basename(video_file))[0] + "_output.mp4"
        sink = (
            "gvawatermark ! videoconvert ! "
            f"vah264enc ! h264parse ! mp4mux ! filesink location={output_file}"
        )
    else:
        sink = "appsink emit-signals=true name=appsink0"

    # Create GStreamer pipeline, pass input video file and OpenVINO model file
    Gst.init([])
    pipeline = Gst.parse_launch(
            f"filesrc location={video_file} ! decodebin3 ! "
            f"gvadetect model={model_file} device={device} batch-size=4 ! queue ! "
            f"{sink}"
        )

    # register user-defined callback function to process results (appsink demo mode)
    appsink = pipeline.get_by_name("appsink0")
    if appsink is not None:
        appsink.connect("new-sample", on_new_sample, object_to_find)

    # execute gstreamer pipeline
    pipeline_loop(pipeline)

if __name__ == '__main__':
    sys.exit(main(sys.argv))