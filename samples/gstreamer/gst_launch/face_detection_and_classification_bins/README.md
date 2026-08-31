# Face Detection And Classification with `processbin` (gst-launch command line)

This sample demonstrates the same face detection and classification use case as the
[Face Detection And Classification](../face_detection_and_classification/README.md) sample, but
built from low-level `processbin` bins (`openvino_tensor_inference`, `tensor_convert`,
`tensor_postproc_*`, `meta_aggregate`, `meta_overlay`) instead of the high-level `gvadetect` /
`gvaclassify` elements. Two variants are provided: a CPU pipeline and a GPU (VA-API) pipeline.

## How It Works

Both scripts build a chain of `processbin` elements, each wrapping one inference stage:
* `preprocess` - color-space conversion / scaling / VA-API surface handling and `tensor_convert`
  to prepare the tensor for inference
* `process` - `openvino_tensor_inference` running one OpenVINO™ model
* `postprocess` - one or more `tensor_postproc_*` elements that turn raw model output tensors
  into detection boxes, labels or landmark points
* `aggregate` - `meta_aggregate` collects results from all stages into the frame metadata

The two scripts differ as follows:

* `face_detection_and_classification_cpu.sh` runs on CPU and chains four `processbin` stages:
  face detection (`centerface`), age/gender classification, emotion classification, and facial
  landmark points, followed by `meta_overlay` for visualization.
* `face_detection_and_classification_gpu.sh` runs on GPU using VA-API preprocessing
  (`vapostproc`, `video/x-raw(memory:VAMemory)`) and chains two `processbin` stages: face
  detection (`centerface`) and emotion classification, followed by `meta_overlay device=GPU`.

## Models

The sample uses the following pre-trained models by default:
*   __centerface__ is the detection network for finding faces
*   __dima806/facial_age_image_detection__ / __dima806/fairface_gender_image_detection__ age and
    gender estimation on detected faces (CPU pipeline only)
*   __dima806/face_emotions_image_detection__ emotion recognition (both pipelines)

> **NOTE**: Before running samples (including this one), prepare required models using scripts in
> [`scripts/download_models`](../../../../scripts/download_models) and follow the per-script setup
> notes in [`README`](../../../../scripts/download_models/README.md).

## Running

```sh
./face_detection_and_classification_cpu.sh [INPUT_VIDEO] [OUTPUT]
./face_detection_and_classification_gpu.sh [INPUT_VIDEO] [OUTPUT]
```
Both scripts take two command-line *optional* parameters:
1. [INPUT_VIDEO] to specify input video file.
The input could be
* local video file
* web camera device (ex. `/dev/video0`)
* RTSP camera (URL starting with `rtsp://`) or other streaming source (ex URL starting with `http://`)
If parameter is not specified, the sample by default streams video example from HTTPS link (utilizing `urisourcebin` element) so requires internet connection.
2. [OUTPUT] to select the output mode:
    * display - render to screen (default)
    * fps - print FPS only
    * json - write metadata to `output.json`
    * display-and-json - render to screen and write metadata to `output.json`
    * file - render to an `.mp4` file

## Sample Output

The sample prints the full `gst-launch-1.0` command line into the console, then starts it and
either visualizes the video with bounding boxes/labels for detected faces or prints FPS/metadata
depending on the selected `OUTPUT` mode.

## See also
* [Samples overview](../../../README.md)
* [Face Detection And Classification](../face_detection_and_classification/README.md) (equivalent
  pipeline built with `gvadetect`/`gvaclassify`)
