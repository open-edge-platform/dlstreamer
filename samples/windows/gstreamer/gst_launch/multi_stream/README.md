# Multi-Stream Processing Sample (gst-launch command line)

This README documents the Windows `multi_stream.ps1` script, demonstrating parallel processing of multiple video streams with DL Streamer.

## How It Works

The script constructs a multi-stream GStreamer pipeline using `gst-launch-1.0` that processes multiple video streams in parallel. 

The script now supports **two operation modes**:

### Dual-Device Mode
Process 4 streams (2+2) across different devices with different models:
- **Streams 1-2**: Use Device1 + Model1 (share model instance `inf0`)
- **Streams 3-4**: Use Device2 + Model2 (share model instance `inf1`)
- **Use case**: Offload work across CPU+GPU, compare models, maximize throughput

### Legacy Single-Device Mode
Process N streams (1-8) on the same device with the same model:
- All streams use the same device and model
- Share single model instance (`shared_model`) for memory efficiency
- **Use case**: Simple multi-stream processing, performance testing

Key features:
- **Model instance sharing**: Streams on the same device share a single model instance, reducing memory footprint
- **Parallel execution**: GStreamer processes all streams concurrently
- **Independent queues**: Each stream has its own queue for optimal throughput
- **Flexible output**: File (MP4), JSON metadata, or FPS performance metrics

## Models

Use any video supported by the installed GStreamer plugins; the examples use `C:\videos\test.mp4`. The sample needs a compatible object-detection model in OpenVINO™ IR format; the script loads each model name passed with `-Model`, `-Model1` or `-Model2` from `%MODELS_PATH%\public\<model>\<precision>\<model>.xml`. A model-proc file is added automatically only for `yolox-tiny` (`yolo-x.json`); any other model must work with `gvadetect` without a model-proc file. Prepare models with the general helpers described in [Model Conversion Scripts](../../../../../scripts/download_models/README.md). The models used to validate this sample are listed in [Validation](#validation).

## Usage

### Dual-Device Mode

```PowerShell
.\multi_stream.ps1 [-InputSource <path>] [-DeviceStream12 <device>] [-DeviceStream34 <device>] [-Model1 <model>] [-Model2 <model>] [-Precision <precision>] [-OutputType <type>] [-FrameLimiter <element>]
```

### Legacy Single-Device Mode

```PowerShell
.\multi_stream.ps1 [-InputSource <path>] [-Device <device>] [-Model <model>] [-NumStreams <count>] [-Precision <precision>] [-OutputType <type>] [-FrameLimiter <element>]
```

## Parameters

### Dual-Device Mode Parameters

| Parameter | Default | Description |
|-----------|---------|-------------|
| -InputSource | (required) | Input video file path or URI |
| -DeviceStream12 | CPU | Device for stream 1 & 2: CPU, GPU, NPU |
| -DeviceStream34 | GPU | Device for stream 3 & 4: CPU, GPU, NPU |
| -Model1 | yolov8s | Model for stream 1 & 2 |
| -Model2 | yolov8s | Model for stream 3 & 4 |
| -Precision | FP16 | Model precision: FP32, FP16, INT8 |
| -OutputType | file | Output type: file, json, fps |
| -FrameLimiter | (empty) | Optional GStreamer element (e.g., ' ! identity eos-after=1000') |

### Legacy Mode Parameters

| Parameter | Default | Description |
|-----------|---------|-------------|
| -InputSource | (required) | Video file path or URI |
| -Device | CPU | Inference device: CPU, GPU, NPU |
| -Model | yolov8s | YOLO model name |
| -NumStreams | 4 | Number of parallel streams (1-8) |
| -Precision | FP16 | Model precision: FP32, FP16, INT8 |
| -OutputType | file | Output type: file, json, fps |
| -FrameLimiter | (empty) | Optional GStreamer element |

## Examples

### Dual-Device Mode Examples

#### 1. Two Models on Different Devices (CPU + GPU)

Process streams 1-2 on CPU with YOLOv8s, streams 3-4 on GPU with YOLOv9c:
```PowerShell
$env:MODELS_PATH = "C:\models"
.\multi_stream.ps1 -InputSource C:\videos\test.mp4 -DeviceStream12 CPU -DeviceStream34 GPU -Model1 yolov8s -Model2 yolov9c
```

This creates 4 output MP4 files with detections overlaid.

#### 2. Hybrid CPU+GPU Deployment (Same Model)

Balance load between CPU and GPU for optimal throughput:
```PowerShell
.\multi_stream.ps1 -InputSource C:\videos\test.mp4 -DeviceStream12 CPU -DeviceStream34 GPU -Model1 yolov8s -Model2 yolov8s -OutputType json
```

All 4 streams use YOLOv8s, but streams 1-2 run on CPU and 3-4 on GPU. Detection results saved to JSON files.

### Legacy Mode Examples

#### 1. Basic Multi-Stream (Default Settings)

Process 4 streams with default settings and a local input video:
```PowerShell
$env:MODELS_PATH = "C:\models"
.\multi_stream.ps1 -InputSource C:\videos\test.mp4 -Device CPU -Model yolov8s -NumStreams 4
```

Creates `multi_stream_1.mp4` through `multi_stream_4.mp4`.

#### 2. Performance Testing

Test throughput with FPS counter only (no file output):
```PowerShell
.\multi_stream.ps1 -InputSource C:\videos\test.mp4 -Device GPU -NumStreams 4 -OutputType fps
```

Displays real-time FPS for each stream.

#### 3. GPU Acceleration

Run 2 streams on GPU:
```PowerShell
.\multi_stream.ps1 -InputSource C:\videos\test.mp4 -Device GPU -Model yolov8s -NumStreams 2 -OutputType file
```

#### 4. JSON Metadata Output

Process 4 streams and save detection results to JSON:
```PowerShell
.\multi_stream.ps1 -InputSource C:\videos\test.mp4 -Device CPU -NumStreams 4 -OutputType json
```

This creates `multi_stream_1.json`, `multi_stream_2.json`, etc., and combines them into `output.json`.

## Output Types
- file (Video Files)
- json (Metadata)
- fps (Performance Metrics)
- Pipeline Architecture

### Dual-Device Mode (4 streams: 2+2)

```
Input Video → Stream 1: decode → gvadetect(Device1, Model1, inf0) → queue → output_1.mp4
           ↘ Stream 2: decode → gvadetect(Device1, Model1, inf0) → queue → output_2.mp4
           ↘ Stream 3: decode → gvadetect(Device2, Model2, inf1) → queue → output_3.mp4
           ↘ Stream 4: decode → gvadetect(Device2, Model2, inf1) → queue → output_4.mp4
```

**Key points:**
- Streams 1-2 share Device1's model instance (`model-instance-id=inf0`)
- Streams 3-4 share Device2's model instance (`model-instance-id=inf1`)
- Each stream has independent decode and queue
- Parallel execution across all streams

### Legacy Mode (N streams on single device)

```
Input Video → Stream 1: decode → gvadetect(Device, Model, shared_model) → queue → output_1.mp4
           ↘ Stream 2: decode → gvadetect(Device, Model, shared_model) → queue → output_2.mp4
           ↘ ...
           ↘ Stream N: decode → gvadetect(Device, Model, shared_model) → queue → output_N.mp4
```

All N streams use the same device and model, sharing one model instance.

## Validation

This sample was validated with:

- Model: `yolov8s` (FP16)
- Video: [`1192116-sd_640_360_30fps.mp4`](https://videos.pexels.com/video-files/1192116/1192116-sd_640_360_30fps.mp4)

## See also

* [Windows Samples overview](../../../../README.md)
* [Linux Multi-Stream Sample](../../../../gstreamer/gst_launch/multi_stream/README.md)
