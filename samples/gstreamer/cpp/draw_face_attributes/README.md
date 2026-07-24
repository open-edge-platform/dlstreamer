# Draw Face Attributes C++ Sample

This sample demonstrates how construct and control GStreamer pipeline from C++ application, and how to access metadata attached by inference elements to image buffer.

## How It Works
The sample utilizes GStreamer function `gst_parse_launch` to construct the pipeline from string representation. Then callback function is set on source pin of `gvawatermark` element in the pipeline.

The callback is invoked on every frame, it loops through inference metadata attached to the frame, converts raw tensor data into attributes and text labels (conversion depends on the model), and visualizes labels around detected objects.

Note that this sample doesn't contain .json files with post-processing rules as post-processing of classification results performed by sample itself (inside callback function), not by `gvaclassify` element.

## Models

The sample uses by default the following pre-trained models from OpenVINO™ Toolkit [Open Model Zoo](https://github.com/openvinotoolkit/open_model_zoo)
*   __centerface__ is primary detection network for finding faces and generates facial landmark points
*   __dima806/facial_age_image_detection__ age estimation on detected faces
*   __dima806/fairface_gender_image_detection__ gender estimation on detected faces
*   __dima806/face_emotions_image_detection__ emotions recognition

> **NOTE**: Before running samples (including this one), run script `download_omz_models.sh` once (the script located in `samples` top folder) to download all models required for this and other samples.

## Running

```sh
./build_and_run.sh [INPUT_VIDEO]
```

The script `build_and_run.sh` compiles the C++ sample into subfolder under `$HOME/intel/dl_streamer`, then runs the executable file.

If no input parameters specified, the sample by default streams video example from HTTPS link (utilizing `urisourcebin` element) so requires internet connection.
The command-line parameter INPUT_VIDEO allows to change input video and supports
* local video file
* web camera device (ex. `/dev/video0`)
* RTSP camera (URL starting with `rtsp://`) or other streaming source (ex URL starting with `http://`)

> **NOTE**: You may need the following dependencies to build the sample:
```sh
sudo apt install cmake make build-essential
## On ubuntu24 you may also need libopencv-dev to build the sample
sudo apt install libopencv-dev
```

## Sample Output

The sample
* prints GStreamer pipeline string as passed to function `gst_parse_launch`
* starts the pipeline and visualizes video with bounding boxes around detected faces, facial landmarks points, head pose, and text with classification results (age/gender, emotion) for each detected face

## See also
* [Samples overview](../../README.md)
