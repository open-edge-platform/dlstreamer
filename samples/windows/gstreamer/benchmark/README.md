# Benchmark Sample (Windows)

Sample `benchmark.bat` demonstrates [gvafpscounter](../../../../docs/source/elements/gvafpscounter.md) element used to measure overall performance of video analytics pipelines on Windows.

The sample outputs last and average FPS (Frames Per Second) every second and overall FPS on exit.

## How It Works
The sample builds GStreamer pipeline containing video decode, inference and other IO elements, or multiple (N) identical pipelines if number of channels parameter is set to N>1.

The `gvafpscounter` inserted at the end of each stream pipeline and measures FPS across all streams.

The command-line parameters allow to select inference device (ex, CPU, GPU).

> **NOTE**: Before running samples (including this one), run script `download_omz_models.bat` once (the script located in `samples\windows` folder) to download all models required for this and other samples.

## Environment Variables

This sample requires the following environment variable to be set:
- `MODELS_PATH`: Path to the models directory

Example:
```batch
set MODELS_PATH=C:\models
```

## Input video

You can download video file example by opening this URL in your browser:
```
https://raw.githubusercontent.com/intel-iot-devkit/sample-videos/master/head-pose-face-detection-female-and-male.mp4
```
or use any other media/video file.

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
benchmark.bat C:\videos\head-pose-face-detection-female-and-male.mp4 CPU 4
```

## Sample Output

The sample
* prints gst-launch command line into console
* reports FPS every second and average FPS on exit

## See also
* [Windows Samples overview](../../README.md)
* [Linux Benchmark Samples](../../../../gstreamer/benchmark/README.md)
