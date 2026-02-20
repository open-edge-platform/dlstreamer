# Concurrent Use of DL Streamer and DeepStream

This tutorial explains how to simultaneously run DL Streamer and DeepStream on a single machine for optimal performance.

## Overview

Systems equipped with both NVIDIA GPUs and Intel hardware (GPU/NPU/CPU) can achieve enhanced performance by distributing workloads across available accelerators. Rather than relying solely on DeepStream for pipeline execution, you can offload additional processing tasks to Intel accelerators, maximizing system resource utilization.

A Python script (`concurrent_dls_and_ds.py`) is provided to facilitate this concurrent setup. It assumes that Docker and Python are properly installed and configured. The Ubuntu 24.04 is currently the only supported operating system.

## How it works

1. Using the **intel/dlstreamer:2025.2.0-ubuntu24** image.

   The sample downloads `yolov8_license_plate_detector` and `ch_PP-OCRv4_rec_infer`
   models to `./public` directory if they were not downloaded yet.

2. Using the **nvcr.io/nvidia/deepstream:8.0-samples-multiarch** image.

   The sample downloads the `deepstream_tao_apps` repository to the `./deepstream_tao_apps`
   directory. Then, it downloads models for License Plate Recognition (LPR),
   makes a custom library and copies dict.txt to the current directory if `deepstream_tao_apps`
   does not exist.

3. Hardware detection depends on the setup.

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

- <input> can be an RTSP or HTTPS stream, or a file.
- License Plate Recognition (LPR) is currently the only supported pipeline.
- <output> is the filename. For example, the `Output.mp4` or `Output` parameters
  will create the `Output_dls.mp4` (DL Streamer output) and/or `Output_ds.mp4`
  (DeepStream output) files.

## Notes

First-time download of the Docker images and models may take a long time.
