# Draw Face Attributes C++ Sample

This sample demonstrates how construct and control GStreamer pipeline from C++ application, and how to access metadata attached by inference elements to image buffer.

## How It Works
The sample utilizes GStreamer function `gst_parse_launch` to construct the pipeline from string representation. Then callback function is set on source pin of `gvawatermark` element in the pipeline.

The callback is invoked on every frame, it loops through inference metadata attached to the frame, converts raw tensor data into attributes and text labels (conversion depends on the model), and visualizes labels around detected objects.

Note that this sample doesn't contain .json files with post-processing rules as post-processing of classification results performed by sample itself (inside callback function), not by `gvaclassify` element.

## Models

The sample requires these models; the application searches `$MODELS_PATH` recursively for the hard-coded file names (preferring a path containing `FP16`) and its callback converts their outputs by model name:
*   `centerface.xml` - face detection with facial landmark points, prepared with `download_other_models.py`
*   `dima806_facial_age_image_detection.xml`, `dima806_fairface_gender_image_detection.xml`, `dima806_face_emotions_image_detection.xml` - age, gender and emotion classification, prepared with `download_hf_models.py`

Both helpers are described in [`scripts/download_models`](../../../../scripts/download_models/README.md); use `$MODELS_PATH` as their output root. The models used to validate this sample are listed in [Validation](#validation).

## Running

```sh
./build_and_run.sh [INPUT_VIDEO] [OUTPUT] [DEVICE] [OUTPUT_DIRECTORY]
```

The script `build_and_run.sh` compiles the C++ sample into subfolder under `$HOME/intel/dl_streamer`, then runs the executable file.

The script requires `INPUT_VIDEO`; output, device, and output directory are optional:
1. [INPUT_VIDEO] input video file.
Use any video supported by GStreamer; a clip containing faces is useful for this example. Pass its path as `INPUT_VIDEO`.
The command-line parameter INPUT_VIDEO allows to change input video and supports
* local video file
* web camera device (ex. `/dev/video0`)
* RTSP camera (URL starting with `rtsp://`) or other streaming source (ex URL starting with `http://`)
2. [OUTPUT] output mode (default: `json`). Valid values: `display`, `display-and-json`, `json`, `file`.
3. [DEVICE] inference device (default: `CPU`).
4. [OUTPUT_DIRECTORY] directory `output.json` is copied to after the run (default: `/home/dlstreamer/`,
   matching the DL Streamer Docker image's default user home). On a bare-metal install this
   directory likely doesn't exist, so pass an existing writable directory (e.g. `.`) explicitly.

> **NOTE:** You may need the following dependencies to build the sample:
```sh
sudo apt install cmake make build-essential
## On ubuntu24 you may also need libopencv-dev to build the sample
sudo apt install libopencv-dev
```

## Sample Output

The sample
* prints GStreamer pipeline string as passed to function `gst_parse_launch`
* starts the pipeline and visualizes video with bounding boxes around detected faces, facial landmarks points, head pose, and text with classification results (age/gender, emotion) for each detected face

## Validation

This sample was validated with:

- Models: `centerface` (FP16), `dima806_facial_age_image_detection`, `dima806_fairface_gender_image_detection`, `dima806_face_emotions_image_detection` (FP16)
- Video: [`head-pose-face-detection-female-and-male.mp4`](https://github.com/intel-iot-devkit/sample-videos/raw/master/head-pose-face-detection-female-and-male.mp4)

## See also
* [Samples overview](../../../README.md)
