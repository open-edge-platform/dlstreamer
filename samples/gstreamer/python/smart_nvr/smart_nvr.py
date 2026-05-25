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

import gi
import os
import openvino as ov
import shutil
# sample uses fixed developer tools for model export.
import subprocess  # nosec B404
import sys
import urllib.parse
import urllib.request

gi.require_version("Gst", "1.0")
from gi.repository import Gst   # pylint: disable=no-name-in-module

DEFAULT_VIDEO_URL = "https://videos.pexels.com/video-files/2431853/2431853-hd_1920_1080_25fps.mp4"

def validate_sample_video_url(url):
    """Return True for the fixed sample video host."""
    parsed = urllib.parse.urlparse(url)
    return parsed.scheme == "https" and parsed.hostname == "videos.pexels.com"

def find_tool(name):
    """Resolve a developer tool from PATH."""
    tool_path = shutil.which(name)
    if tool_path:
        return tool_path
    print(f"Required tool '{name}' was not found in PATH.")
    return None

def run_model_tool(command, cwd=None):
    """Run a fixed model-preparation command without shell interpretation."""
    result = subprocess.run(command, check=False, shell=False, cwd=cwd)  # nosec B603
    if result.returncode != 0:
        print(f"Model preparation command failed with exit code {result.returncode}: {' '.join(command)}")
        return False
    return True

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

def check_download_video_file():
    """Check if the default video file exists locally, if not, download it."""
    input_video = os.path.join(os.getcwd(), "2431853-hd_1920_1080_25fps.mp4")

    # download if local copy does not exist
    if not os.path.isfile(input_video):
        input_video = os.path.join(os.getcwd(), "2431853-hd_1920_1080_25fps.mp4")
        print("\nNo input provided. Downloading default video...\n")
        if not validate_sample_video_url(DEFAULT_VIDEO_URL):
            print(f"Unexpected default video URL: {DEFAULT_VIDEO_URL}")
            return None
        request = urllib.request.Request(
            DEFAULT_VIDEO_URL,
            headers={"User-Agent": "Mozilla/5.0"},
        )
        # fixed https sample asset from videos.pexels.com.
        with urllib.request.urlopen(request) as response, open(input_video, "wb") as output:  # nosec B310
            output.write(response.read())

    return input_video

def check_download_detection_model():
    """Check if the default detection model exists locally, if not, download it."""
    ov_model_path = os.path.join(os.getcwd(), "rtdetr_v2_r50vd/model.xml")

    # download RTDETRv2 model from Hugging Face Model Hub if local copy does not exist
    if not os.path.isfile(ov_model_path):
        print("Downloading PekingU/rtdetr_v2_r50vd from HuggingFace\n")
        optimum_cli = find_tool("optimum-cli")
        hf_cli = find_tool("hf")
        ovc_cli = find_tool("ovc")
        if not all((optimum_cli, hf_cli, ovc_cli)):
            return None
        if not run_model_tool([optimum_cli, "export", "onnx", "--model", "PekingU/rtdetr_v2_r50vd",
                               "--task", "object-detection", "--opset", "18", "--width", "640", "--height", "640", "rtdetr_v2_r50vd"]):
            return None
        if not run_model_tool([hf_cli, "download", "PekingU/rtdetr_v2_r50vd", "--include", "preprocessor_config.json", "--local-dir", "."], cwd="rtdetr_v2_r50vd"):
            return None
        if not run_model_tool([ovc_cli, "model.onnx"], cwd="rtdetr_v2_r50vd"):
            return None
        print(f"Model exported to OpenVINO IR format at: {ov_model_path}\n")

    return ov_model_path

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

    # Download assets
    video_file = check_download_video_file()
    detection_model = check_download_detection_model()
    if video_file is None or detection_model is None:
        sys.exit(1)

    # Create GStreamer pipeline and parametrize with downloaded models and video files
    PIPELINE_STR = f"filesrc location={video_file} ! decodebin3 ! " \
        f"gvadetect model={detection_model} device=GPU batch-size=4 threshold=0.7 ! queue ! " \
        f"gvaanalytics_py distance=500 angle=-135,-45 ! gvafpscounter ! gvawatermark ! " \
        f"gvarecorder_py location=output.mp4 max-time=10"
    print(f"Constructed Pipeline: \"{PIPELINE_STR}\"")
    pipeline = Gst.parse_launch(PIPELINE_STR)

    # Execute Gstreamer pipeline
    pipeline_loop(pipeline)
    sys.exit(0)
