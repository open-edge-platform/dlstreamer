# Face Detection And Classification Sample (gst-launch command line)

This sample demonstrates face detection and classification pipeline constructed via `gst-launch-1.0` command-line utility.

## How It Works
The sample utilizes GStreamer command-line tool `gst-launch-1.0` which can build and run GStreamer pipeline described in a string format.
The string contains a list of GStreamer elements separated by exclamation mark `!`, each element may have properties specified in the format `property`=`value`.

This sample builds GStreamer pipeline of the following elements
* `filesrc` or `urisourcebin` or `v4l2src` for input from file/URL/web-camera
* `decodebin3` for video decoding
* `videoconvert` for converting video frame into different color formats
* [gvadetect](https://docs.openedgeplatform.intel.com/dev/edge-ai-libraries/dlstreamer/elements/gvadetect.html) for face detection based on OpenVINO™ Toolkit Inference Engine
* [gvaclassify](https://docs.openedgeplatform.intel.com/dev/edge-ai-libraries/dlstreamer/elements/gvaclassify.html) inserted into pipeline three times for face classification on three DL models (age-gender, emotion, landmark points)
* [gvawatermark](https://docs.openedgeplatform.intel.com/dev/edge-ai-libraries/dlstreamer/elements/gvawatermark.html) for bounding boxes and labels visualization
* `autovideosink` for rendering output video into screen
> **NOTE**: `sync=false` property in `autovideosink` element disables real-time synchronization so pipeline runs as fast as possible

## Models

The sample uses the following pre-trained models by default:
*   __centerface__ is the primary detection network for finding faces and generates facial landmark points
*   __dima806/facial_age_image_detection__ age estimation on detected faces
*   __dima806/fairface_gender_image_detection__ gender estimation on detected faces
*   __dima806/face_emotions_image_detection__ emotions recognition

These models are not from Open Model Zoo:
*   `centerface` is prepared by [`download_other_models.sh`](../../../../scripts/download_models/download_other_models.sh)
*   `dima806/*` models are prepared from Hugging Face via [`download_hf_models.py`](../../../../scripts/download_models/download_hf_models.py)

> **NOTE**: Before running samples (including this one), prepare required models using scripts in [`scripts/download_models`](../../../../scripts/download_models) and follow the per-script setup notes in [`README`](../../../../scripts/download_models/README.md).

## Running

```sh
./face_detection_and_classification.sh [INPUT_VIDEO] [DEVICE] [SINK_ELEMENT]
```
The sample takes three command-line *optional* parameters:
1. [INPUT_VIDEO] to specify input video file.
The input could be
* local video file
* web camera device (ex. `/dev/video0`)
* RTSP camera (URL starting with `rtsp://`) or other streaming source (ex URL starting with `http://`)
If parameter is not specified, the sample by default streams video example from HTTPS link (utilizing `urisourcebin` element) so requires internet connection.
2. [DEVICE] to specify device for detection and classification.
        Please refer to OpenVINO™ toolkit documentation for supported devices.
        https://docs.openvinotoolkit.org/latest/openvino_docs_IE_DG_supported_plugins_Supported_Devices.html
        You can find what devices are supported on your system by running following OpenVINO™ toolkit sample:
        https://docs.openvinotoolkit.org/latest/openvino_inference_engine_ie_bridges_python_sample_hello_query_device_README.html
3. [SINK_ELEMENT] to choose between render mode and fps throughput mode:
    * display - render (default)
    * fps - FPS only

## Sample Output

The sample
* prints gst-launch-1.0 full command line into console
* starts the command and either visualizes video with bounding boxes around detected faces, facial landmarks points and text with classification results (age/gender, emotion) for each detected face or
prints out fps if you set SINK_ELEMENT = fps

## See also
* [Samples overview](../../README.md)
