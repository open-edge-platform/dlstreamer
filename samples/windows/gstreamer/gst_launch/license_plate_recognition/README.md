# License Plate Recognition Sample (Windows)

This sample demonstrates automatic license plate detection and OCR (Optical Character Recognition) on Windows.

## How It Works

The sample uses a two-stage cascaded inference pipeline:
1. **Detection Stage**: YOLOv8 detects license plates in video frames
2. **Recognition Stage**: PaddleOCR recognizes characters on detected plates

Pipeline elements:
- `filesrc` or `urisourcebin` for input
- `decodebin3` for video decoding
- `gvadetect` for license plate detection
- `gvaclassify` for OCR
- `gvawatermark` for visualization

## Models

- **yolov8_license_plate_detector** - YOLOv8-based license plate detector
- **ch_PP-OCRv4_rec_infer** - PaddlePaddle OCR V4 recognition model

> **NOTE**: Run `download_public_models.bat` before using this sample.

## Environment Variables

```PowerShell
$set MODELS_PATH = "C:\models"
```

Models should be located at:
- `%MODELS_PATH%\public\yolov8_license_plate_detector\FP32\yolov8_license_plate_detector.xml`
- `%MODELS_PATH%\public\ch_PP-OCRv4_rec_infer\FP32\ch_PP-OCRv4_rec_infer.xml`

## Running

```PowerShell
.\license_plate_recognition.ps1 [-InputSource <path>] [-Device <device>] [-OutputType <type>] [-JsonFile <file>] [-FrameLimiter <element>]
```

Parameters:
- **-InputSource** - Input source (default: `https://github.com/open-edge-platform/edge-ai-resources/raw/main/videos/ParkingVideo.mp4`)
  - Local file path (e.g., `C:\videos\parking.mp4`)
  - URL (e.g., `https://...`)
- **-Device** - Inference device (default: `GPU`)
  - Supported: `CPU`, `GPU`, `NPU`
- **-OutputType** - Output type (default: `fps`)
  - `display` - Show video with overlay (sync)
  - `display-async` - Show video with overlay (async, faster)
  - `fps` - Benchmark mode (no display)
  - `json` - Export metadata to JSON
  - `display-and-json` - Display and export
  - `file` - Save to MP4 file
- **-JsonFile** - JSON output filename (default: `output.json`)
- **-FrameLimiter** - Optional GStreamer element to insert after decode (default: empty)
  - Example: `" ! identity eos-after=1000"` - Process only first 1000 frames
  - Useful for testing/benchmarking with limited frame count

## Examples

### Use default settings (GitHub video, GPU, benchmark mode)
```PowerShell
.\license_plate_recognition.ps1
```

### Display with real-time visualization
```PowerShell
.\license_plate_recognition.ps1 -InputSource "C:\videos\parking.mp4" -Device GPU -OutputType display-async
```

### Export recognized plates to JSON
```PowerShell
.\license_plate_recognition.ps1 -InputSource "C:\videos\parking.mp4" -Device GPU -OutputType json -JsonFile plates.json
```

### Benchmark performance on CPU
```PowerShell
.\license_plate_recognition.ps1 -InputSource "C:\videos\parking.mp4" -Device CPU -OutputType fps
```

### Run on NPU with display
```PowerShell
.\license_plate_recognition.ps1 -InputSource "C:\videos\parking.mp4" -Device NPU -OutputType display-async
```

### Save annotated video
```PowerShell
.\license_plate_recognition.ps1 -InputSource "C:\videos\parking.mp4" -Device GPU -OutputType file
```

### Process only first 1000 frames (for testing)
```PowerShell
.\license_plate_recognition.ps1 -InputSource "C:\videos\parking.mp4" -Device GPU -OutputType json -FrameLimiter " ! identity eos-after=1000"
```

## Performance Tips

1. **Use GPU or NPU for best performance** - Hardware acceleration significantly speeds up both detection and OCR
2. **Use `display-async` instead of `display`** - Async mode improves frame rate
3. **Pre-process backend**:
   - CPU: Uses OpenCV (software)
   - GPU/NPU: Uses D3D11 (hardware accelerated)

## Pipeline Architecture

**Note**: Pipeline varies based on device type. GPU/NPU use D3D11 hardware acceleration (`d3d11convert`, `d3d11videosink`, `d3d11h264enc`), while CPU uses software path (`videoconvert`, `autovideosink`, `openh264enc`).

```mermaid
graph LR
    A[filesrc/urisourcebin] --> B[decodebin3]
    B --> C[queue]
    C --> D[gvadetect]
    D --> E[queue]
    E --> F[videoconvert]
    F --> G[gvaclassify]
    G --> H[queue]
    H --> I{OUTPUT}
    
    I -->|file<br/>GPU/NPU| J1[d3d11convert]
    J1 --> J2[gvawatermark]
    J2 --> J3[gvafpscounter]
    J3 --> J4[d3d11h264enc]
    J4 --> J5[h264parse]
    J5 --> J6[mp4mux]
    J6 --> J7[filesink]
    
    I -->|file<br/>CPU| J1C[videoconvert]
    J1C --> J2C[gvawatermark]
    J2C --> J3C[gvafpscounter]
    J3C --> J4C[openh264enc]
    J4C --> J5C[h264parse]
    J5C --> J6C[mp4mux]
    J6C --> J7C[filesink]
    
    I -->|display<br/>GPU/NPU| K1[d3d11convert]
    K1 --> K2[gvawatermark]
    K2 --> K3[videoconvert]
    K3 --> K4[gvafpscounter]
    K4 --> K5[d3d11videosink]
    
    I -->|display<br/>CPU| K1C[gvawatermark]
    K1C --> K2C[videoconvert]
    K2C --> K3C[gvafpscounter]
    K3C --> K4C[autovideosink]
    
    I -->|display-async<br/>GPU/NPU| L1[d3d11convert]
    L1 --> L2[gvawatermark]
    L2 --> L3[videoconvert]
    L3 --> L4[gvafpscounter]
    L4 --> L5[d3d11videosink<br/>sync=false]
    
    I -->|display-async<br/>CPU| L1C[gvawatermark]
    L1C --> L2C[videoconvert]
    L2C --> L3C[gvafpscounter]
    L3C --> L4C[autovideosink<br/>sync=false]
    
    I -->|fps| M1[gvafpscounter]
    M1 --> M2[fakesink]
    
    I -->|json| N1[gvametaconvert]
    N1 --> N2[gvametapublish]
    N2 --> N3[fakesink]
    
    I -->|display-and-json<br/>GPU/NPU| O1[d3d11convert]
    O1 --> O2[gvawatermark]
    O2 --> O3[gvametaconvert]
    O3 --> O4[gvametapublish]
    O4 --> O5[videoconvert]
    O5 --> O6[gvafpscounter]
    O6 --> O7[d3d11videosink<br/>sync=false]
    
    I -->|display-and-json<br/>CPU| O1C[gvawatermark]
    O1C --> O2C[gvametaconvert]
    O2C --> O3C[gvametapublish]
    O3C --> O4C[videoconvert]
    O4C --> O5C[gvafpscounter]
    O5C --> O6C[autovideosink<br/>sync=false]
```

## See also
* [Windows Samples overview](../../../README.md)
* [Linux License Plate Recognition Sample](../../../../gstreamer/gst_launch/license_plate_recognition/README.md)
