# Sample

The sample implements pipeline
```
FFmpeg input -> FFmpeg VAAPI decode -> VAAPI resize -> OpenVINO™ inference
```
with the following performance optimizations for GPU
1. multi-stream input and decode
2. async inference (multiple inference requests)
3. batched inference

Deep Learning Streamer (DL Streamer) C++ element `ffmpeg_multi_source` is used for first 3 stages of pipeline, the inference
stage implemented by directly calling OpenVINO™ API 2.0.  

## Build and Run

If you are building dlstreamer from source, add "-DENABLE_SAMPLES=ON" to the cmake command line when you are building dlstreamer, and run the resulting ffmpeg_openvino_decode_inference binary as:

```
./intel64/Debug/bin/ffmpeg_openvino_decode_inference -i input_video.mp4 -m model_file.xml
```

If you are building stand alone samples with dlstreamer installed from .deb packages or as a docker image, you can build and run it using

```sh
source /opt/intel/openvino_2026/setupvars.sh
source /opt/intel/dlstreamer/gstreamer/setupvars.sh
./build_and_run.sh INPUT_VIDEO [MODEL]
```

Alternatively, you can build it using CMake from the CMakeLists.txt file in this directory.

> **NOTE**: You may need the following dependencies to build the sample:
```sh
sudo apt install cmake make build-essential libgflags-dev
```