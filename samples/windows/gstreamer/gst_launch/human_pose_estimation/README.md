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

```batch
$set MODELS_PATH=C:\models
```

Model should be located at:
- `%MODELS_PATH%\intel\human-pose-estimation-0001\FP32\human-pose-estimation-0001.xml`
## Running

```batch
.\human_pose_estimation.bat [INPUT] [DEVICE] [OUTPUT] [JSON_FILE]
```

Arguments:
- **INPUT** - Input source (default: `https://github.com/intel-iot-devkit/sample-videos/raw/master/face-demographics-walking.mp4`)
  - Local file path (e.g., `C:\videos\video.mp4`)
  - URL (e.g., `https://...`)
- **DEVICE** - Device for inference (default: `CPU`)
  - Supported: `CPU`, `GPU`, `NPU`
- **OUTPUT** - Output type (default: `display`)
  - `display` - Show video with pose overlay
  - `file` - Save to MP4 file
  - `fps` - Benchmark mode (no display)
  - `json` - Export metadata to JSON
  - `display-and-json` - Show video and save JSON
- **JSON_FILE** - JSON output filename (default: `output.json`)

## Examples

### Use default settings (GitHub video, CPU, display)
```batch
.\human_pose_estimation.bat
```

### Display with CPU inference
```batch
.\human_pose_estimation.bat "C:\videos\walking.mp4" CPU display
```

### Save to file with GPU inference
```batch
.\human_pose_estimation.bat "C:\videos\walking.mp4" GPU file
```

### Export metadata to JSON
```batch
.\human_pose_estimation.bat "C:\videos\walking.mp4" CPU json pose_results.json
```

### Benchmark FPS on NPU
```batch
.\human_pose_estimation.bat "C:\videos\walking.mp4" NPU fps
```

## Pipeline Architecture

```mermaid
graph LR
    A[filesrc/urisourcebin] --> B[decodebin3]
    B --> C[gvaclassify]
    C --> D[queue]
    D --> E{OUTPUT}
    
    E -->|display| F1[queue]
    F1 --> F2[d3d11convert]
    F2 --> F3[gvawatermark]
    F3 --> F4[videoconvert]
    F4 --> F5[gvafpscounter]
    F5 --> F6[autovideosink]
    
    E -->|file| G1[queue]
    G1 --> G2[d3d11convert]
    G2 --> G3[gvawatermark]
    G3 --> G4[gvafpscounter]
    G4 --> G5[d3d11h264enc]
    G5 --> G6[h264parse]
    G6 --> G7[mp4mux]
    G7 --> G8[filesink]
    
    E -->|fps| H1[queue]
    H1 --> H2[gvafpscounter]
    H2 --> H3[fakesink]
    
    E -->|json| I1[queue]
    I1 --> I2[gvametaconvert]
    I2 --> I3[gvametapublish]
    I3 --> I4[fakesink]
    
    E -->|display-and-json| J1[queue]
    J1 --> J2[d3d11convert]
    J2 --> J3[gvawatermark]
    J3 --> J4[gvametaconvert]
    J4 --> J5[gvametapublish]
    J5 --> J6[videoconvert]
    J6 --> J7[gvafpscounter]
    J7 --> J8[autovideosink]
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
