# Concurrent usage of DL Streamer and DeepStream.

This tutorial explains how to simultaneously run DL Streamer and DeepStream on a single machine for optimal performance.

### Overview
Systems equipped with both NVIDIA GPUs and Intel hardware (GPU/NPU/CPU) can achieve enhanced performance by distributing workloads across available accelerators. Rather than relying solely on DeepStream for pipeline execution, you can offload additional processing tasks to Intel accelerators, maximizing system resource utilization.

A Python script (concurrent_dls_and_ds.py) is provided to facilitate this concurrent setup. It assumes that Docker and Python are properly installed and configured. The Ubuntu 24.04 is currently the only supported operating system.

## How it works

1. Using intel/dlstreamer:2025.2.0-ubuntu24 image, the sample downloads yolov8_license_plate_detector and ch_PP-OCRv4_rec_infer models to \./public directory if they were not downloaded yet.
2. Using nvcr.io/nvidia/deepstream:8.0-samples-multiarch image it downloads deepstream_tao_apps repository to \./deepstream_tao_apps directory. Then downloads models for License Plate Recognition (LPR), makes a custom library and copies dict.txt to the current directory, in case deepstream_tao_apps does not exist.
3. Hardware detection depending on setup
- Run pipeline simultaneously on both devices for:
  - both Nvidia and Intel GPUs
  - Nvidia GPU and Intel NPU
  - Nvidia GPU with Intel CPU
- Run pipeline directly per device for:
  - Intel GPU
  - Nvidia GPU
  - Intel NPU
  - Intel CPU

## How to use

```sh
python3 ./concurrent_dls_and_ds.py <input> LPR <output>
```

- Input can be rtsp, https or file.
- License Plate Recognition (LPR) is currently the only pipeline supported.
- Output is the filename. For example parameter: Output.mp4 or Output will create files Output_dls.mp4 (DL Streamer output) and/or Output_ds.mp4 (DeepStream output). 

## Notes

First-time download of the Docker images and models could take a longer time.
