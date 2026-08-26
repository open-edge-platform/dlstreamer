# ==============================================================================
# Copyright (C) 2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================
"""Run face detection and classification using pre-exported OpenVINO models."""

import argparse
import os
import sys
from pathlib import Path

import gi

sys.path.insert(
    0,
    os.path.dirname(
        os.path.dirname(
            os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
        )
    ),
)
# pylint: disable-next=wrong-import-position
from shared_utils import download_https

gi.require_version("Gst", "1.0")
gi.require_version("GstAnalytics", "1.0")
# pylint: disable-next=no-name-in-module, wrong-import-position
from gi.repository import Gst

DEFAULT_VIDEO_URL = "https://videos.pexels.com/video-files/18553046/18553046-hd_1280_720_30fps.mp4"
DEFAULT_DETECTION_MODEL_REL = (
    "public/arnabdhar_YOLOv8-Face-Detection/FP16/"
    "arnabdhar_YOLOv8-Face-Detection.xml"
)
DEFAULT_CLASSIFICATION_MODEL_REL = (
    "public/dima806_fairface_age_image_detection/FP16/"
    "dima806_fairface_age_image_detection.xml"
)


def get_runtime_dir():
    """Return the current working directory used as the runtime directory."""
    return os.getcwd()


def ensure_file(path, description):
    """Return resolved path if file exists; exit with error otherwise."""
    p = Path(path)
    if not p.is_file():
        sys.stderr.write(f"Error: {description} not found: {path}\n")
        sys.exit(1)
    return str(p.resolve())


def parse_args(args):
    """Parse command-line arguments for the sample."""
    parser = argparse.ArgumentParser(
        description="Run face detection + classification using pre-exported OpenVINO models."
    )
    parser.add_argument("--input", default=None, help="Path to input video file")
    parser.add_argument("--device", default="GPU", help="Inference device: CPU, GPU or NPU")
    parser.add_argument("--output", default="file", help="Output mode: file or json")
    parser.add_argument(
        "--det-model",
        default=None,
        help="Path to the face-detection OpenVINO model XML (default: $MODELS_PATH/" + DEFAULT_DETECTION_MODEL_REL + ")",
    )
    parser.add_argument(
        "--cls-model",
        default=None,
        help="Path to the age/gender/classification OpenVINO model XML (default: $MODELS_PATH/" + DEFAULT_CLASSIFICATION_MODEL_REL + ")",
    )
    parsed = parser.parse_args(args[1:])
    return parsed.input, parsed.device, parsed.output, parsed.det_model, parsed.cls_model


def prepare_input_video(input_arg):
    """Prepare the input video, downloading the default clip if needed."""
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


def pipeline_loop(pipeline):
    """Start the GStreamer pipeline and stop it on EOS or ERROR."""
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


def main(input_video, device, output, detection_model_path, classification_model_path):
    """Build and run the DL Streamer GStreamer pipeline."""
    models_path = os.environ.get("MODELS_PATH", "./models")
    runtime_dir = get_runtime_dir()
    detection_model_path = detection_model_path or os.path.join(
        models_path, DEFAULT_DETECTION_MODEL_REL
    )
    classification_model_path = classification_model_path or os.path.join(
        models_path, DEFAULT_CLASSIFICATION_MODEL_REL
    )

    ov_detection_model_path = ensure_file(detection_model_path, "detection model")
    ov_classification_model_path = ensure_file(classification_model_path, "classification model")

    # STEP 1: Build and run the DL Streamer GStreamer pipeline

    Gst.init([])

    if output == "json":
        output_json = os.path.join(runtime_dir, "output.json")
        if os.path.isfile(output_json):
            os.remove(output_json)
        sink = (
            "gvafpscounter ! gvametaconvert add-tensor-data=true ! "
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
    input_argument, device_argument, output_argument, det_model, cls_model = parse_args(sys.argv)
    video_path = prepare_input_video(input_argument)
    sys.exit(main(video_path, device_argument, output_argument, det_model, cls_model))
