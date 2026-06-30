# ==============================================================================
# Copyright (C) 2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================
import sys
import os
import subprocess

from huggingface_hub import hf_hub_download
from ultralytics import YOLO

sys.path.insert(
    0, os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
)
# pylint: disable-next=wrong-import-position
from shared_utils import download_https

import gi

gi.require_version("Gst", "1.0")
gi.require_version("GstAnalytics", "1.0")
# pylint: disable-next=no-name-in-module, wrong-import-position
from gi.repository import Gst

DEFAULT_VIDEO_URL = "https://videos.pexels.com/video-files/18553046/18553046-hd_1280_720_30fps.mp4"
YOLO_FACE_REPO_ID = "arnabdhar/YOLOv8-Face-Detection"
# Pinned model revisions and export precision keep the generated ground-truth output deterministic.
YOLO_FACE_REVISION = "52fa54977207fa4f021de949b515fb19dcab4488"
FAIRFACE_REPO_ID = "dima806/fairface_age_image_detection"
FAIRFACE_REVISION = "4e02ab8057ea7fd74b1670940995c5dfda3e6ec0"


def get_runtime_dir():
    return os.getcwd()


# Parse INPUT, DEVICE and OUTPUT positional arguments
def parse_args(args):
    if len(args) > 4:
        sys.stderr.write(f"usage: {args[0]} [INPUT] [DEVICE] [OUTPUT]\n")
        sys.stderr.write("  INPUT  - local video file (default: download sample video)\n")
        sys.stderr.write("  DEVICE - inference device: CPU or GPU (default: GPU)\n")
        sys.stderr.write("  OUTPUT - output mode: file or json (default: file)\n")
        sys.exit(1)
    input_arg = args[1] if len(args) > 1 and args[1] else None
    device = args[2] if len(args) > 2 and args[2] else "GPU"
    output = args[3] if len(args) > 3 and args[3] else "file"
    return input_arg, device, output


# Prepare input video file; download default if none provided
def prepare_input_video(input_arg):
    runtime_dir = get_runtime_dir()

    if input_arg:
        if not os.path.isfile(input_arg):
            sys.stderr.write("Input video file does not exist\n")
            sys.exit(1)
        return input_arg

    input_video = os.path.join(runtime_dir, "default_video.mp4")
    if not os.path.isfile(input_video):
        print("\nNo input provided. Downloading default video...\n")
        download_https(DEFAULT_VIDEO_URL, input_video, {"videos.pexels.com"})

    return input_video


# wrapper to run the gstreamer pipeline loop
def pipeline_loop(pipeline):
    print("\nStarting Pipeline \n")
    bus = pipeline.get_bus()
    pipeline.set_state(Gst.State.PLAYING)
    terminate = False
    while not terminate:
        msg = bus.timed_pop_filtered(
            Gst.CLOCK_TIME_NONE, Gst.MessageType.EOS | Gst.MessageType.ERROR
        )
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
def main(input_video, device, output):

    runtime_dir = get_runtime_dir()

    # STEP 1: Prepare face detection model (download + export to OpenVINO IR)

    # Detection model from Hugging Face Model Hub
    ov_detection_model_path = os.path.join(
        runtime_dir, "model_int8_openvino_model", "model.xml"
    )
    if not os.path.isfile(ov_detection_model_path):
        print(
            "\nDownloading the detection model and converting to OpenVINO IR format...\n"
        )
        model_path = hf_hub_download(
            repo_id=YOLO_FACE_REPO_ID,
            filename="model.pt",
            local_dir=runtime_dir,
            revision=YOLO_FACE_REVISION,
        )

        model = YOLO(str(model_path))
        exported_model_path = model.export(format="openvino", dynamic=True, int8=True)
        print(f"Model exported to {exported_model_path}\n")

    # STEP 2: Prepare classification model (download + export to OpenVINO IR)

    ov_classification_model_path = os.path.join(
        runtime_dir, "fairface_age_image_detection", "openvino_model.xml"
    )
    if not os.path.isfile(ov_classification_model_path):
        print(
            "\nDownloading classification model and converting to OpenVINO IR format...\n"
        )
        subprocess.run(
            [
                "optimum-cli",
                "export",
                "openvino",
                "--model",
                FAIRFACE_REPO_ID,
                "--revision",
                FAIRFACE_REVISION,
                os.path.join(runtime_dir, "fairface_age_image_detection"),
                "--weight-format",
                "int8",
            ],
            check=True,
        )
        print(f"Model exported to {ov_classification_model_path}\n")

    # STEP 3: Build and run the DL Streamer GStreamer pipeline

    Gst.init(None)

    if output == "json":
        # Deterministic json-lines output (used for ground-truth comparison)
        output_json = os.path.join(runtime_dir, "output.json")
        if os.path.isfile(output_json):
            os.remove(output_json)
        sink = (
            "gvametaconvert add-tensor-data=true ! "
            "gvametapublish file-format=json-lines file-path=output.json ! "
            "fakesink async=false"
        )
    else:
        output_file = os.path.splitext(input_video)[0] + "_output.mp4"
        sink = (
            "gvafpscounter ! gvawatermark ! "
            "videoconvert ! vah264enc ! h264parse ! mp4mux ! "
            f"filesink location={output_file}"
        )

    pipeline_string = (
        f"filesrc location={input_video} ! decodebin3 ! "
        f"gvadetect model={ov_detection_model_path} device={device} batch-size=4 ! queue ! "
        f"gvaclassify model={ov_classification_model_path} device={device} batch-size=4 ! queue ! "
        f"{sink}"
    )

    pipeline = Gst.parse_launch(pipeline_string)
    print(f"\nPipeline string: \n{pipeline_string}\n")

    # Execute gstreamer pipeline
    pipeline_loop(pipeline)


if __name__ == "__main__":
    input_argument, device_argument, output_argument = parse_args(sys.argv)
    video_path = prepare_input_video(input_argument)
    sys.exit(main(video_path, device_argument, output_argument))
