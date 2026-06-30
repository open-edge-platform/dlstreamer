# ==============================================================================
# Copyright (C) 2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================

"""
This sample application demonstrates how to add custom Python elements to DLStreamer pipeline.
- gvaanalytics_py analyzes bounding-box detection results and identifies cars
  hogging lane in a predefined inspection zone.
- gvarecorder_py splits the video stream into N-second file chunks and stores
  custom detection metadata along with each chunk.
"""

import os
import subprocess
import sys

import gi

gi.require_version("Gst", "1.0")
from gi.repository import Gst   # pylint: disable=no-name-in-module,wrong-import-order,wrong-import-position

sys.path.insert(
    0, os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
)
from shared_utils import download_https   # pylint: disable=wrong-import-position

DEFAULT_VIDEO_URL = "https://videos.pexels.com/video-files/2431853/2431853-hd_1920_1080_25fps.mp4"
# Pinned model revision and a fixed stream length keep the generated ground-truth
# output (detections and number of recorded chunks) deterministic.
RTDETR_REPO_ID = "PekingU/rtdetr_v2_r50vd"
RTDETR_REVISION = "282494075698cab9faa1096ae26856890030c817"
NUM_FRAMES = 500


def pipeline_loop(gst_pipeline):
    """Wrapper to run the gstreamer pipeline loop"""
    print("Starting Pipeline \n")
    bus = gst_pipeline.get_bus()
    gst_pipeline.set_state(Gst.State.PLAYING)
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
    gst_pipeline.set_state(Gst.State.NULL)

def check_download_video_file(input_arg=None):
    """Use the provided input file or download the default video if none is given."""
    if input_arg:
        if not os.path.isfile(input_arg):
            print(f"Input video file does not exist: {input_arg}")
            sys.exit(1)
        return input_arg

    input_video = os.path.join(os.getcwd(), "2431853-hd_1920_1080_25fps.mp4")

    # download if local copy does not exist
    if not os.path.isfile(input_video):
        print("\nNo input provided. Downloading default video...\n")
        download_https(DEFAULT_VIDEO_URL, input_video, {"videos.pexels.com"})

    return input_video

def check_download_detection_model():
    """Check if the default detection model exists locally, if not, download it."""
    ov_model_path = os.path.join(os.getcwd(), "rtdetr_v2_r50vd/model.xml")

    # download RTDETRv2 model from Hugging Face Model Hub if local copy does not exist
    if not os.path.isfile(ov_model_path):
        print("Downloading PekingU/rtdetr_v2_r50vd from HuggingFace\n")
        subprocess.run(["optimum-cli", "export", "onnx", "--model", RTDETR_REPO_ID, "--revision", RTDETR_REVISION,
                        "--task", "object-detection", "--opset", "18", "--width", "640", "--height", "640", "rtdetr_v2_r50vd",
            ],check=True)
        os.chdir("rtdetr_v2_r50vd")
        subprocess.run(["hf", "download", RTDETR_REPO_ID, "--revision", RTDETR_REVISION, "--include", "preprocessor_config.json", "--local-dir", "."], check=True)
        subprocess.run(["ovc", "model.onnx"], check=True)
        os.chdir("..")
        print(f"Model exported to OpenVINO IR format at: {ov_model_path}\n")

    return ov_model_path

def parse_args(args):
    """Parse INPUT, DEVICE and OUTPUT positional arguments."""
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

def build_pipeline(video_file, detection_model, device, output):
    """Construct the GStreamer pipeline string for the given output mode."""
    analytics_branch = "gvaanalytics_py distance=500 angle=-135,-45"
    if output == "json":
        # Capture detection + lane-hogging analytics metadata as deterministic json-lines
        output_json = os.path.join(os.getcwd(), "output.json")
        if os.path.isfile(output_json):
            os.remove(output_json)
        analytics_branch += " ! gvametaconvert ! gvametapublish file-format=json-lines file-path=output.json"

    return (
        f"filesrc location={video_file} ! decodebin3 ! identity eos-after={NUM_FRAMES} ! "
        f"gvadetect model={detection_model} device={device} batch-size=4 threshold=0.7 ! queue ! "
        f"{analytics_branch} ! gvafpscounter ! gvawatermark ! "
        f"gvarecorder_py location=output.mp4 max-time=10"
    )

if __name__ == '__main__':
    # check if GST_PLUGIN_PATH includes path to local python elements, if not add it to the environment variable
    if f"{os.getcwd()}/plugins" not in os.environ.get("GST_PLUGIN_PATH", ""):
        print(f"Adding \"{os.getcwd()}/plugins\" path to GST_PLUGIN_PATH environment variable")
        os.environ["GST_PLUGIN_PATH"] = f"{os.environ.get('GST_PLUGIN_PATH', '')}:{os.getcwd()}/plugins"

    # Initialize Gst library, python plugin (if found) will load local python elements
    Gst.init(None)
    reg = Gst.Registry.get()
    if not reg.find_plugin("python"):
        print("GStreamer 'python' plugin not found in registry.")
        print("Check GST_PLUGIN_PATH includes path to 'libgstpython.so', if error persist please delete GStreamer registry cache.")
        print(">rm ~/.cache/gstreamer-1.0/registry.x86_64.bin")
        sys.exit(1)

    # Parse arguments
    input_argument, device_argument, output_argument = parse_args(sys.argv)

    # Download assets
    video_file = check_download_video_file(input_argument)
    detection_model = check_download_detection_model()

    # Create GStreamer pipeline and parametrize with downloaded models and video files
    PIPELINE_STR = build_pipeline(video_file, detection_model, device_argument, output_argument)
    print(f"Constructed Pipeline: \"{PIPELINE_STR}\"")
    pipeline = Gst.parse_launch(PIPELINE_STR)

    # Execute Gstreamer pipeline
    pipeline_loop(pipeline)
    sys.exit(0)
