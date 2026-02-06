# ==============================================================================
# Copyright (C) 2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================

import sys
import os
import urllib.request
from pathlib import Path
import gi
gi.require_version("Gst", "1.0")
gi.require_version("GstAnalytics", "1.0")
from gi.repository import GLib, Gst, GstAnalytics
from huggingface_hub import hf_hub_download
from ultralytics import YOLO

from optimum.intel.openvino import OVModelForImageClassification
from transformers import AutoImageProcessor

# Detection model from Hugging Face Model Hub
detection_model_id = "arnabdhar/YOLOv8-Face-Detection"
detection_model_src_filename = "model.pt"

# Classification model from Hugging Face Model Hub
classification_model_id = "dima806/fairface_age_image_detection"

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

# Download PyTorch models, convert to OpenVINO IR, create and run gstreamer pipeline
def main(args):
    # Check input arguments
    if len(args) > 2:
        sys.stderr.write(f"usage: {args[0]} [LOCAL_VIDEO_FILE]\n")
        sys.exit(1)

    current_dir = os.path.dirname(os.path.abspath(__file__))


    # Prepare input video file; download default if none provided
    if len(args) == 2:
        input_video = args[1]
        if not os.path.isfile(input_video):
            sys.stderr.write("Input video file does not exist\n")
            sys.exit(1)
    else:
        default_video_url = "https://videos.pexels.com/video-files/18553046/18553046-hd_1280_720_30fps.mp4"
        input_video = os.path.join(current_dir, "default_input.mp4")
        if not os.path.isfile(input_video):
            print("No input provided. Downloading default video...")
            request = urllib.request.Request(
                default_video_url,
                headers={"User-Agent": "Mozilla/5.0"},
            )
            with urllib.request.urlopen(request) as response, open(input_video, "wb") as output:
                output.write(response.read())

    # STEP 1: Prepare face detection model (download + export to OpenVINO IR)
    
    detection_model_dst_filename = "detector.pt"
    detection_model_dst_name = "detector"
    ov_detection_model_path = os.path.join(current_dir, f"{detection_model_dst_name}_openvino_model/{detection_model_dst_name}.xml")
    
    if not os.path.isfile(ov_detection_model_path):
        print(f"Downloading {detection_model_id} model and converting to OpenVINO IR format...")
        model_path = hf_hub_download(repo_id=detection_model_id, filename=detection_model_src_filename, local_dir=current_dir)
        model_path = Path(model_path).replace(Path(current_dir) / detection_model_dst_filename)
        model = YOLO(str(model_path))
        exported_model_path = model.export(format="openvino", dynamic=True, half=True)
        print (f"Model exported to {exported_model_path}")
    
    # STEP 2: Prepare classification model (download + export to OpenVINO IR)
    
    ov_classification_dir = os.path.join(current_dir, "classificator_openvino_model")
    ov_classification_model_path = os.path.join(ov_classification_dir, "openvino_model.xml")
    
    
    if not os.path.isfile(ov_classification_model_path):
        print(f"Downloading {classification_model_id} model and converting to OpenVINO IR format...")
        ov_model = OVModelForImageClassification.from_pretrained(
            classification_model_id,
            export=True,
        )
        
        ov_model.save_pretrained(ov_classification_dir)
        image_processor = AutoImageProcessor.from_pretrained(classification_model_id)
        image_processor.save_pretrained(ov_classification_dir)

    # STEP 3: Build and run the DL Streamer GStreamer pipeline
    Gst.init(None)
    output_file = os.path.splitext(input_video)[0] + '_output.mp4'

    pipeline_string = (
        f"filesrc location={input_video} ! decodebin3 ! "
        f"gvadetect model={ov_detection_model_path} device=GPU batch-size=4 ! queue ! "
        f"gvaclassify model={ov_classification_model_path} device=GPU batch-size=4 ! queue ! "
        f"gvafpscounter ! gvawatermark displ-cfg=text-scale=0.5,draw-txt-bg=true ! "
        f"videoconvert ! vah264enc ! h264parse ! mp4mux ! "
        f"filesink location={output_file}"
    )

    pipeline = Gst.parse_launch(pipeline_string)
    print(f"\nPipeline string: \n{pipeline_string}\n")

    # Execute gstreamer pipeline
    pipeline_loop(pipeline)

if __name__ == '__main__':
    sys.exit(main(sys.argv))