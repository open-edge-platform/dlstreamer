# Vehicle and Pedestrian Tracking Sample (Windows)

This sample demonstrates object detection, tracking, and classification pipeline on Windows using DL Streamer.

## How It Works

The sample builds a GStreamer pipeline using:
- `filesrc` or `urisourcebin` for input
- `decodebin3` for video decoding
- `gvadetect` for object detection (person, vehicle, bike)
- `gvatrack` for object tracking across frames
- `gvaclassify` for vehicle-type classification
- `gvawatermark` for visualization
- `d3d12convert` for GPU-accelerated video processing (GPU/NPU/AUTO)

The pre-processing backend is `d3d12-surface-sharing` on GPU and `d3d12` on NPU and AUTO (surface sharing needs a GPU device), and `opencv` on CPU.

## Models

The sample uses the following models:
- **yolo26s** - Object detection
- **dima806_vehicle_10_types_image_detection** - Vehicle-type classification

> **NOTE**: Download models using `scripts/download_models` helpers.

## Environment Variables

```PowerShell
$set MODELS_PATH = 'C:\models'
```

Models should be located at:
- `%MODELS_PATH%\public\yolo26s\FP16\yolo26s.xml`
- `%MODELS_PATH%\public\dima806_vehicle_10_types_image_detection\FP16\dima806_vehicle_10_types_image_detection.xml`

## Running

```PowerShell
.\vehicle_pedestrian_tracking.ps1 [-InputSource <path>] [-DetectionInterval <interval>] [-Device <device>] [-OutputType <type>] [-TrackingType <type>] [-JsonFile <file>] [-FrameLimiter <element>]
```

Parameters:
- **-InputSource** - Input source (default: `https://github.com/intel-iot-devkit/sample-videos/raw/master/person-bicycle-car-detection.mp4`)
  - Local file path (e.g., `C:\videos\traffic.mp4`)
  - URL (e.g., `https://...`)
- **-DetectionInterval** - Detection frequency (default: `3`)
  - `1` = detect every frame (highest accuracy, slowest)
  - `3` = detect every 3rd frame (tracking fills gaps)
  - Higher value = better performance, lower accuracy
- **-Device** - Inference device (default: `AUTO`)
  - Supported: `AUTO`, `CPU`, `GPU`, `GPU.0`, `NPU`
- **-OutputType** - Output type (default: `display`)
  - `display` - Display with sync
  - `display-async` - Display async (faster)
  - `fps` - Benchmark mode (no display)
  - `json` - Export metadata to JSON
  - `display-and-json` - Display and export
  - `file` - Save to MP4 file
- **-TrackingType** - Tracking algorithm (default: `short-term-imageless`)
  - `short-term-imageless` - Fast, appearance-free tracking
  - `zero-term` - Detection-only (no tracking)
  - `zero-term-imageless` - Simplified zero-term
- **-JsonFile** - JSON output filename (default: `output.json`)
- **-FrameLimiter** - Optional GStreamer element to insert after decode (default: empty)
  - Example: `" ! identity eos-after=1000"` - Process only first 1000 frames
  - Useful for testing/benchmarking with limited frame count

## Examples

### Use default settings (GitHub video, detect every 3rd frame, AUTO device, display)
```PowerShell
.\vehicle_pedestrian_tracking.ps1
```

### Detect every 3rd frame with tracking on GPU
```PowerShell
.\vehicle_pedestrian_tracking.ps1 -InputSource "C:\videos\traffic.mp4" -DetectionInterval 3 -Device GPU -OutputType display-async
```

### High-accuracy mode (detect every frame)
```PowerShell
.\vehicle_pedestrian_tracking.ps1 -InputSource "C:\videos\traffic.mp4" -DetectionInterval 1 -Device AUTO -OutputType display
```

### Export tracking data to JSON
```PowerShell
.\vehicle_pedestrian_tracking.ps1 -InputSource "C:\videos\traffic.mp4" -DetectionInterval 3 -Device GPU -OutputType json -TrackingType short-term-imageless -JsonFile tracking_results.json
```

### Performance benchmark (detect every 10 frames)
```PowerShell
.\vehicle_pedestrian_tracking.ps1 -InputSource "C:\videos\traffic.mp4" -DetectionInterval 10 -Device GPU -OutputType fps
```

### Different tracking algorithm
```PowerShell
.\vehicle_pedestrian_tracking.ps1 -InputSource "C:\videos\traffic.mp4" -DetectionInterval 1 -Device GPU -OutputType display -TrackingType zero-term
```

### Process only first 1000 frames (for testing)
```PowerShell
.\vehicle_pedestrian_tracking.ps1 -InputSource "C:\videos\traffic.mp4" -DetectionInterval 10 -Device GPU -OutputType json -FrameLimiter " ! identity eos-after=1000"
```

## Pipeline Architecture

**Note**: Pipeline varies based on device type. GPU/NPU/AUTO keep frames in D3D12 memory from the decoder to the sink (`d3d12convert`, `d3d12videosink`, `d3d12h264enc rate-control=cqp`), while CPU uses software path (`videoconvert`, `autovideosink`, `openh264enc`).

```mermaid
graph LR
    A[filesrc/urisourcebin] --> B[decodebin3]
    B --> C[queue]
    C --> D[gvadetect]
    D --> E[queue]
    E --> F[gvatrack]
    F --> G[queue]
  G --> H[gvaclassify<br/>vehicle type]
  H --> K[queue]
    K --> L{OUTPUT}
    
    L -->|file<br/>GPU/NPU/AUTO| M1[d3d12convert]
    M1 --> M2[gvawatermark]
    M2 --> M3[gvafpscounter]
    M3 --> M4[d3d12h264enc]
    M4 --> M5[h264parse]
    M5 --> M6[mp4mux]
    M6 --> M7[filesink]
    
    L -->|file<br/>CPU| M1C[videoconvert]
    M1C --> M2C[gvawatermark]
    M2C --> M3C[gvafpscounter]
    M3C --> M4C[openh264enc]
    M4C --> M5C[h264parse]
    M5C --> M6C[mp4mux]
    M6C --> M7C[filesink]
    
    L -->|display<br/>GPU/NPU/AUTO| N1[d3d12convert]
    N1 --> N2[gvawatermark]
    N2 --> N4[gvafpscounter]
    N4 --> N5[d3d12videosink]
    
    L -->|display<br/>CPU| N1C[gvawatermark]
    N1C --> N2C[videoconvert]
    N2C --> N3C[gvafpscounter]
    N3C --> N4C[autovideosink]
    
    L -->|display-async<br/>GPU/NPU/AUTO| O1[d3d12convert]
    O1 --> O2[gvawatermark]
    O2 --> O4[gvafpscounter]
    O4 --> O5[d3d12videosink<br/>sync=false]
    
    L -->|display-async<br/>CPU| O1C[gvawatermark]
    O1C --> O2C[videoconvert]
    O2C --> O3C[gvafpscounter]
    O3C --> O4C[autovideosink<br/>sync=false]
    
    L -->|fps| P1[gvafpscounter]
    P1 --> P2[fakesink]
    
    L -->|json| Q1[gvametaconvert]
    Q1 --> Q2[gvametapublish]
    Q2 --> Q3[fakesink]
    
    L -->|display-and-json<br/>GPU/NPU/AUTO| R1[d3d12convert]
    R1 --> R2[gvawatermark]
    R2 --> R3[gvametaconvert]
    R3 --> R4[gvametapublish]
    R4 --> R6[gvafpscounter]
    R6 --> R7[d3d12videosink<br/>sync=false]
    
    L -->|display-and-json<br/>CPU| R1C[gvawatermark]
    R1C --> R2C[gvametaconvert]
    R2C --> R3C[gvametapublish]
    R3C --> R4C[videoconvert]
    R4C --> R5C[gvafpscounter]
    R5C --> R6C[autovideosink<br/>sync=false]
```

## See also
* [Windows Samples overview](../../../../README.md)
* [Linux Vehicle and Pedestrian Tracking Sample](../../../../gstreamer/gst_launch/vehicle_pedestrian_tracking/README.md)
