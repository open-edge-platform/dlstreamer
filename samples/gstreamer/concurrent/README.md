# Concurrent use of DL Streamer and DeepStream

This sample detects hardware and runs pipelines using DL Streamer and/or DeepStream.

## How it works

1. Using intel/dlstreamer:2025.2.0-ubuntu24 image it downloads yolov8_license_plate_detector and ch_PP-OCRv4_rec_infer models to \./public directory if they were not downloaded yet.
2. Using nvcr.io/nvidia/deepstream:8.0-samples-multiarch image it downloads deepstream_tao_apps repository to \./deepstream_tao_apps then it download models for LPR, makes custom library and copy dict.txt to current directory if deepstream_tao_apps does not exist.
3. It detects hardware:
- if Nvidia GPU exists and Intel GPU exists then run pipeline simultaneously on both devices.
- else if Nvidia GPU exists and Intel NPU exists then run pipeline simultaneously on both devices.
- else if Nvidia GPU exists and Intel CPU exists then run pipeline simultaneously on both devices.
- else if Intel GPU exists then run on this device.
- else if Nvidia GPU exists then run on this device.
- else if Intel NPU exists then run on this device.
- else if Intel CPU exists then run on this device.

## Usage

```sh
./concurrent_dls_and_ds.sh <input> LPR <output>
```

- Input can be rtsp, https or file.
- LPR is standing for License Plate Recognition and currently it is the only pipeline supported.
- Output is the filename. For example parameter: Output.mp4 or Output will create files Output_dls.mp4 (DL Streamer output) and/or Output_ds.mp4 (DeepStream output). 

## Notes

For the first time downloading Docker images and models could take a long time.
