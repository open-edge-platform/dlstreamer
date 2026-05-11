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
* `gvametaconvert` and `gvametapublish` for writing depth tensors to JSON output
* `gvafpscounter` for FPS measurement

The shell script keeps detector and depth-estimator devices independent so you can reproduce pipelines such as GPU detection with GPU depth estimation, or GPU detection with CPU depth estimation.

## Models

This sample expects two OpenVINO IR models:

* `yolo11n` for object detection
* `Depth-Anything-V2-Small-hf` for depth estimation

Use the model conversion scripts in [scripts/download_models](../../../../scripts/download_models/README.md) to prepare both models.

Example commands:

```sh
export MODELS_PATH="$HOME/models"
cd /opt/intel/dlstreamer/scripts/download_models

python download_ultralytics_models.py \
  --model yolo11n.pt \
  --outdir "${MODELS_PATH}/public/yolo11n/FP16"

python download_hf_models.py \
  --model depth-anything/Depth-Anything-V2-Small-hf \
  --outdir "${MODELS_PATH}/public/Depth-Anything-V2-Small-hf"
```

The sample resolves the common Ultralytics export layout automatically. If you export models elsewhere, set `DETECTION_MODEL` and `DEPTH_MODEL` to the full `.xml` paths before running the sample.

## Running

```sh
export MODELS_PATH="$HOME/models"
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
   * `json` - write depth tensors for the first frame to `output.json`
   * `display-and-json` - render annotated video and write `output.json`

Example runs:

```sh
# GPU detection + GPU depth estimation
./depth_estimation.sh

# GPU detection + CPU depth estimation with JSON output
./depth_estimation.sh \
  /home/labrat/data/4517676-hd_1280_720_30fps.mp4 \
  GPU \
  CPU \
  json
```

## Sample Output

The sample:

* prints the full `gst-launch-1.0` pipeline before execution
* renders detections when using display output modes
* writes per-object depth tensors to `output.json` when using JSON output modes

## See also

* [Samples overview](../../README.md)