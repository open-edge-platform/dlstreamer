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

# Pinned weights, export precision and detection prompt keep the generated
# ground-truth output deterministic.
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
    if len(args) < 2 or len(args) > 4:
        sys.stderr.write(f"usage: {args[0]} <LOCAL_VIDEO_FILE> [DEVICE] [OUTPUT]\n")
        sys.stderr.write("  DEVICE - inference device: CPU or GPU (default: GPU)\n")
        sys.stderr.write("  OUTPUT - output mode: appsink or json (default: appsink)\n")
        sys.exit(1)

    if not os.path.isfile(args[1]):
        sys.stderr.write("Input video file does not exist\n")
        sys.exit(1)

    video_file = args[1]
    device = args[2] if len(args) > 2 and args[2] else "GPU"
    output = args[3] if len(args) > 3 and args[3] else "appsink"

    # Configure YOLO-E model with pinned detection prompt, and export to OpenVINO format
    model = YOLO(WEIGHTS + ".pt")
    names = [OBJECT_TO_FIND]
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