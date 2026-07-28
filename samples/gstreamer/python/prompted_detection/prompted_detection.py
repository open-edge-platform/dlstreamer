# ==============================================================================
# Copyright (C) 2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================

import sys
import os

from ultralytics import YOLO
import gi
gi.require_version("Gst", "1.0")
gi.require_version("GstAnalytics", "1.0")
from gi.repository import GLib, Gst, GstAnalytics # pylint: disable=no-name-in-module, wrong-import-position

# Pinned weights and export precision to keep deterministic outputs.
WEIGHTS = "yoloe-26s-seg"
OBJECT_TO_FIND = "dog"


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
def on_new_sample(sink, user_data):
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
                    print(f"Detected {category} in frame at {buffer.pts}")
        return Gst.FlowReturn.OK

    return Gst.FlowReturn.Flushing

# download PyTorch model, convert to OpenVINO IR, create and run gstreamer pipeline
def main(args):
    # Check input arguments
    if len(args) < 3 or len(args) > 5:
        sys.stderr.write(f"usage: {args[0]} <LOCAL_VIDEO_FILE> <OBJECT_TO_FIND> [DEVICE] [OUTPUT]\n")
        sys.stderr.write("  OBJECT_TO_FIND - object to detect (e.g. 'dog', 'white car')\n")
        sys.stderr.write("  DEVICE         - inference device: CPU, GPU, or NPU (default: GPU)\n")
        sys.stderr.write("  OUTPUT         - output mode: appsink, json, or file (default: appsink)\n")
        sys.exit(1)

    if not os.path.isfile(args[1]):
        sys.stderr.write("Input video file does not exist\n")
        sys.exit(1)

    video_file = args[1]
    object_to_find = args[2]
    device = args[3] if len(args) > 3 and args[3] else "GPU"
    output = args[4] if len(args) > 4 and args[4] else "appsink"

    # Configure YOLO-E model with requested detection prompt, and export to OpenVINO format
    model = YOLO(WEIGHTS + ".pt")
    names = [object_to_find]
    model.set_classes(names, model.get_text_pe(names))
    exported_model_path = model.export(format="openvino", dynamic=True, half=True)
    model_file = f"{exported_model_path}/{WEIGHTS}.xml"

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
        appsink.connect("new-sample", on_new_sample, None)

    # execute gstreamer pipeline
    pipeline_loop(pipeline)

if __name__ == '__main__':
    sys.exit(main(sys.argv))