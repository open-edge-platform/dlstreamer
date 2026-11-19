# Object Detection and Classification with Yolo models (gst-launch command line)

This set of samples demonstrates how to construct object detection pipelines, and one detection-plus-classification pipeline, via `gst-launch-1.0` using the latest YOLO models.

## How It Works

This set of samples utilizes GStreamer command-line tool `gst-launch-1.0` which can build and run GStreamer pipeline described in a string format.
The string contains a list of GStreamer elements separated by exclamation mark `!`, each element may have properties specified in the format `property`=`value`.

This sample builds a GStreamer pipeline from the following elements:

* `filesrc` or `urisourcebin` or `v4l2src` for input from file/URL/web-camera
* `decodebin3` for video decoding
* `videoconvert` for converting video frame into different color formats
* [gvadetect](../../../../docs/user-guide/elements/gvadetect.md) for full-frame object detection and marking objects with labels
* [gvaclassify](../../../../docs/user-guide/elements/gvaclassify.md) for ROI classification when `yolo26s-cls` mode is selected
* [gvawatermark](../../../../docs/user-guide/elements/gvawatermark.md) for object and classification label visualization
* `autovideosink` for rendering output video into screen

> **NOTE**: `sync=false` property in `autovideosink` element disables real-time synchronization so pipeline runs as fast as possible

## Models

The `yolo_detect.sh` script only accepts a `MODEL` value from the fixed supported list in the table below; it loads the model from `$MODELS_PATH/public/<MODEL>/<PRECISION>/<MODEL>.xml` (for `yolo26s-cls`, both `yolo26s` and `yolo26s-cls` from the same layout).
Prepare the models with the general helpers in [`scripts/download_models`](../../../../scripts/download_models/README.md): `download_ultralytics_models.py` for Ultralytics models and `download_other_models.py` for `yolox-tiny`, `yolox_s` and `yolov7`.
The models used to validate this sample are listed in [Validation](#validation).

| Model        | Model Preparation                  | Model pipeline (model-proc)       |
| ------------ | ---------------------------------- | ----------------------------------|
| yolox-tiny   | `download_other_models.py`         | gvadetect model-proc=yolo-x.json  |
| yolox_s      | `download_other_models.py`         | gvadetect model-proc=yolo-x.json  |
| yolov5nu     | `download_ultralytics_models.py`   | gvadetect \<model-proc not needed\> |
| yolov7       | `download_other_models.py`         | gvadetect model-proc=yolo-v7.json |
| yolov8s      | `download_ultralytics_models.py`   | gvadetect \<model-proc not needed\> |
| yolov8n-obb  | `download_ultralytics_models.py`   | gvadetect \<model-proc not needed\> |
| yolov8n-seg  | `download_ultralytics_models.py`   | gvadetect \<model-proc not needed\> |
| yolov9c      | `download_ultralytics_models.py`   | gvadetect \<model-proc not needed\> |
| yolov10s     | `download_ultralytics_models.py`   | gvadetect \<model-proc not needed\> |
| yolo11s      | `download_ultralytics_models.py`   | gvadetect \<model-proc not needed\> |
| yolo11s-seg  | `download_ultralytics_models.py`   | gvadetect \<model-proc not needed\> |
| yolo11s-obb  | `download_ultralytics_models.py`   | gvadetect \<model-proc not needed\> |
| yolo11s-pose | `download_ultralytics_models.py`   | gvadetect \<model-proc not needed\> |
| yolo26n      | `download_ultralytics_models.py`   | gvadetect \<model-proc not needed\> |
| yolo26s      | `download_ultralytics_models.py`   | gvadetect \<model-proc not needed\> |
| yolo26m      | `download_ultralytics_models.py`   | gvadetect \<model-proc not needed\> |
| yolo26l      | `download_ultralytics_models.py`   | gvadetect \<model-proc not needed\> |
| yolo26x      | `download_ultralytics_models.py`   | gvadetect \<model-proc not needed\> |
| yolo26s-obb  | `download_ultralytics_models.py`   | gvadetect \<model-proc not needed\> |
| yolo26s-seg  | `download_ultralytics_models.py`   | gvadetect \<model-proc not needed\> |
| yolo26s-pose | `download_ultralytics_models.py`   | gvadetect \<model-proc not needed\> |
| yolo26s-cls  | `download_ultralytics_models.py`   | gvadetect `yolo26s` + gvaclassify `yolo26s-cls` |

## Samples

The sample `yolo_detect.sh` script can be used to build and run an object detection pipeline. For `yolo26s-cls`, it builds a composite pipeline with `yolo26s` detection followed by ROI classification with `yolo26s-cls`.
Use any video supported by the installed GStreamer plugins and any compatible YOLO model from the supported list. The commands below use example model names and local paths; their values are not fixed requirements.

```sh
./yolo_detect.sh <MODEL> <DEVICE> <INPUT> <OUTPUT_TYPE> <PPBKEND> <PRECISION> <OUTPUT_DIRECTORY> <EOS_LIMIT_ELEMENT>
```

> **NOTE**: Prior to running `yolo_detect.sh`, export the required YOLO model with `scripts/download_models/download_ultralytics_models.py` (see `scripts/download_models/README.md` for commands and venv setup).

Example run of the `yolo11s` model with CPU device on a local video, saving results to a file:

```sh
./yolo_detect.sh yolo11s CPU /path/to/video.mp4
```

Another example run of the `yolo11s` model with GPU device on a local video, saving results to a file:

```sh
./yolo_detect.sh yolo11s GPU /path/to/video.mp4
```

Yet another example run of `yolov9c` model on your own mp4 video, showing results on a display:

```sh
./yolo_detect.sh yolov9c CPU path/to/your/video.mp4 display
```

Example run of the composite `yolo26s-cls` mode with GPU device on a local video, showing detection and classification labels on each detected ROI:

```sh
./yolo_detect.sh yolo26s-cls GPU /path/to/your/video.mp4 display
```

Example run of `yolo11s-obb` with FP32 precision and VA-API acceleration:

```sh
./yolo_detect.sh yolo11s-obb GPU /path/to/video.mp4 file va FP32
```

Sample output for `yolo11s-obb` model:

![Sample Output](yolo11s-obb_sample_output.jpeg)

Example run of `yolo11s-seg` with FP32 precision and VA-API acceleration:

```sh
./yolo_detect.sh yolo11s-seg GPU /path/to/video.mp4 file va FP32
```

Sample output for `yolo11s-seg` model:

![Sample Output](yolo11s-seg_sample_output.jpeg)

Please examine `yolo_detect.sh` script to see how to source data from input camera and how to generate other types of the output.

## Validation

This sample was validated with:

- Models: `yolox_s` (default), `yolo11s`, `yolo11s-seg`, `yolov8n-seg`, `yolo26n`, `yolo26s`, `yolo26m`, `yolo26l`, `yolo26x`, `yolo26s-seg` (FP32, FP16 or INT8)
- Video: [`1192116-sd_640_360_30fps.mp4`](https://videos.pexels.com/video-files/1192116/1192116-sd_640_360_30fps.mp4)
- Model `yolo11s-obb` (FP32) with video: [`3150382-sd_640_338_25fps.mp4`](https://videos.pexels.com/video-files/3150382/3150382-sd_640_338_25fps.mp4)

## See also

* [Samples overview](../../../README.md)

---

*Intel, the Intel logo, OpenVINO, and the OpenVINO logo are trademarks of Intel Corporation or its subsidiaries.*
