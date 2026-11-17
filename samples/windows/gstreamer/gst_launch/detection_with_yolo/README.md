# Object Detection and Classification with Yolo models (Windows)

This sample demonstrates how to construct object detection pipelines via `gst-launch-1.0` command-line utility using YOLO models on Windows.

## How It Works

This sample utilizes GStreamer command-line tool `gst-launch-1.0` which can build and run GStreamer pipeline described in a string format.
The string contains a list of GStreamer elements separated by exclamation mark `!`, each element may have properties specified in the format `property`=`value`.

This sample builds GStreamer pipeline of the following elements:

- `filesrc` or `urisourcebin` for input from file/URL
- `decodebin3` for video decoding (automatically selects the D3D12 hardware decoder when available)
- [gvadetect](https://docs.openedgeplatform.intel.com/dev/edge-ai-libraries/dlstreamer/elements/gvadetect.html) for full-frame object detection
- [gvawatermark](https://docs.openedgeplatform.intel.com/dev/edge-ai-libraries/dlstreamer/elements/gvawatermark.html) for bounding boxes visualization
- `d3d12convert` (or `d3d11convert`) for GPU-accelerated video conversion
- `d3d12videosink` (or `d3d11videosink`) for rendering output video to screen

> **NOTE**: `sync=false` property in the video sink element disables real-time synchronization so pipeline runs as fast as possible

## Pipeline Architecture

This pipeline performs object detection using a YOLO model. After decoding, `gvadetect` runs inference on each frame. The output branch is selected at startup via the `OUTPUT` parameter and determines how detected metadata and video are handled — from live display with overlays, to MP4 file recording, JSON metadata logging, FPS benchmarking, or a combined display-and-JSON mode.

The diagram shows the default GPU/NPU pipeline, which keeps frames in D3D12 memory from the decoder to the sink. The elements used with the other pre-processing backends are listed in [Pre-processing Backend Selection](#pre-processing-backend-selection).

```mermaid
graph LR
    A[source] --> B[decodebin3]
    B --> C[gvadetect: YOLO]
    C --> D{OUTPUT mode}

    D -->|display| E[d3d12convert]
    E --> F[gvawatermark]
    F --> G[gvafpscounter]
    G --> H[d3d12videosink]

    D -->|file| I[d3d12convert]
    I --> J[gvawatermark]
    J --> K[gvafpscounter]
    K --> L[d3d12h264enc]
    L --> M[mp4mux]
    M --> N[filesink]

    D -->|json| O[gvametaconvert]
    O --> P[gvametapublish]
    P --> Q[fakesink]

    D -->|fps| R[gvafpscounter]
    R --> S[fakesink]

    D -->|display-and-json| T[d3d12convert]
    T --> U[gvawatermark]
    U --> V[gvametaconvert]
    V --> W[gvametapublish]
    W --> X[gvafpscounter]
    X --> Y[d3d12videosink]
```

## Windows-Specific Features

### D3D11 and D3D12 Pre-processing Backends

On Windows with GPU or NPU devices, this sample uses **Direct3D** pre-processing backends instead of VA-API (Linux):

- `d3d12` / `d3d11`: the frame is resized and color-converted on the GPU, then passed to OpenVINO™ through system memory.
- `d3d12-surface-sharing` / `d3d11-surface-sharing`: the pre-processed frame is passed to the OpenVINO™ GPU plugin without leaving GPU memory. Supported on GPU only; on NPU the element falls back to `d3d12` / `d3d11`.

The D3D12 backends work directly on the frames from the D3D12 decoders. The D3D11 backends need the decoder to copy each frame into D3D11 memory.

### Pre-processing Backend Selection

| Device | Default Backend         |
| ------ | ----------------------- |
| CPU    | `opencv`                |
| GPU    | `d3d12-surface-sharing` |
| NPU    | `d3d12`                 |

The elements after `gvadetect` determine which memory type it receives, so the sample selects them from the backend. A `d3d12` backend accepts only D3D12 memory and a `d3d11` backend only D3D11 memory:

| Backend                          | Conversion     | Display          | File encoder                       |
| -------------------------------- | -------------- | ---------------- | ---------------------------------- |
| `d3d12`, `d3d12-surface-sharing` | `d3d12convert` | `d3d12videosink` | `d3d12h264enc rate-control=cqp` \* |
| `d3d11`, `d3d11-surface-sharing` | `d3d11convert` | `d3d11videosink` | `mfh264enc`                        |
| `ie`, `opencv`                   | `videoconvert` | `autovideosink`  | `openh264enc`                      |

\* Falls back to `d3d12download ! mfh264enc` when the GPU has no D3D12 video encoder. Constant QP rate control is used because the default rate control of `d3d12h264enc` produces broken streams with some drivers.

The D3D backends require a GPU or NPU device.

## Models

Use any video that the installed GStreamer plugins can decode. Choose a clip containing the objects you want to detect; examples below use `C:\videos\test.mp4`. The model can be any compatible detector from the supported list, prepared in the expected OpenVINO format. The examples use `yolox_s` and `yolo11s`; these are examples, not required model choices.

Prepare models with the general helpers documented in [`scripts/download_models/README.md`](../../../../../scripts/download_models/README.md): use `download_ultralytics_models.py` for Ultralytics models such as `yolo11s.pt`, `download_other_models.py` for `yolox-tiny`, `yolox_s`, and `yolov7`, or `download_hf_models.py` for supported Hugging Face models.

### Supported Models

| Model        | Model Preparation                | Model pipeline (model-proc)       |
| Model        | General model helper                                      | Model pipeline (model-proc)       |
| ------------ | --------------------------------------------------------- | --------------------------------- |
| yolox-tiny   | `download_other_models.py yolox-tiny`                    | gvadetect model-proc=yolo-x.json  |
| yolox_s      | `download_other_models.py yolox_s`                       | gvadetect model-proc=yolo-x.json  |
| yolov5nu     | `download_ultralytics_models.py --model yolov5nu.pt`      | gvadetect (model-proc not needed) |
| yolov7       | `download_other_models.py yolov7`                        | gvadetect model-proc=yolo-v7.json |
| yolov8s      | `download_ultralytics_models.py --model yolov8s.pt`       | gvadetect (model-proc not needed) |
| yolov8n-obb  | `download_ultralytics_models.py --model yolov8n-obb.pt`   | gvadetect (model-proc not needed) |
| yolov8n-seg  | `download_ultralytics_models.py --model yolov8n-seg.pt`   | gvadetect (model-proc not needed) |
| yolov9c      | `download_ultralytics_models.py --model yolov9c.pt`       | gvadetect (model-proc not needed) |
| yolov10s     | `download_ultralytics_models.py --model yolov10s.pt`      | gvadetect (model-proc not needed) |
| yolo11s      | `download_ultralytics_models.py --model yolo11s.pt`       | gvadetect (model-proc not needed) |
| yolo11s-seg  | `download_ultralytics_models.py --model yolo11s-seg.pt`   | gvadetect (model-proc not needed) |
| yolo11s-obb  | `download_ultralytics_models.py --model yolo11s-obb.pt`   | gvadetect (model-proc not needed) |
| yolo11s-pose | `download_ultralytics_models.py --model yolo11s-pose.pt`  | gvadetect (model-proc not needed) |

## Environment Variables

This sample requires the following environment variable to be set:

- `MODELS_PATH`: Path to the models directory

Example:

```PowerShell
$set MODELS_PATH = "C:\models"
```

## Running

```PowerShell
.\yolo_detect.ps1 [-Model <model>] [-Device <device>] [-InputSource <path>] [-OutputType <type>] [-PreprocessBackend <backend>] [-Precision <precision>] [-FrameLimiter <element>]
```

### Parameters

| Parameter          | Default          | Description                                                                             |
| ------------------ | ---------------- | --------------------------------------------------------------------------------------- |
| -Model             | yolox_s          | Model name (see supported models above)                                                 |
| -Device            | GPU              | Inference device: CPU, GPU, NPU                                                         |
| -InputSource       | (required)       | Input video file or URL                                                                 |
| -OutputType        | display          | Output type: file, display, fps, json, display-and-json                                 |
| -PreprocessBackend | per device       | Pre-processing backend: ie, opencv, d3d11, d3d11-surface-sharing, d3d12, d3d12-surface-sharing (see [Pre-processing Backend Selection](#pre-processing-backend-selection)) |
| -Precision         | FP16             | Model precision: INT8, FP32, FP16                                                       |
| -FrameLimiter      | (empty)          | Optional GStreamer element to insert after decode (e.g., `" ! identity eos-after=100"`) |

### Examples

Run with yolox_s, GPU inference, and display output:

```PowerShell
.\yolo_detect.ps1 -InputSource C:\videos\test.mp4
```

Run yolo11s model with GPU inference:

```PowerShell
.\yolo_detect.ps1 -Model yolo11s -Device GPU -InputSource C:\videos\test.mp4
```

Run with local video file:

```PowerShell
.\yolo_detect.ps1 -Model yolo11s -Device GPU -InputSource C:\videos\test.mp4
```

Run with CPU and save to file:

```PowerShell
.\yolo_detect.ps1 -Model yolo11s -Device CPU -InputSource C:\videos\test.mp4 -OutputType file
```

Run with the D3D11 surface-sharing backend instead of the default D3D12 one:

```PowerShell
.\yolo_detect.ps1 -Model yolo11s -Device GPU -InputSource C:\videos\test.mp4 -OutputType display -PreprocessBackend d3d11-surface-sharing
```

Run in FPS-only mode (benchmark):

```PowerShell
.\yolo_detect.ps1 -Model yolo11s -Device GPU -InputSource C:\videos\test.mp4 -OutputType fps
```

Run with FP32 precision:

```PowerShell
.\yolo_detect.ps1 -Model yolo11s -Device GPU -InputSource C:\videos\test.mp4 -OutputType display -Precision FP32
```

Process only first 100 frames (for testing/benchmarking):

```PowerShell
.\yolo_detect.ps1 -Model yolov8n-seg -Device CPU -InputSource C:\videos\test.mp4 -OutputType json -PreprocessBackend opencv -Precision FP32 -FrameLimiter " ! identity eos-after=100"
```

### Output Types

| Output             | Description                              |
| ------------------ | ---------------------------------------- |
| `display`          | Show video with detections on screen     |
| `file`             | Save video with detections to MP4 file   |
| `fps`              | Measure FPS only (no visualization)      |
| `json`             | Output detection metadata to output.json |
| `display-and-json` | Both display and JSON output             |

## Device Restrictions

- **yolov10s**: Not supported on NPU
- **yolov10s on GPU**: Requires special configuration (automatically applied)

## Download Models

For example, the sample commands use `yolo11s`; prepare it with the general `download_ultralytics_models.py` helper. Run the helper in the environment and with the output path described in [`scripts/download_models/README.md`](../../../../../scripts/download_models/README.md). On Windows, use an environment that can access the same model directory as `MODELS_PATH`.

> **Note**: Make sure to set your `MODELS_PATH` environment variable in Windows to point to the same location where models were downloaded (e.g., `set MODELS_PATH=C:\models`).

## Sample Output

The sample:

- Prints the full gst-launch-1.0 command to the console
- Runs the pipeline and either:
  - Displays video with bounding boxes around detected objects
  - Saves output to MP4 file
  - Prints FPS metrics
  - Outputs JSON metadata

## See also

- [Windows Samples overview](../../../../README.md)
- [Linux Detection with Yolo Sample](../../../../gstreamer/gst_launch/detection_with_yolo/README.md)

---

*Intel, the Intel logo, OpenVINO, and the OpenVINO logo are trademarks of Intel Corporation or its subsidiaries.*
