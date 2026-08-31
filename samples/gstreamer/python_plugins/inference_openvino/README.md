# Object Detection with `inference_openvino` and VA-API Preprocessing (gst-launch command line)

This sample demonstrates object detection built from low-level `processbin` bins using the
`inference_openvino` inference element (instead of `gvadetect`) together with a VA-API
(`vapostproc`) preprocessing chain running entirely on GPU memory (`video/x-raw(memory:VAMemory)`).

## How It Works

The sample builds a single `processbin` stage:
* `preprocess` - `vapostproc ! videoconvert ! tensor_convert` prepares the tensor for inference on
  GPU-resident video memory
* `process` - `inference_openvino` runs the OpenVINO™ detection model
* `postprocess` - `tensor_postproc_detection` turns raw model output into detection boxes
* `aggregate` - `meta_aggregate` attaches detection results to the frame metadata

`vapostproc` is used both before and after the `processbin` stage to move data in/out of VA-API
surfaces.

## Models

The sample uses by default:
*   __centerface__ face detection model (`FP32`)

> **NOTE**: Before running samples (including this one), prepare required models using scripts in
> [`scripts/download_models`](../../../../scripts/download_models) and follow the per-script setup
> notes in [`README`](../../../../scripts/download_models/README.md).

## Running

```sh
./object_detection_vaapi_preproc.sh [INPUT_VIDEO] [DEVICE] [OUTPUT] [MODEL]
```
The sample takes four command-line *optional* parameters:
1. [INPUT_VIDEO] to specify input video file.
The input could be
* local video file
* web camera device (ex. `/dev/video0`)
* RTSP camera (URL starting with `rtsp://`) or other streaming source (ex URL starting with `http://`)
If parameter is not specified, the sample by default streams video example from HTTPS link (utilizing `urisourcebin` element) so requires internet connection.
2. [DEVICE] to specify inference device (default: `CPU`).
3. [OUTPUT] to select the output mode (default: `display-async`):
    * display - render to screen
    * display-async - render to screen without real-time synchronization
    * fps - print FPS only
    * json - write metadata to `output.json`
    * display-and-json - render to screen and write metadata to `output.json`
4. [MODEL] path to the OpenVINO IR `.xml` model file (default: `$MODELS_PATH/public/centerface/FP32/centerface.xml`).

## Sample Output

The sample prints the full `gst-launch-1.0` command line into the console, then starts it and
either visualizes the video with detection bounding boxes or prints FPS/metadata depending on the
selected `OUTPUT` mode.

## See also
* [Samples overview](../../../README.md)
