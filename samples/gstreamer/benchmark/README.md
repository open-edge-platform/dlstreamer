# Benchmark Samples

Samples `benchmark_one_model.sh` and `benchmark_two_models.sh` demonstrates [gvafpscounter](../../../docs/user-guide/elements/gvafpscounter.md) element used to measure overall performance of multi-stream and multi-process video analytics pipelines.

The sample outputs last and average FPS (Frames Per Second) every second and overall FPS on exit.

## How It Works
The sample builds GStreamer pipeline containing video decode, inference and other IO elements, or multiple (N) identical pipelines if number streams parameter set to N>1.

If number processes parameter set to M>1, then `N` streams will be split equally across `M` processes. On multi-socket Intel® Xeon® CPU system, processes will be split equally across number of sockets.

The `gvafpscounter` inserted at the end of each stream pipeline and measures FPS across all streams.

The command-line parameters allow to select decode and inference devices (ex, CPU, GPU).

> **NOTE**: Before running samples (including this one), run script `download_omz_models.sh` once (the script located in `samples` top folder) to download all models required for this and other samples.

## Input video

You can download video file example by command
```sh
curl https://raw.githubusercontent.com/intel-iot-devkit/sample-videos/master/head-pose-face-detection-female-and-male.mp4 --output /path/to/your/video/head-pose-face-detection-female-and-male.mp4
```
or use any other media/video file.

## Running

Benchmark video decode and inference on single model (using gvainference element):
```sh
./benchmark_one_model.sh VIDEO_FILE [MODEL_PATH] [DECODE_DEVICE] [INFERENCE_DEVICE] [NUMBER_STREAMS] [NUMBER_PROCESSES] [DECODE_ELEMENT] [INFERENCE_ELEMENT] [SINK_ELEMENT]
```

Benchmark video decode and inference on two models (object detection and object classification models) using gvadetect and gvaclassify elements:
```sh
./benchmark_two_models.sh VIDEO_FILE [MODEL1_PATH] [MODEL2_PATH] [DECODE_DEVICE] [INFERENCE_DEVICE] [NUMBER_STREAMS] [NUMBER_PROCESSES] [DECODE_ELEMENT] [INFERENCE1_ELEMENT] [INFERENCE2_ELEMENT] [SINK_ELEMENT]
```

The sample `benchmark_one_model.sh` takes one to eight command-line parameters (last eight are optional):
1. [VIDEO_FILE] to specify a path to input video file
2. [MODEL_PATH] to specify a path to model in OpenVINO™ toolkit IR format. Default is `face-detection-adas-0001` model in `INT8` precision.
3. [DECODE_DEVICE] to specify device for video decode, could be
    * CPU (Default)
    * GPU
4. [INFERENCE_DEVICE] to specify inference device, could be any device supported by OpenVINO™ toolkit
    * CPU (Default)
    * GPU
    * HDDL
    * ...
5. [NUMBER_STREAMS] number of simultaneous streams to benchmark
6. [NUMBER_PROCESSES] number of processes. If multiple processes, streams distributed equally across processes
7. [DECODE_ELEMENT] element (or pipeline of elements in gst-launch format) for demuxing and video decoding. Default is `decodebin3`
8. [INFERENCE_ELEMENT] DL Streamer pipeline element to perform the inference, default is `gvainference`
9. [SINK_ELEMENT] sink element (or pipeline of elements in gst-launch format), default is `fakesink async=false`

The sample `benchmark_one_model.sh` has similar parameters with additional parameter for second model.

## Sample Output

The sample
* prints gst-launch command line into console
* reports FPS every second and average FPS on exit

## See also
* [Samples overview](../README.md)
