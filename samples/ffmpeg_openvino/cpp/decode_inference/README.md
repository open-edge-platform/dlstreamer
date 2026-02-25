# Sample

The sample implements basic pipeline
```
FFmpeg input -> FFmpeg VAAPI decode -> OpenVINO™ inference
```
directly calling FFmpeg/libav and OpenVINO™ interfaces, without any dependency on Deep Learning Streamer (DL Streamer).
This sample demonstrates zero-copy buffer sharing FFmpeg->VAAPI->OpenVINO™, but it's not
optimized for performance. For more optimized version with multi-stream and batching support
please refer to sample `decode_resize_inference`.

## Build and Run

If you are building dlstreamer from source, add "-DENABLE_SAMPLES=ON" to the cmake command line when you are building dlstreamer, and run the resulting ffmpeg_openvino_decode_inference binary as:

```
./intel64/Debug/bin/ffmpeg_openvino_decode_inference -i input_video.mp4 -m model_file.xml
```

If you are building stand alone samples with dlstreamer installed from .deb packages or as a docker image, you can build the sample from source and run it using

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