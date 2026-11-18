# Coexistence Benchmark

Copyright (C) 2026 Intel Corporation  
SPDX-License-Identifier: MIT

## Overview

`coexistance_benchmark.sh` is a benchmark script that measures the potential maximum number of concurrent video analytics streams that can be executed on a system equipped with a combination of Intel and NVIDIA hardware.

The script automatically detects available hardware and runs the appropriate inference pipeline:

| Detected hardware | Pipeline used |
|---|---|
| Intel GPU | DL Streamer (via VA-API) |
| Intel NPU | DL Streamer (via NPU backend) |
| Intel CPU | DL Streamer (via OpenCV backend) |
| NVIDIA GPU | DeepStream |
| Intel GPU + NVIDIA GPU | Both simultaneously |
| Intel NPU + NVIDIA GPU | Both simultaneously |
| Intel CPU + NVIDIA GPU | Both simultaneously |

The use case is **License Plate Recognition (LPR)**:
- **DL Streamer pipeline**: YOLOv8 license plate detector → PP-OCRv4 text recognizer
- **DeepStream pipeline**: TrafficCamNet (vehicle detection) → LPDNet (plate detection) → LPRNet (plate recognition)

These are example model pairs for the benchmark. The pipeline uses these model paths/configurations; changing models also requires updating the corresponding pipeline configuration. Both pipelines run inside Docker containers.

## Requirements

- Docker installed and running
- At least one of:
  - Intel GPU with VA-API support (Intel Arc, Iris Xe, etc.)
  - Intel NPU (`/dev/accel`)
  - Intel CPU
  - NVIDIA GPU with CUDA support
- `lspci`, `lscpu` available on the host
- `DISPLAY` environment variable set (X11 forwarding for Docker)
- Prepare models compatible with the selected platform before running the benchmark. The LPR model paths below are examples for the current pipelines; substituting models requires updating their pipeline paths/configuration. Remote input streams and pulling container images may also require internet access.

## Files

| File | Description |
|---|---|
| `coexistance_benchmark.sh` | Main benchmark script (device/source detection, Docker command strings, argument parsing, benchmark loop, optional parallel run) |
| `utils.sh` | Shared helper functions: GStreamer pipeline builders, hardware/source detection, input validation, live FPS monitor, diagnostics, cleanup, and usage/printing helpers |

## Usage

```bash
./coexistance_benchmark.sh <INPUT_DLS> <INPUT_DS> LPR [OPTIONS]
```

### Arguments

| Argument | Description | Example |
|---|---|---|
| `<INPUT_DLS>` | Input video for DL Streamer — local file path, `rtsp://` URL, or `https://` URL | `video_dls.mp4` |
| `<INPUT_DS>` | Input video for DeepStream — local file path, `rtsp://` URL, or `https://` URL | `video_ds.mp4` |
| `LPR` | Pipeline mode — currently only `LPR` is supported | `LPR` |

`<INPUT_DLS>`/`<INPUT_DS>` must not contain `"`, `` ` ``, `$(`, `;`, or newline characters — these are rejected to prevent shell injection when the input is embedded in the generated pipeline command.

### Options

| Option | Description | Default |
|---|---|---|
| `--dls-only` | Run DL Streamer only (skip DeepStream) | both platforms |
| `--ds-only` | Run DeepStream only (skip DL Streamer) | both platforms |
| `--dls-fps-threshold=N` | Minimum acceptable per-stream FPS for DL Streamer | `30` |
| `--ds-fps-threshold=N` | Minimum acceptable per-source FPS for DeepStream | `30` |
| `--dls-start-streams=N` | Initial stream count for the DL Streamer benchmark | `1` |
| `--ds-start-streams=N` | Initial stream count for the DeepStream benchmark | `1` |
| `--dls-decode-chain=1\|2` | DL Streamer decode chain: `1` uses `parsebin ! vah264dec ! vapostproc`; `2` uses `decodebin3 ! vapostproc` | `1` |
| `--measure-seconds=N` | Measurement duration per round, in seconds (positive integer) | `20` |

### Examples

Benchmark both platforms (first DL Streamer, then DeepStream):
```bash
./coexistance_benchmark.sh video_dls.mp4 video_ds.mp4 LPR
```

DL Streamer only:
```bash
./coexistance_benchmark.sh video_dls.mp4 video_ds.mp4 LPR --dls-only
```

Custom FPS threshold:
```bash
./coexistance_benchmark.sh video_dls.mp4 video_ds.mp4 LPR --dls-fps-threshold=30
```

Custom measurement duration per round:
```bash
./coexistance_benchmark.sh video_dls.mp4 video_ds.mp4 LPR --measure-seconds=30
```

Start the benchmark at a higher stream count:
```bash
./coexistance_benchmark.sh video_dls.mp4 video_ds.mp4 LPR --dls-start-streams=2 --ds-start-streams=2
```

Use the alternative DL Streamer decode chain:
```bash
./coexistance_benchmark.sh video_dls.mp4 video_ds.mp4 LPR --dls-decode-chain=2
```

RTSP stream:
```bash
./coexistance_benchmark.sh rtsp://192.168.1.10:8554/stream rtsp://192.168.1.10:8554/stream LPR
```

### Logging output to a file

To save everything displayed on screen to a log file while still seeing it in the terminal:
```bash
./coexistance_benchmark.sh video_dls.mp4 video_ds.mp4 LPR 2>&1 | tee <LOG_FILE.LOG>
```

### Running without arguments

Running the script with no arguments skips the benchmark and instead reports whether
the DL Streamer and DeepStream model files are present, then prints usage and exits:
```bash
./coexistance_benchmark.sh
```
```text
Model availability check:
	 DL Streamer models: available
	 DeepStream models : missing
```

### Selecting an Intel GPU

When more than one Intel GPU is present, the script lists them and prompts for a
choice (defaults to the discrete GPU, if any, when run non-interactively or with no
selection). Set the `INTEL_GPU_RENDER_DEVICE` environment variable (e.g.
`/dev/dri/renderD129`) to pick a device without the interactive prompt.

## Model Preparation

The benchmark does not download models or clone model repositories. Prepare the models for each platform you plan to benchmark before launching it.

### DL Streamer models

The example DL Streamer pipeline uses `yolov8_license_plate_detector` and `ch_PP-OCRv4_rec_infer`. Prepare compatible models with the general [`download_other_models.py`](../../../../scripts/download_models/README.md) helper; its README describes setup and supported models. Replacing these models may require updating the pipeline paths and configuration.

| Model | Purpose | Saved to |
|---|---|---|
| `yolov8_license_plate_detector` | Detects license plates in the frame | `./public/yolov8_license_plate_detector/FP32/` |
| `ch_PP-OCRv4_rec_infer` | Recognizes text on detected plates | `./public/ch_PP-OCRv4_rec_infer/FP32/` |

### DeepStream / TAO models

Clone [deepstream_tao_apps](https://github.com/NVIDIA-AI-IOT/deepstream_tao_apps) into `./deepstream_tao_apps/` and prepare these model files using NVIDIA's TAO model setup instructions:

| Model | Purpose | Saved to |
|---|---|---|
| `resnet18_trafficcamnet_pruned.onnx` | Vehicle detection (primary detector) | `./deepstream_tao_apps/models/trafficcamnet/` |
| `resnet18_trafficcamnet_pruned_int8.txt` | TrafficCamNet calibration data | `./deepstream_tao_apps/models/trafficcamnet/` |
| `LPDNet_usa_pruned_tao5.onnx` | License plate detection (secondary) | `./deepstream_tao_apps/models/LPD_us/` |
| `usa_cal_10.1.0.bin`, `usa_lpd_label.txt` | LPDNet calibration and labels | `./deepstream_tao_apps/models/LPD_us/` |
| `us_lprnet_baseline18_deployable.onnx` | License plate recognition | `./deepstream_tao_apps/models/LPR_us/` |
| `labels_us.txt` | LPRNet labels file | `./deepstream_tao_apps/models/LPR_us/` |

The checked-out repository must also include `apps/tao_others/deepstream_lpr_app/dict_us.txt` and the sample configuration files.

The sample checks for these files before starting. When the DeepStream branch is enabled, it builds the custom LPR parser from the checked-out source and copies `dict_us.txt` to `./dict.txt`; it does not download model files.

## Output

The benchmark uses `fakesink` output — it measures pure inference/decode throughput.

## Optional Parallel Run

After the benchmark finishes and prints the results, the script asks whether to run
both platforms simultaneously using the maximum sustainable stream counts that were
found (`DLS_MAX_STREAMS` and `DS_MAX_STREAMS`):

```text
Run DL Streamer (N) and DeepStream (M) containers in parallel now? [y/N]
```

- Answering `y` / `Y` launches one DL Streamer container and one DeepStream container
  concurrently, each at its found maximum, and displays a live FPS view for both for
  180 seconds before stopping and cleaning up.
- Any other answer skips the parallel run.
- The prompt appears only when at least one platform reached a result greater than `0`.
- A platform whose maximum is `0` is skipped in the parallel run.



> **Note:** Startup order: input arguments are validated first (or, with no arguments, model
> availability is reported and the script exits) before anything hardware-related runs.
> Only then are Intel devices detected, the Intel GPU selection prompt shown, and the
> `DLSTREAMER_DOCKER` / `DEEPSTREAM_DOCKER` command strings declared — those strings embed
> `${DEVICE_DRI}`, `${DEVICE_ACCEL}` and `${EXTRA_INPUT_VOLUME_DLS}` / `${EXTRA_INPUT_VOLUME_DS}`
> by value at declaration time, so they must come after device/source detection and GPU
> selection. The pipeline builders in `utils.sh` expand `${SOURCE_DLS}` / `${SOURCE_DS}` when
> called, not at source time.

