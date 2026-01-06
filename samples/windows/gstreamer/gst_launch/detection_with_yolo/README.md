# Object Detection and Classification with Yolo models (Windows)

This sample demonstrates how to construct object detection pipelines via `gst-launch-1.0` command-line utility using YOLO models on Windows.

## How It Works

This sample utilizes GStreamer command-line tool `gst-launch-1.0` which can build and run GStreamer pipeline described in a string format.
The string contains a list of GStreamer elements separated by exclamation mark `!`, each element may have properties specified in the format `property`=`value`.

This sample builds GStreamer pipeline of the following elements:
* `filesrc` or `urisourcebin` for input from file/URL
* `decodebin3` for video decoding (automatically selects D3D11 decoder when available)
* [gvadetect](https://dlstreamer.github.io/elements/gvadetect.html) for full-frame object detection
* [gvawatermark](https://dlstreamer.github.io/elements/gvawatermark.html) for bounding boxes visualization
* `d3d11convert` for D3D11-accelerated video conversion
* `autovideosink` for rendering output video to screen

> **NOTE**: `sync=false` property in `autovideosink` element disables real-time synchronization so pipeline runs as fast as possible

## Windows-Specific Features

### D3D11 Pre-processing Backend

On Windows with GPU or NPU devices, this sample uses **D3D11 (Direct3D 11)** as the pre-processing backend instead of VA-API (Linux). This provides:
- Hardware-accelerated video decode via D3D11 decoders
- Zero-copy or efficient memory transfer between decode and inference
- Better performance on Intel GPUs with Windows drivers

### Pre-processing Backend Selection

| Device | Default Backend | Description |
|--------|-----------------|-------------|
| CPU | `ie` | OpenVINO Inference Engine |
| GPU | `d3d11` | Direct3D 11 acceleration |
| NPU | `d3d11` | Direct3D 11 acceleration |

## Models

The sample uses YOLO models from different repositories. The model preparation and conversion method depends on the model source.

For yolov5su, yolov8s (8n-obb, 8n-seg), yolov9c, yolov10s and yolo11s (yolo11s-seg, yolo11s-obb) models it is also necessary to install the ultralytics python package:

```batch
pip install ultralytics
```

### Supported Models

| Model | Model Preparation | Model pipeline (model-proc) |
|-------|-------------------|----------------------------|
| yolox-tiny | omz_downloader and omz_converter | gvadetect model-proc=yolo-x.json |
| yolox_s | Intel® OpenVINO™ model | gvadetect model-proc=yolo-x.json |
| yolov5s | Pytorch -> OpenVINO™ converter | gvadetect model-proc=yolo-v7.json |
| yolov5su | Ultralytics python exporter | gvadetect model-proc=yolo-v8.json |
| yolov7 | Pytorch -> ONNX -> OpenVINO™ | gvadetect model-proc=yolo-v7.json |
| yolov8s | Ultralytics python exporter | gvadetect (model-proc not needed) |
| yolov8n-obb | Ultralytics python exporter | gvadetect (model-proc not needed) |
| yolov8n-seg | Ultralytics python exporter | gvadetect (model-proc not needed) |
| yolov9c | Ultralytics python exporter | gvadetect (model-proc not needed) |
| yolov10s | Ultralytics python exporter | gvadetect (model-proc not needed) |
| yolo11s | Ultralytics python exporter | gvadetect (model-proc not needed) |
| yolo11s-seg | Ultralytics python exporter | gvadetect (model-proc not needed) |
| yolo11s-obb | Ultralytics python exporter | gvadetect (model-proc not needed) |
| yolo11s-pose | Ultralytics python exporter | gvadetect (model-proc not needed) |

## Environment Variables

This sample requires the following environment variable to be set:
- `MODELS_PATH`: Path to the models directory

Example:
```batch
set MODELS_PATH=C:\models
```

## Running

```batch
yolo_detect.bat [MODEL] [DEVICE] [INPUT] [OUTPUT] [PPBKEND] [PRECISION]
```

### Parameters

| Parameter | Default | Description |
|-----------|---------|-------------|
| MODEL | yolox_s | Model name (see supported models above) |
| DEVICE | GPU | Inference device: CPU, GPU, NPU |
| INPUT | Pexels video URL | Input video file or URL |
| OUTPUT | display | Output type: file, display, fps, json, display-and-json |
| PPBKEND | auto | Pre-processing backend: ie, opencv, d3d11 |
| PRECISION | INT8 | Model precision: INT8, FP32, FP16 |

### Examples

Run with default settings (yolox_s model, GPU, display output):
```batch
yolo_detect.bat
```

Run yolo11s model with GPU inference:
```batch
yolo_detect.bat yolo11s GPU
```

Run with local video file:
```batch
yolo_detect.bat yolo11s GPU C:\videos\test.mp4
```

Run with CPU and save to file:
```batch
yolo_detect.bat yolo11s CPU C:\videos\test.mp4 file
```

Run with explicit D3D11 backend:
```batch
yolo_detect.bat yolo11s GPU C:\videos\test.mp4 display d3d11
```

Run in FPS-only mode (benchmark):
```batch
yolo_detect.bat yolo11s GPU C:\videos\test.mp4 fps
```

Run with FP32 precision:
```batch
yolo_detect.bat yolo11s GPU C:\videos\test.mp4 display d3d11 FP32
```

### Output Types

| Output | Description |
|--------|-------------|
| `display` | Show video with detections on screen |
| `file` | Save video with detections to MP4 file |
| `fps` | Measure FPS only (no visualization) |
| `json` | Output detection metadata to output.json |
| `display-and-json` | Both display and JSON output |

## Device Restrictions

- **yolov10s**: Not supported on NPU
- **yolov10s on GPU**: Requires special configuration (automatically applied)

## Download Models

If you have not already downloaded the required models, do so before running the sample. Use the Linux download script. On Windows, you can run this script using WSL (Windows Subsystem for Linux) or Git Bash:

### Using WSL or Git Bash

```bash
# Set the models path (use a path accessible from Windows)
export MODELS_PATH=/mnt/c/models  # WSL path to C:\models

# Download a specific model (FP32 and FP16)
cd samples
./download_public_models.sh yolo11s

# For INT8 quantization (requires a calibration dataset)
./download_public_models.sh yolo11s coco128
```

For detailed instructions on downloading models, including the full list of supported models and quantization options, see the [Download Public Models Guide](../../../../../docs/source/dev_guide/download_public_models.md).

> **Note**: Make sure to set your `MODELS_PATH` environment variable in Windows to point to the same location where models were downloaded (e.g., `set MODELS_PATH=C:\models`).

## Sample Output

The sample:
* Prints the full gst-launch-1.0 command to the console
* Runs the pipeline and either:
  - Displays video with bounding boxes around detected objects
  - Saves output to MP4 file
  - Prints FPS metrics
  - Outputs JSON metadata

## See also
* [Windows Samples overview](../../../README.md)
* [Linux Detection with Yolo Sample](../../../../gstreamer/gst_launch/detection_with_yolo/README.md)
