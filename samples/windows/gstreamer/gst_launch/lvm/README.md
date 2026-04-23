# Image Embeddings Generation with ViT (gst-launch command line)

This sample demonstrates the use of Vision Transformer (ViT) in a pipeline constructed via `gst-launch-1.0` command-line utility. It allows the extraction of image embeddings (CLS tokens) for each frame using the Vision Transformer extracted from a CLIP model.

## How It Works

The sample utilizes GStreamer command-line tool `gst-launch-1.0` which can build and run a GStreamer pipeline described in a string format.
The string contains a list of GStreamer elements separated by an exclamation mark `!`, each element may have properties specified in the format `property=value`.

This sample builds a GStreamer pipeline of the following elements:

**Input**: `filesrc` or `urisourcebin` (automatically detected).
**Decoding**: `decodebin3` for hardware/software video decoding.
**Preprocessing**:
- **CPU**: `videoconvert` and `videoscale` (OpenCV backend).
- **GPU**: `d3d11convert` (D3D11 backend).
- **NPU**: `d3d11convert` (D3D11 backend).
**Inference**: `gvainference` running the CLIP Vision Transformer model.
**Metadata**: `gvametaconvert` (JSON with tensor data) and `gvametapublish` (file output).
**Sink**: `fakesink` (default) or `gvafpscounter` for performance measurement.

## Model
The sample uses the following models with OpenVINO™ format:
- [`clip-vit-large-patch14`](https://huggingface.co/openai/clip-vit-large-patch14)
- [`clip-vit-base-patch16`](https://huggingface.co/openai/clip-vit-base-patch16)
- [`clip-vit-base-patch32`](https://huggingface.co/openai/clip-vit-base-patch32)

## Pipeline Architecture
This pipeline extracts image embeddings using a Vision Transformer (ViT) from a CLIP model. After decoding, `gvainference` extracts the high-dimensional feature vectors (embeddings) from each frame. The data flow is determined by the `OUTPUT` parameter, supporting either JSON metadata extraction or FPS performance benchmarking.

```mermaid
graph LR
A[source] --> B[decodebin3]
    B --> C{DEVICE}

    C -->|CPU| D[videoconvert]
    D --> E[videoscale]
    E --> F[gvainference]

    C -->|GPU/NPU| G[d3d11convert]
    G --> F

    F --> H{OUTPUT mode}

    H -->|json| I[gvametaconvert]
    I --> J[gvametapublish]
    J --> K[fakesink]

    H -->|fps| L[gvafpscounter]
    L --> M[fakesink]
```

## Running
### Prerequisites
```PowerShell
$set MODELS_PATH = "C:\models"
```
### Command Line Arguments
```PowerShell
.\generate_frame_embedding.ps1 [-InputSource <path>] [-Device <device>] [-Precision <precision>] [-Model <model>] [-PreprocessBackend <backend>] [-OutputType <type>] [-FrameLimiter <element>]
```

The sample takes the following *optional* parameters:

| Parameter | Description | Default |
| :--- | :--- | :--- |
| -InputSource | Local file path or HTTPS URL | Pexels Video URL |
| -Device | `CPU`, `GPU`, or `NPU` | `CPU` |
| -Precision | `FP32`, `FP16`, or `INT8` | `FP32` |
| -Model | CLIP model name (see above) | `clip-vit-large-patch14` |
| -PreprocessBackend | `opencv` (CPU) or `d3d11` (GPU/NPU) | `opencv` for CPU; `d3d11` for GPU/NPU |
| -OutputType | `json` (save embeddings) or `fps` (benchmark) | `json` |
| -FrameLimiter | Optional GStreamer element to insert after decode | empty |

## Sample Output

The sample:

* prints the `gst-launch-1.0` full command line into the console
* starts the command and either publishes metadata to a file or prints out FPS if you set OUTPUT=fps

## See also

* [Windows Samples overview](../../../README.md)
* [Linux LVM Sample](../../../../gstreamer/gst_launch/lvm/README.md)

## Example Usage

Default execution (CPU, FP32, JSON output):
```PowerShell
.\generate_frame_embedding.ps1
```

High-performance GPU inference (Intel® Arc™ GPU, FP16):
```PowerShell
.\generate_frame_embedding.ps1 -InputSource "C:\you\video\path\sample.mp4" -Device GPU -Precision FP16 -Model clip-vit-large-patch14 -PreprocessBackend d3d11 -OutputType json
```

Benchmark throughput (FPS mode):
```PowerShell
.\generate_frame_embedding.ps1 -InputSource https://example.com/stream.mp4 -Device CPU -Precision FP32 -Model clip-vit-base-patch32 -PreprocessBackend opencv -OutputType fps
```

Process only first 1000 frames (for testing):
```PowerShell
.\generate_frame_embedding.ps1 -InputSource "C:\videos\sample.mp4" -Device GPU -OutputType json -FrameLimiter " ! identity eos-after=1000"
```
