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
from gi.repository import GLib, Gst, GstAnalytics
from huggingface_hub import hf_hub_download
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
def main(args):
    # Check input arguments
    if len(args) != 2:
        sys.stderr.write(f"usage: {args[0]} <LOCAL_VIDEO_FILE>\n")
        sys.exit(1)

    if not os.path.isfile(args[1]):
        sys.stderr.write("Input video file does not exist\n")
        sys.exit(1)

    # Download YOLOv8 face detection model from Hugging Face Model Hub and convert to OpenVINO IR
    current_dir = os.getcwd()
    ov_model_path = os.path.join(current_dir, "model_openvino_model/model.xml")
    if not os.path.isfile(ov_model_path):
        print("Downloading YOLOv8 model and converting to OpenVINO IR format...")
        model_path = hf_hub_download(repo_id="arnabdhar/YOLOv8-Face-Detection", filename="model.pt", local_dir=current_dir)
        model = YOLO(model_path)
        exported_model_path = model.export(format="openvino", dynamic=True, half=True)
        print (f"Model exported to {exported_model_path}")
    
    # TODO - download classification model and convert to OpenVINO IR

    # Create GStreamer pipeline and parametrize with downloaded models and video files
    Gst.init(None)
    output_file = os.path.splitext(args[1])[0] + '_output.mp4'
    pipeline = Gst.parse_launch(
            f"filesrc location={args[1]} ! decodebin3 ! "
            f"gvadetect model={ov_model_path} device=GPU batch-size=4 ! queue ! "
            f"gvafpscounter ! gvawatermark ! "
            f"videoconvert ! vah264enc ! h264parse ! mp4mux ! "
            f"filesink location={output_file}"
        )

    # execute gstreamer pipeline
    pipeline_loop(pipeline)

if __name__ == '__main__':
    sys.exit(main(sys.argv))