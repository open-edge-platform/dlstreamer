# Action Recognition Sample (gst-launch command line)

This sample demonstrates action recognition pipeline constructed via `gst-launch-1.0` command-line utility.

## How It Works
The sample utilizes GStreamer command-line tool `gst-launch-1.0` which can build and run GStreamer pipeline described in a string format.
The string contains a list of GStreamer elements separated by exclamation mark `!`, each element may have properties specified in the format `property`=`value`.

This sample builds GStreamer pipeline of the following elements
* `filesrc` or `urisourcebin` or `v4l2src` for input from file/URL/web-camera
* `decodebin3` for video decoding
* `video_inference` for converting video frame into custom tensor, inferencing using OpenVINO™ toolkit and post process data.
* [gvawatermark](../../../../docs/user-guide/elements/gvawatermark.md) for labels visualization
* `gvafpscounter` for rendering fps info in terminal
* `autovideosink` for rendering output video into screen
> **NOTE**: `sync=false` property in `autovideosink` element disables real-time synchronization so pipeline runs as fast as possible

## Models

The sample uses by default the following pre-trained models from OpenVINO™ toolkit [Open Model Zoo](https://github.com/openvinotoolkit/open_model_zoo)
*   __action-recognition-0001-encoder__ encoder part of action recognition
*   __action-recognition-0001-decoder__ decoder part of action recognition

> **NOTE**: Before running samples (including this one), run script `download_omz_models.sh` once (the script located in `samples` top folder) to download all models required for this and other samples.

The sample contains `model_proc` subfolder with .json file with description of input/output formats and post-processing rules for action recognition models.

## Running

```sh
./action_recognition.sh INPUT_VIDEO [DEVICE] [SINK_ELEMENT]
```
The sample takes three command-line parameters:
1. [INPUT_VIDEO] to specify input video file (*required*).
The input could be
* local video file
* web camera device (ex. `/dev/video0`)
* RTSP camera (URL starting with `rtsp://`) or other streaming source (ex URL starting with `http://`)
2. [DEVICE] to specify device for inference (*optional*).
        Please refer to OpenVINO™ toolkit documentation for supported devices.
        https://docs.openvinotoolkit.org/latest/openvino_docs_IE_DG_supported_plugins_Supported_Devices.html
        You can find what devices are supported on your system by running following OpenVINO™ toolkit sample:
        https://docs.openvinotoolkit.org/latest/openvino_inference_engine_ie_bridges_python_sample_hello_query_device_README.html
3. [SINK_ELEMENT] to choose between render mode and fps throughput mode (*optional*):
    * display - render (default)
    * fps - FPS only

## Sample Output

The sample
* prints gst-launch-1.0 full command line into console
* starts the command and either visualizes video with action text labels or prints out fps if you set SINK_ELEMENT = fps

## See also
* [Samples overview](../../README.md)
