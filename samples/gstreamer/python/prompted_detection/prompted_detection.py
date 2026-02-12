# ==============================================================================
# Copyright (C) 2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================

import sys
import os
import gi
gi.require_version("Gst", "1.0")
gi.require_version("GstAnalytics", "1.0")
# pylint: disable=no-name-in-module
from gi.repository import GLib, Gst, GstAnalytics
# pylint: enable=no-name-in-module
from ultralytics import YOLO # pylint: disable=import-error


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
    if len(args) != 3:
        sys.stderr.write(f"usage: {args[0]} <LOCAL_VIDEO_FILE> <OBJECT_TO_FIND>\n")
        sys.exit(1)

    if not os.path.isfile(args[1]):
        sys.stderr.write("Input video file does not exist\n")
        sys.exit(1)

    # Configure YOLO-E model with requested classes, and export to OpenVINO format
    weights = "yoloe-26s-seg"
    model = YOLO(weights+".pt")
    names = [args[2]]
    model.set_classes(names, model.get_text_pe(names))
    exported_model_path = model.export(format="openvino", dynamic=True, half=True)
    model_file = f"{exported_model_path}/{weights}.xml"

    # Create GStreamer pipeline, pass input video file and OpenVINO model file
    Gst.init(None)
    pipeline = Gst.parse_launch(
            f"filesrc location={args[1]} ! decodebin3 ! "
            f"gvadetect model={model_file} device=GPU batch-size=4 ! queue ! "
            f"appsink emit-signals=true name=appsink0"
        )

    # register user-defined callback function to process results
    appsink = pipeline.get_by_name("appsink0")
    appsink.connect("new-sample", on_new_sample, None)

    # execute gstreamer pipeline
    pipeline_loop(pipeline)

if __name__ == '__main__':
    sys.exit(main(sys.argv))