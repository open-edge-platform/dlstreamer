# gvaattachroi Sample

This sample demonstrates gvaattachroi element and ability to define the area of interest on which the inference should be performed. This feature is particularly useful when the focus is on processing only a specific part of an image, for example, monitoring traffic on a road in a city camera feed, rather than the surrounding areas. It can also be beneficial when dealing with large images where running inference on the entire image may lead to inaccurate results.

## How It Works

The sample utilizes GStreamer command-line tool `gst-launch-1.0` which can build and run GStreamer pipeline described in a string format.
The string contains a list of GStreamer elements separated by exclamation mark `!`, each element may have properties specified in the format `property`=`value`.

This sample builds GStreamer pipeline of the following elements

* `filesrc` or `urisourcebin` or `v4l2src` for input from file/URL/web-camera
* `decodebin3` for video decoding
* `videoconvert` for converting video frame into different color formats
* [gvaattachroi](../../../../docs/user-guide/elements/gvaattachroi.md) for defining the areas of interest (one or more) in the input image
* [gvadetect](../../../../docs/user-guide/elements/gvadetect.md) uses for roi object detection and marking objects with labels
* [gvawatermark](../../../../docs/user-guide/elements/gvawatermark.md) for points and theirs connections visualization
* `autovideosink` for rendering output video into screen

> **NOTE**: `sync=false` property in `autovideosink` element disables real-time synchronization so pipeline runs as fast as possible

## Model

The example pipeline is configured for the YOLOv8s detector under `$MODELS_PATH/public/yolov8s/FP32/`. A compatible OpenVINO detector may be used by updating the model path in the sample script. Prepare it with the general [`download_ultralytics_models.py`](../../../../scripts/download_models/README.md#2-ultralytics-conversion) helper; see its README for supported models and setup. Set `MODELS_PATH` to the model store used by the sample.

## Running

Run sample:

```sh
./gvaattachroi_sample.sh [INPUT_VIDEO] [DEVICE] [SINK_ELEMENT] [ROI_COORDS]
```

The sample requires `INPUT_VIDEO`; device, sink, and ROI coordinates are optional:

1. [INPUT_VIDEO] to specify input video file.
    The input could be
    * local video file
    * web camera device (ex. `/dev/video0`)
    * RTSP camera (URL starting with `rtsp://`) or other streaming source (ex URL starting with `http://`)
    Use any local video, supported stream URL, or camera source. A clip containing objects in the selected model's label set is useful for checking the ROI.
2. [DEVICE] to specify device for detection and classification.
        Please refer to OpenVINO™ toolkit documentation for supported devices.
        <https://docs.openvinotoolkit.org/latest/openvino_docs_IE_DG_supported_plugins_Supported_Devices.html>
        You can find what devices are supported on your system by running following OpenVINO™ toolkit sample:
        <https://docs.openvinotoolkit.org/latest/openvino_inference_engine_ie_bridges_python_sample_hello_query_device_README.html>
        Default value: CPU
3. [SINK_ELEMENT] to choose between different output formats:
    * file (default)
    * display - render
    * fps - FPS only
    * json - json file with metadata
    * display-and-json - render and json file
4. [ROI_COORDS] to manually define the coordinates at which the ROI should be located
    format: x_top_left,y_top_left,x_bottom_right,y_bottom_right
    example: 100,150,200,300
    If not defined, the [roi list file](roi_list.json) will be used.
    You can also edit the [roi list file](roi_list.json) file to add more ROIs
    or change the coordinates of the current ones.

Example:

```sh
./gvaattachroi_sample.sh /path/to/video.mp4 GPU file 100,150,200,300
```

## See also

* [Samples overview](../../../README.md)

---

*Intel, the Intel logo, OpenVINO, and the OpenVINO logo are trademarks of Intel Corporation or its subsidiaries.*
