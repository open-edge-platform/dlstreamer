# Human Pose Estimation Sample (gst-launch command line)

This sample demonstrates human pose estimation pipeline constructed via `gst-launch-1.0` command-line utility.

## How It Works
The sample utilizes GStreamer command-line tool `gst-launch-1.0` which can build and run GStreamer pipeline described in a string format.
The string contains a list of GStreamer elements separated by exclamation mark `!`, each element may have properties specified in the format `property`=`value`.

This sample builds GStreamer pipeline of the following elements
* `filesrc` or `urisourcebin` or `v4l2src` for input from file/URL/web-camera
* `decodebin3` for video decoding
* `videoconvert` for converting video frame into different color formats
* [gvadetect](../../../../docs/user-guide/elements/gvadetect.md) with `inference-region=full-frame` used for full-frame inference and post-processing of the pose keypoints model output
* [gvawatermark](../../../../docs/user-guide/elements/gvawatermark.md) for points and theirs connections visualization
* `autovideosink` for rendering output video into screen
> **NOTE**: `sync=false` property in `autovideosink` element disables real-time synchronization so pipeline runs as fast as possible

## Models

The sample is configured with the `yolo26s-pose` example model from Ultralytics. Prepare a compatible OpenVINO IR pose model using the general Ultralytics conversion helper; see [`scripts/download_models/README.md`](../../../../scripts/download_models/README.md) for supported models and setup.
*   __yolo26s-pose__ generates poses keypoints

The listed pose model is an example for the current pipeline configuration. Prepare a compatible pose model before running; use the general helpers in [`scripts/download_models`](../../../../scripts/download_models/README.md) if conversion is needed. When substituting the model, update its path and matching model-proc configuration.

The sample contains `model_proc` subfolder with .json files for each model with description of model input/output formats and post-processing rules for classification models.

## Running

```sh
./human_pose_estimation.sh [INPUT_VIDEO] [DEVICE] [SINK_ELEMENT]
```
The sample requires an input source; device and sink are optional:
1. [INPUT_VIDEO] to specify input video file.
The input could be
* local video file
* web camera device (ex. `/dev/video0`)
* RTSP camera (URL starting with `rtsp://`) or other streaming source (ex URL starting with `http://`)
Use any local video, supported stream URL, or camera source. A clip containing a visible person is useful for pose estimation.
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
* starts the command and either visualizes video with people's skeleton
or prints out fps if you set SINK_ELEMENT = fps

## See also
* [Samples overview](../../../README.md)
