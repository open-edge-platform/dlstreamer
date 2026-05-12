# Multi-Stream Processing Sample (gst-launch command line)

This README documents the Windows `multi_stream.ps1` script, demonstrating parallel processing of multiple video streams with DL Streamer.

## How It Works

The script constructs a multi-stream GStreamer pipeline using `gst-launch-1.0` that processes multiple video streams in parallel. 

The script now supports **two operation modes**:

### Dual-Device Mode (New, Linux-Compatible)
Process 4 streams (2+2) across different devices with different models:
- **Streams 1-2**: Use Device1 + Model1 (share model instance `inf0`)
- **Streams 3-4**: Use Device2 + Model2 (share model instance `inf1`)
- **Use case**: Offload work across CPU+GPU, compare models, maximize throughput

### Legacy Single-Device Mode (Backward Compatible)
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

The sample supports YOLO detection models:
- **yolox-tiny** - Very fast, lower accuracy
- **yolox_s** - Balanced speed/accuracy
- **yolov7** - Accurate, moderate speed
- **yolov8s** (default) - Latest YOLO, excellent balance
- **yolov9c** - Advanced architecture
- **yolo11s** - Newest YOLO variant
- **yolo26s** - Latest generation

> **NOTE**: Before running samples (including this one), run script `download_public_models.bat` once (the script located in `samples` top folder) to download all models required for this and other samples.

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
| -InputSource | DEFAULT | Input source (default: sample video URL) |
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
| -InputSource | DEFAULT | Video file path or URI |
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
.\multi_stream.ps1 -DeviceStream12 CPU -DeviceStream34 GPU -Model1 yolov8s -Model2 yolov9c
```

This creates 4 output MP4 files with detections overlaid.

#### 2. Hybrid CPU+GPU Deployment (Same Model)

Balance load between CPU and GPU for optimal throughput:
```PowerShell
.\multi_stream.ps1 -DeviceStream12 CPU -DeviceStream34 GPU -Model1 yolov8s -Model2 yolov8s -OutputType json
```

All 4 streams use YOLOv8s, but streams 1-2 run on CPU and 3-4 on GPU. Detection results saved to JSON files.

#### 3. All Streams on GPU

Process all 4 streams on GPU:
```PowerShell
.\multi_stream.ps1 -DeviceStream12 GPU -DeviceStream34 GPU -Model1 yolov8s -Model2 yolov8s
```

#### 4. Model Comparison Test

Compare YOLOv8s vs YOLOv9c performance on GPU:
```PowerShell
.\multi_stream.ps1 -DeviceStream12 GPU -DeviceStream34 GPU -Model1 yolov8s -Model2 yolov9c -OutputType fps -FrameLimiter " ! identity eos-after=1000"
```

#### 5. Custom Video Input

Process your own video file:
```PowerShell
.\multi_stream.ps1 -InputSource "C:\videos\traffic.mp4" -DeviceStream12 CPU -DeviceStream34 GPU -Model1 yolov8s -Model2 yolov9c
```

#### 6. NPU Device Support

Process all 4 streams on NPU:
```PowerShell
.\multi_stream.ps1 -DeviceStream12 NPU -DeviceStream34 NPU -Model1 yolov8s -Model2 yolov8s -OutputType json
```

Hybrid NPU+GPU deployment:
```PowerShell
.\multi_stream.ps1 -DeviceStream12 NPU -DeviceStream34 GPU -Model1 yolov8s -Model2 yolov9c -OutputType fps
```

### Legacy Mode Examples

#### 1. Basic Multi-Stream (Default Settings)

Process 4 streams with default settings:
```PowerShell
$env:MODELS_PATH = "C:\models"
.\multi_stream.ps1 -Device CPU -Model yolov8s -NumStreams 4
```

Creates `multi_stream_1.mp4` through `multi_stream_4.mp4`.

#### 2. Performance Testing

Test throughput with FPS counter only (no file output):
```PowerShell
.\multi_stream.ps1 -Device GPU -NumStreams 4 -OutputType fps
```

Displays real-time FPS for each stream.

#### 3. GPU Acceleration

Run 2 streams on GPU:
```PowerShell
.\multi_stream.ps1 -Device GPU -Model yolov8s -NumStreams 2 -OutputType file
```

#### 4. JSON Metadata Output

Process 4 streams and save detection results to JSON:
```PowerShell
.\multi_stream.ps1 -Device CPU -NumStreams 4 -OutputType json
```

This creates `multi_stream_1.json`, `multi_stream_2.json`, etc., and combines them into `output.json`.

#### 5. Custom Model

Use YOLOv9 with FP32 precision:
```PowerShell
.\multi_stream.ps1 -Device GPU -Model yolov9c -Precision FP32 -NumStreams 2
```

#### 6. Limited Frames for Testing

Process first 1000 frames per stream:
```PowerShell
.\multi_stream.ps1 -Device CPU -NumStreams 4 -FrameLimiter " ! identity eos-after=1000" -OutputType fps
```

#### 7. Custom Video Input

Process your own video file:
```PowerShell
.\multi_stream.ps1 -InputSource "C:\videos\traffic.mp4" -Device CPU -NumStreams 4 -OutputType file
```

#### 8. Maximum Streams

Test with 8 parallel streams:
```PowerShell
.\multi_stream.ps1 -Device GPU -NumStreams 8 -OutputType fps
```

## Output Types

### file (Video Files)

Creates separate MP4 files for each stream:
- Dual-device mode: `multi_stream_1.mp4`, `multi_stream_2.mp4`, `multi_stream_3.mp4`, `multi_stream_4.mp4`
- Legacy mode: `multi_stream_1.mp4` through `multi_stream_N.mp4` (where N is NumStreams)

Each file includes:
- Original video
- Detection bounding boxes (via gvawatermark)
- H.264 encoding for efficient storage
  - **CPU**: Uses x264enc software encoder
  - **GPU**: Uses mfh264enc hardware encoder for better performance

**Use case**: Visual verification, archival, demo videos

### json (Metadata)

Creates JSON files with detection results:
- Individual files: `multi_stream_1.json`, `multi_stream_2.json`, etc.
- Combined file: `output.json` (all streams merged)

JSON format (json-lines: one object per line):
```json
{"frame_id":10,"timestamp":1234567890,"objects":[{"detection":{"bounding_box":{"x_min":100,"y_min":50,"x_max":200,"y_max":150},"confidence":0.98,"label":"person"}}]}
{"frame_id":11,"timestamp":1234567923,"objects":[{"detection":{"bounding_box":{"x_min":105,"y_min":52,"x_max":205,"y_max":152},"confidence":0.97,"label":"person"}}]}
```

Each line contains:
- `frame_id`: Frame sequence number
- `timestamp`: Timestamp in nanoseconds
- `objects`: Array of detected objects with bounding boxes, confidence scores, and labels

**Use case**: Analytics, post-processing, database insertion

### fps (Performance Metrics)

Displays FPS (frames per second) for each stream:
```
Stream 1: 45.2 fps
Stream 2: 44.8 fps
Stream 3: 45.0 fps
Stream 4: 44.5 fps
```

No file output, minimal overhead.

## Pipeline Architecture

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

## See also

* [Windows Samples overview](../../../README.md)
* [Linux Multi-Stream Sample](../../../../gstreamer/gst_launch/multi_stream/README.md)
