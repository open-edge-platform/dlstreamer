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
from gi.repository import GLib, Gst, GstAnalytics, GObject
from ultralytics import YOLO

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

# download PyTorch model, convert to OpenVINO IR, create and run gstreamer pipeline
def main(video_file):
    # Download yolo26 detection model from Ultralytics hub and convert to OpenVINO IR
    ov_model_path = os.path.join(os.getcwd(), "yolo26s_openvino_model/yolo26s.xml")
    if not os.path.isfile(ov_model_path):
        print(f"Downloading and converting YOLO26 model")
        model = YOLO("yolo26s.pt")
        exported_model_path = model.export(format="openvino", dynamic=True, half=True)
        if exported_model_path != ov_model_path:
            print(f"YOLO26 model exported to {exported_model_path}")
    
    # Create GStreamer pipeline and parametrize with downloaded models and video files
    pipeline = Gst.parse_launch(
        f"filesrc location={video_file} ! decodebin3 ! "
        f"gvadetect model={ov_model_path} ! gvaanalytics_py ! "
        f"gvarecorder_py fileprefix=output"
    )

    # execute gstreamer pipeline
    pipeline_loop(pipeline)

if __name__ == '__main__':
    # check input arguments
    if len(sys.argv) != 2:
        sys.stderr.write(f"usage: {sys.argv[0]} <LOCAL_VIDEO_FILE>\n")
        sys.exit(1)

    if not os.path.isfile(sys.argv[1]):
        sys.stderr.write("Input video file does not exist\n")
        sys.exit(1)

    # check if GST_PLUGIN_PATH includes path to local python elements, if not add it to the environment variable
    if f"{os.getcwd()}/plugins" not in os.environ.get("GST_PLUGIN_PATH", ""):
        print(f"Adding \"{os.getcwd()}/plugins\" path to GST_PLUGIN_PATH environment variable")
        os.environ["GST_PLUGIN_PATH"] = f"{os.environ.get('GST_PLUGIN_PATH', '')}:{os.getcwd()}/plugins"

    # Initialize Gst library, python plgin (if available) will load local python elements
    Gst.init(None)
    reg = Gst.Registry.get()
    if not reg.find_plugin("python"):
        print("GStreamer python plugin not found in registry, check GST_PLUGIN_PATH environment variable")
        sys.exit(1)

    sys.exit(main(sys.argv[1]))