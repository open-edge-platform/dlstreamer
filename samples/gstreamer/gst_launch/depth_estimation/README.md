# Depth Estimation Sample (gst-launch command line)

This sample demonstrates object-aware depth estimation with a `gst-launch-1.0` pipeline built from a YOLO11n detector followed by the Hugging Face `Depth-Anything-V2-Small-hf` depth-estimation model.

## How It Works

The sample constructs a GStreamer pipeline string and runs it with `gst-launch-1.0`.
The pipeline combines the following elements:

* `filesrc`, `urisourcebin`, or `v4l2src` for file, URL, or webcam input
* `decodebin3` for video decoding
* `gvadetect` for full-frame object detection with YOLO11n
* `gvaclassify` for running `Depth-Anything-V2-Small-hf` on detected regions
* `gvawatermark` for rendering detections on display output
* `gvametaconvert` and `gvametapublish` for writing depth metric tensors to JSON output
* `gvafpscounter` for FPS measurement

The shell script keeps detector and depth-estimator devices independent so you can reproduce pipelines such as GPU detection with GPU depth estimation, or GPU detection with CPU depth estimation.
For each detected ROI, the mean depth value is attached as a classification label. Additional depth metrics are attached as a `depth_metrics` tensor and are included in JSON output modes: center depth, mean depth, median depth, minimum depth, maximum depth, standard deviation, valid pixel count, and valid pixel ratio.

## Models

This sample expects two OpenVINO IR models:

* `yolo11n` in FP16 precision from Ultralytics for object detection
* `depth-anything/Depth-Anything-V2-Small-hf` in FP16 precision from HuggingFace for depth estimation

Use the model conversion scripts in [scripts/download_models/](../../../../scripts/download_models/README.md) to prepare both models.
Before running them, create and activate the dedicated model-download virtual environment described in [scripts/download_models/README.md](../../../../scripts/download_models/README.md).

Set `MODELS_PATH` environment variable to the main directory (specified as `--outdir <output_dir>`) where models have been downloaded, for example:

```sh
export MODELS_PATH="$HOME/models"
```

## Running

Running the sample:

```sh
cd /opt/intel/dlstreamer/samples/gstreamer/gst_launch/depth_estimation/
./depth_estimation.sh [INPUT] [DETECT_DEVICE] [DEPTH_DEVICE] [OUTPUT]
```

The sample takes four optional parameters:

1. `[INPUT]` specifies the input source.
   The input can be:
   * a local video file
   * a webcam device such as `/dev/video0`
   * a streaming URL such as `rtsp://`, `http://`, or `https://`

   If omitted, the sample uses this default video:
   `https://videos.pexels.com/video-files/18553046/18553046-hd_1280_720_30fps.mp4`

2. `[DETECT_DEVICE]` selects the device used by `gvadetect`.
   Supported values: `CPU`, `GPU`

3. `[DEPTH_DEVICE]` selects the device used by `gvaclassify`.
   Supported values: `CPU`, `GPU`

4. `[OUTPUT]` selects the output mode.
   Supported values:
   * `display` - render annotated video
   * `fps` - print FPS only
   * `json` - write per-object depth metric tensors as json-lines (one line per frame) to `output.json`
   * `display-and-json` - render annotated video and write `output.json`

Example runs:

```sh
# GPU detection + GPU depth estimation
./depth_estimation.sh

# GPU detection + CPU depth estimation with JSON output
./depth_estimation.sh sample.mp4 GPU CPU json
```

## Sample Output

The sample:

* prints the full `gst-launch-1.0` pipeline before execution
* renders detections when using display output modes, with the mean depth for each detected object attached as the ROI classification label
* writes per-object `depth_metrics` tensors as json-lines to `output.json` when using JSON output modes, containing center depth, mean depth, median depth, minimum depth, maximum depth, standard deviation, valid pixel count, and valid pixel ratio

## See also

* [Samples overview](../../../README.md)