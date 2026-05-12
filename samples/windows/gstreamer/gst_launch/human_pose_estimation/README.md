# Human Pose Estimation Sample (Windows)

This sample demonstrates human pose estimation using the `gvaclassify` element with full-frame inference on Windows.

## How It Works

The sample builds a GStreamer pipeline using:
- `filesrc` or `urisourcebin` for input from file/URL
- `decodebin3` for video decoding
- `gvaclassify` with `inference-region=full-frame` for human pose estimation
- `gvawatermark` for visualization of pose keypoints
- `d3d11convert` for D3D11-accelerated video conversion
- `autovideosink` for rendering output video to screen

## Models

The sample uses the following pre-trained model from OpenVINO™ Toolkit [Open Model Zoo](https://github.com/openvinotoolkit/open_model_zoo):
- **human-pose-estimation-0001** - Single person pose estimation network

> **NOTE**: Before running this sample, run `download_omz_models.bat` once (located in `samples\windows` folder) to download all required models.

## Environment Variables

```PowerShell
$set MODELS_PATH = "C:\models"
```

Model should be located at:
- `%MODELS_PATH%\intel\human-pose-estimation-0001\FP32\human-pose-estimation-0001.xml`
## Running

```PowerShell
.\human_pose_estimation.ps1 [-InputSource <path>] [-Device <device>] [-OutputType <type>] [-JsonFile <file>] [-FrameLimiter <element>]
```

Parameters:
- **-InputSource** - Input source (default: `https://github.com/intel-iot-devkit/sample-videos/raw/master/face-demographics-walking.mp4`)
  - Local file path (e.g., `C:\videos\video.mp4`)
  - URL (e.g., `https://...`)
- **-Device** - Device for inference (default: `CPU`)
  - Supported: `CPU`, `GPU`, `NPU`
- **-OutputType** - Output type (default: `display`)
  - `display` - Show video with pose overlay
  - `file` - Save to MP4 file
  - `fps` - Benchmark mode (no display)
  - `json` - Export metadata to JSON
  - `display-and-json` - Show video and save JSON
- **-JsonFile** - JSON output filename (default: `output.json`)
- **-FrameLimiter** - Optional GStreamer element to insert after decode (default: empty)
  - Example: `" ! identity eos-after=1000"` - Process only first 1000 frames
  - Example: `" ! identity eos-after=500"` - Process only first 500 frames
  - Useful for testing/benchmarking with limited frame count

## Examples

### Use default settings (GitHub video, CPU, display)
```PowerShell
.\human_pose_estimation.ps1
```

### Display with CPU inference
```PowerShell
.\human_pose_estimation.ps1 -InputSource "C:\videos\walking.mp4" -Device CPU -OutputType display
```

### Save to file with GPU inference
```PowerShell
.\human_pose_estimation.ps1 -InputSource "C:\videos\walking.mp4" -Device GPU -OutputType file
```

### Export metadata to JSON
```PowerShell
.\human_pose_estimation.ps1 -InputSource "C:\videos\walking.mp4" -Device CPU -OutputType json -JsonFile pose_results.json
```

### Process only first 1000 frames (for testing)
```PowerShell
.\human_pose_estimation.ps1 -InputSource "C:\videos\long_video.mp4" -Device CPU -OutputType json -FrameLimiter " ! identity eos-after=1000"
```

### Benchmark FPS on NPU
```PowerShell
.\human_pose_estimation.ps1 -InputSource "C:\videos\walking.mp4" -Device NPU -OutputType fps
```

## Pipeline Architecture

**Note**: Pipeline varies based on device type. GPU/NPU use D3D11 hardware acceleration (`d3d11convert`, `d3d11videosink`, `d3d11h264enc`), while CPU uses software path (`videoconvert`, `autovideosink`, `openh264enc`).

```mermaid
graph LR
    A[filesrc/urisourcebin] --> B[decodebin3]
    B --> C[gvaclassify]
    C --> D[queue]
    D --> E{OUTPUT}
    
    E -->|display<br/>GPU/NPU| F1[queue]
    F1 --> F2[d3d11convert]
    F2 --> F3[gvawatermark]
    F3 --> F4[videoconvert]
    F4 --> F5[gvafpscounter]
    F5 --> F6[d3d11videosink]
    
    E -->|display<br/>CPU| F1C[queue]
    F1C --> F2C[gvawatermark]
    F2C --> F3C[videoconvert]
    F3C --> F4C[gvafpscounter]
    F4C --> F5C[autovideosink]
    
    E -->|file<br/>GPU/NPU| G1[queue]
    G1 --> G2[d3d11convert]
    G2 --> G3[gvawatermark]
    G3 --> G4[gvafpscounter]
    G4 --> G5[d3d11h264enc]
    G5 --> G6[h264parse]
    G6 --> G7[mp4mux]
    G7 --> G8[filesink]
    
    E -->|file<br/>CPU| G1C[queue]
    G1C --> G2C[videoconvert]
    G2C --> G3C[gvawatermark]
    G3C --> G4C[gvafpscounter]
    G4C --> G5C[openh264enc]
    G5C --> G6C[h264parse]
    G6C --> G7C[mp4mux]
    G7C --> G8C[filesink]
    
    E -->|fps| H1[queue]
    H1 --> H2[gvafpscounter]
    H2 --> H3[fakesink]
    
    E -->|json| I1[queue]
    I1 --> I2[gvametaconvert]
    I2 --> I3[gvametapublish]
    I3 --> I4[fakesink]
    
    E -->|display-and-json<br/>GPU/NPU| J1[queue]
    J1 --> J2[d3d11convert]
    J2 --> J3[gvawatermark]
    J3 --> J4[gvametaconvert]
    J4 --> J5[gvametapublish]
    J5 --> J6[videoconvert]
    J6 --> J7[gvafpscounter]
    J7 --> J8[d3d11videosink]
    
    E -->|display-and-json<br/>CPU| J1C[queue]
    J1C --> J2C[gvawatermark]
    J2C --> J3C[gvametaconvert]
    J3C --> J4C[gvametapublish]
    J4C --> J5C[videoconvert]
    J5C --> J6C[gvafpscounter]
    J6C --> J7C[autovideosink]
```

## Notes

- Windows-specific elements:
  - Uses `d3d11convert` for GPU acceleration (instead of Linux `vapostproc`)
  - Uses `d3d11h264enc` for video encoding (instead of Linux `vah264enc`)
  - Preprocessing backend: `opencv` for CPU, `d3d11` for GPU/NPU
- The `sync=false` property in video sink runs pipeline as fast as possible
- For real-time playback, change to `sync=true`

## See also
* [Windows Samples overview](../../../README.md)
* [Linux Human Pose Estimation Sample](../../../../gstreamer/gst_launch/human_pose_estimation/README.md)
