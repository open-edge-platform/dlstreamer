# Benchmark Sample (Windows)

Sample `benchmark.bat` demonstrates [gvafpscounter](../../../../docs/user-guide/elements/gvafpscounter.md) element used to measure overall performance of video analytics pipelines on Windows.

The sample outputs last and average FPS (Frames Per Second) every second and overall FPS on exit.

## How It Works
The sample builds GStreamer pipeline containing video decode, inference and other IO elements, or multiple (N) identical pipelines if number of channels parameter is set to N>1.

The `gvafpscounter` inserted at the end of each stream pipeline and measures FPS across all streams.

The command-line parameters allow to select inference device (ex, CPU, GPU).

> **NOTE**: `benchmark.bat` requires the `centerface` face-detection model, loaded from `%MODELS_PATH%\public\centerface\FP32\centerface.xml`. Prepare it with `download_other_models.py` (see [Model Conversion Scripts](../../../../scripts/download_models/README.md)). The models used to validate this sample are listed in [Validation](#validation).

## Environment Variables

This sample requires the following environment variable to be set:
- `MODELS_PATH`: Path to the models directory

Example:
```batch
set MODELS_PATH=C:\models
```

## Input video
Use any local video file supported by the installed GStreamer plugins. For useful face detections, choose a clip containing people or faces. The example filename below is illustrative; obtain a suitable video from a source available to you and pass its local path. An optional example is [`head-pose-face-detection-female-and-male.mp4`](https://raw.githubusercontent.com/intel-iot-devkit/sample-videos/master/head-pose-face-detection-female-and-male.mp4).

## Running

```batch
benchmark.bat VIDEO_FILE [INFERENCE_DEVICE] [CHANNELS_COUNT]
```

The sample takes one to three command-line parameters:
1. **VIDEO_FILE** (required) - path to input video file
2. **INFERENCE_DEVICE** (optional) - device for inference, could be any device supported by OpenVINO™ toolkit
    * CPU (Default)
    * GPU
3. **CHANNELS_COUNT** (optional) - number of simultaneous streams to benchmark (default: 1)

### Example
```batch
benchmark.bat C:\videos\faces.mp4 CPU 4
```

## Sample Output

The sample
* prints gst-launch command line into console
* reports FPS every second and average FPS on exit

## Validation

This sample was validated with:

- Model: `centerface` (FP32)
- Video: [`head-pose-face-detection-female-and-male.mp4`](https://github.com/intel-iot-devkit/sample-videos/raw/master/head-pose-face-detection-female-and-male.mp4)

## See also
* [Windows Samples overview](../../../README.md)
* [Linux Benchmark Samples](../../../gstreamer/benchmark/README.md)
