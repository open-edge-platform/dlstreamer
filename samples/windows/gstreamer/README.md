# Deep Learning Streamer (DL Streamer) Samples (Windows)

Samples are simple applications that demonstrate how to use the Intel® DL Streamer on Windows systems.

## Available Samples

Samples separated into several categories
1. gst_launch command-line samples (samples construct GStreamer pipeline via [gst-launch-1.0](https://gstreamer.freedesktop.org/documentation/tools/gst-launch.html) command-line utility)
    * [Face Detection And Classification Sample](./gst_launch/face_detection_and_classification/README.md) - constructs object detection and classification pipeline example with [gvadetect](https://dlstreamer.github.io/elements/gvadetect.html) and [gvaclassify](https://dlstreamer.github.io/elements/gvaclassify.html) elements to detect faces and estimate age, gender, emotions and landmark points
    * [Audio Event Detection Sample](./gst_launch/audio_detect/README.md) - constructs audio event detection pipeline example with [gvaaudiodetect](https://dlstreamer.github.io/elements/gvaaudiodetect.html) element and uses [gvametaconvert](https://dlstreamer.github.io/elements/gvametaconvert.html), [gvametapublish](https://dlstreamer.github.io/elements/gvametapublish.html) elements to convert audio event metadata with inference results into JSON format and to print on standard out
2. Benchmark
    * [Benchmark Sample](./benchmark/README.md) - measures overall performance of single-channel or multi-channel video analytics pipelines

## How To Build And Run

Samples provide `.bat` script for constructing and executing gst-launch command line on Windows.

## DL Models

DL Streamer samples use pre-trained models from OpenVINO™ Toolkit [Open Model Zoo](https://github.com/openvinotoolkit/open_model_zoo)

Before running samples, run script `download_omz_models.bat` once to download all models required for samples. The script located in `samples\windows` folder.
> **NOTE**: To install all necessary requirements for `download_omz_models.bat` script run this command:
```batch
python -m pip install --upgrade pip
python -m pip install openvino-dev[onnx]
```

## Input video

First command-line parameter in DL Streamer samples specifies input video and supports
* local video file (`C:\path\to\video.mp4`)
* web camera device (Windows USB camera path format, uses `ksvideosrc` element)
* RTSP camera (URL starting with `rtsp://`) or other streaming source (ex URL starting with `http://`)

If command-line parameter not specified, most samples by default stream video example from predefined HTTPS link, so require internet connection.

> **NOTE**: Most samples set property `sync=false` in video sink element to disable real-time synchronization and run pipeline as fast as possible. Change to `sync=true` to run pipeline with real-time speed.

## See also
* [Windows Samples overview](../README.md)
* [Linux GStreamer Samples](../../gstreamer/README.md)
