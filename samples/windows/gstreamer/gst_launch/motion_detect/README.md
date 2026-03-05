# Motion Detect Sample (gst-launch command line)

This README documents the Windows `gvamotiondetect_demo.bat` script, a simple way to run the `gvamotiondetect` element in a GStreamer pipeline, additionally chaining `gvadetect` over motion ROIs.

## How It Works

The script builds and runs a GStreamer pipeline with `gst-launch-1.0`. It supports both GPU (VA surface memory) and CPU (system memory) paths and accepts a local file or URI source.

Key elements in the pipeline:
- `urisourcebin` or `filesrc` + `decodebin3`: input and decoding
- Caps filter: `video/x-raw(memory:VAMemory)` for GPU or `video/x-raw` for CPU
- `gvamotiondetect`: motion region detection (ROI publisher)
- `gvadetect`: runs object detection restricted to motion ROIs (`inference-region=1`)
- Output:
  - `gvametaconvert` + `gvametapublish`: write metadata to `output.json` (JSON Lines)
  - or `gvawatermark` + `vapostproc` + `d3d11videosink`: on-screen rendering with FPS counter

## Models

The sample uses YOLOv8n (resolved via `MODELS_PATH`) or other supported object detection model. The necessary conversion to the OpenVINO™ format can be performed by the `download_public_models.sh` script located in the `samples` directory.


## Usage

```bat
gvamotiondetect_demo.bat [--device GPU|CPU] [--source <video_or_uri>] ^
  [--model <path/to/model.xml>] [--precision FP32|FP16|INT8] ^
  [--output display|json] [--md-opts "prop1=val prop2=val"]
```

- `--device`: Choose `GPU` (VA memory path) or `CPU` (system memory). Default: `GPU`.
- `--source` / `--src`: Local file path or URI (e.g., `rtsp://`, `http://`, `https://`). Default: sample HTTPS video.
- `--model`: Path to OpenVINO XML model for `gvadetect`. If omitted, it uses `MODELS_PATH/public/yolov8n/<PRECISION>/yolov8n.xml`.
- `--precision`: Model precision subfolder (`FP32`, `FP16`, `INT8`). Default: `FP32`.
- `--output`: `json` (default) writes `output.json` or `display` shows annotated video with watermark.
- `--md-opts`: Extra properties for `gvamotiondetect`, space-separated (e.g., `"motion-threshold=0.07 min-persistence=2"`).

## Examples

- GPU path with default source and model, JSON output:
```bat
set MODELS_PATH=C:\models
gvamotiondetect_demo.bat --device GPU --output json
```
- CPU path with local file, display output, and custom motion detector options:
```bat
set MODELS_PATH=C:\models
gvamotiondetect_demo.bat --device CPU --source C:\path\to\video.mp4 --output display ^
  --md-opts "motion-threshold=0.07 min-persistence=2"
```

- Explicit model path on GPU:
```bat
gvamotiondetect_demo.bat --device GPU --model C:\models\public\yolov8n\FP32\yolov8n.xml --output display
```

## Motion Detector Options (`--md-opts`)

`--md-opts` lets you pass properties directly to the `gvamotiondetect` element. Provide them as a space-separated list in quotes:

```bat
--md-opts "motion-threshold=0.07 min-persistence=2"
```

- `motion-threshold`: Float in [0..1]. Sensitivity of motion detection; lower values detect smaller changes, higher values reduce false positives. Example: `0.05` (more sensitive) vs `0.10` (less sensitive).
- `min-persistence`: Integer ≥ 0. Minimum number of consecutive frames a region must persist to be reported as motion. Helps filter out transient noise.
- Other properties: You can pass any supported `gvamotiondetect` property the element exposes (e.g., ROI size or smoothing controls, if available in your build). Use `gst-inspect-1.0 gvamotiondetect` to list all properties and defaults.

Tip: Start with `motion-threshold=0.07` and `min-persistence=2`, then adjust based on scene noise and desired sensitivity.

## Output

- JSON mode: writes metadata to `output.json` (JSON Lines) and prints FPS via `gvafpscounter`.
- Display mode: overlays bounding boxes and labels using `gvawatermark` and renders via `d3d11videosink`.
