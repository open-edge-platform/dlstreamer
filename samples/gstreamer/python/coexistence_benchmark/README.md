# Coexistence Benchmark

Copyright (C) 2026 Intel Corporation  
SPDX-License-Identifier: MIT

## Overview

`coexistance_benchmark.sh` is a benchmark script that measures the (potantial) maximum number of concurrent video analytics streams that can be executed on a system equipped with a combination of Intel and NVIDIA hardware.

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

Both pipelines run inside Docker containers.

## Requirements

- Docker installed and running
- At least one of:
  - Intel GPU with VA-API support (Intel Arc, Iris Xe, etc.)
  - Intel NPU (`/dev/accel`)
  - Intel CPU
  - NVIDIA GPU with CUDA support
- `lspci`, `lscpu` available on the host
- `DISPLAY` environment variable set (X11 forwarding for Docker)
- Internet access for first-run model download

## Files

| File | Description |
|---|---|
| `coexistance_benchmark.sh` | Main benchmark script |
| `utils.sh` | Shared variables (Docker commands, GStreamer pipeline definitions, validation functions) |

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

### Options

| Option | Description | Default |
|---|---|---|
| `--dls-only` | Run DL Streamer only (skip DeepStream) | both platforms |
| `--ds-only` | Run DeepStream only (skip DL Streamer) | both platforms |
| `--dls-fps-threshold=N` | Minimum acceptable per-stream FPS for DL Streamer | `30` |
| `--ds-fps-threshold=N` | Minimum acceptable per-source FPS for DeepStream | `30` |
| `--dls-streams=N` | Run exactly N streams on DL Streamer (skip benchmark loop) | benchmark mode |
| `--ds-streams=N` | Run exactly N streams on DeepStream (skip benchmark loop) | benchmark mode |

### Examples

Benchmark both platforms (first Deep Leearning Streamer, next DeepStream):
```bash
./coexistance_benchmark.sh video_dls.mp4 video_ds.mp4 LPR
```

DL Streamer only:
```bash
./coexistance_benchmark.sh video_dls.mp4 video_ds.mp4 LPR --dls-only
```

Fixed streams (both platforms simultaneously, no benchmark loop):
```bash
./coexistance_benchmark.sh video_dls.mp4 video_ds.mp4 LPR --dls-streams=5 --ds-streams=3
```

Custom FPS threshold:
```bash
./coexistance_benchmark.sh video_dls.mp4 video_ds.mp4 LPR --dls-fps-threshold=30
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

## First-run Setup (Automatic)

On first run the script automatically downloads all required models inside Docker containers. This requires an internet connection and may take several minutes.

Download containers use dedicated names (`benchmark_dls_download`, `benchmark_ds_download`) separate from benchmark containers.

### DL Streamer models (downloaded when Intel hardware detected)

| Model | Purpose | Saved to |
|---|---|---|
| `yolov8_license_plate_detector` | Detects license plates in the frame | `./public/` |
| `ch_PP-OCRv4_rec_infer` | Recognizes text on detected plates | `./public/` |

Downloaded via `intel/dlstreamer:latest` container.

### DeepStream / TAO models (downloaded when NVIDIA GPU detected)

Clones [deepstream_tao_apps](https://github.com/NVIDIA-AI-IOT/deepstream_tao_apps) into `./deepstream_tao_apps/` and downloads:

| Model | Purpose | Saved to |
|---|---|---|
| `resnet18_trafficcamnet_pruned.onnx` | Vehicle detection (primary detector) | `./deepstream_tao_apps/models/trafficcamnet/` |
| `LPDNet_usa_pruned_tao5.onnx` | License plate detection (secondary) | `./deepstream_tao_apps/models/LPD_us/` |
| `us_lprnet_baseline18_deployable.onnx` | License plate recognition | `./deepstream_tao_apps/models/LPR_us/` |

Also compiles the custom LPR parser plugin (`nvinfer_custom_lpr_parser`) inside the container using `make`.

Downloaded via `nvcr.io/nvidia/deepstream:8.0-samples-multiarch` container.

## Output

Default benchmark mode uses `fakesink` output — the benchmark measures pure inference/decode throughput.

## Execution Flow

### Overall script flow

```mermaid
flowchart TD
    A([Start]) --> B["Detect /dev/dri → DEVICE_DRI<br/>Detect /dev/accel → DEVICE_ACCEL<br/>Detect Intel GPU: prefer dGPU over iGPU"]
    B --> C["Determine SOURCE element from INPUT_DLS / INPUT_DS"]
    C --> D["Source utils.sh<br/>Builds DLSTREAMER_DOCKER, DEEPSTREAM_DOCKER,<br/>pipeline builder functions"]
    D --> E["Parse arguments<br/>Set RUN_DLS / RUN_DS<br/>Set thresholds and fixed streams"]
    E --> F[Validate input arguments]
    F --> G["Detect hardware<br/>lspci → INTEL_GPU / NVIDIA_GPU<br/>lscpu → INTEL_CPU"]
    G --> H{Models present?}
    H -- DLS missing --> I["Download DL Streamer models<br/>benchmark_dls_download container"]
    H -- DS missing --> J["Download DeepStream TAO models<br/>benchmark_ds_download container"]
    I --> K{Both DLS_FIXED_STREAMS<br/>and DS_FIXED_STREAMS set?}
    J --> K
    H -- all present --> K
    K -- yes --> L["Parallel fixed mode<br/>Both pipelines simultaneously"]
    K -- no --> M[Sequential mode]
    M --> N{RUN_DLS?}
    N -- yes --> O[run_phase dls]
    N -- no --> P{RUN_DS?}
    O --> P
    P -- yes --> Q[run_phase ds]
    P -- no --> R[Print BENCHMARK RESULTS]
    Q --> R
    L --> R
    R --> S([End])
```

### Hardware detection and Docker setup

```mermaid
flowchart TD
    A([Start hardware detection]) --> B{/dev/dri exists?}
    B -- yes --> C["DEVICE_DRI = --device /dev/dri<br/>--group-add render_group"]
    B -- no --> D[DEVICE_DRI = empty]
    C --> E{/dev/accel exists?}
    D --> E
    E -- yes --> F["DEVICE_ACCEL = --device /dev/accel<br/>--group-add accel_group"]
    E -- no --> G[DEVICE_ACCEL = empty]
    F --> H["Scan /dev/dri/render* for Intel vendor 0x8086<br/>Prefer dGPU over iGPU<br/>→ INTEL_RENDER_DEVICE"]
    G --> H
    H --> I["Override DEVICE_DRI with full /dev/dri<br/>card* nodes required by iHD VA-API driver<br/>GST_VA_DRM_DEVICE = INTEL_RENDER_DEVICE"]
    I --> J(["Done → DLSTREAMER_DOCKER / DEEPSTREAM_DOCKER built"])
```

### run_phase — benchmark loop

```mermaid
flowchart TD
    A([run_phase platform]) --> B{fixed_streams set?}
    B -- yes --> C["Fixed mode:<br/>run exactly N streams<br/>wait for natural completion"]
    C --> D["_finish_round<br/>collect FPS and status"]
    D --> E([Return])
    B -- no --> F[streams = 1]
    F --> G[run_one_round platform streams]
    G --> H["_finish_round<br/>collect FPS"]
    H --> I{FPS < threshold?}
    I -- yes --> J[Report max = streams - 1]
    J --> E
    I -- no --> K[streams++]
    K --> G
```

### run_one_round — single measurement round

```mermaid
flowchart TD
    A(["run_one_round platform streams"]) --> B["Build pipeline string<br/>build_dls_pipeline_no_encode<br/>build_ds_pipeline_no_encode"]
    B --> C["Start Docker container<br/>benchmark_dls_N or benchmark_ds_N"]
    C --> D["wait_for_pipeline_start<br/>wait for FpsCounter / PERF / New clock"]
    D --> E{Startup OK?}
    E -- error/OOM --> F["Abort round<br/>return error"]
    E -- ok --> G["Start live FPS monitor<br/>background subshell<br/>reads FpsCounter last lines"]
    G --> H{nowait mode?}
    H -- yes --> I(["Return to caller<br/>caller waits for natural end"])
    H -- no --> J["Sleep MEASURE_SECONDS = 45s<br/>live FPS displayed"]
    J --> K{DS engine build detected?}
    K -- yes --> L[Extend wait up to 420s]
    L --> M["kill live monitor<br/>_finish_round"]
    K -- no --> M
    M --> N(["Return ROUND_FPS / ROUND_STATUS"])
```

### FPS measurement — get_avg_fps

```mermaid
flowchart TD
    A(["get_avg_fps logfile platform streams"]) --> B{platform?}
    B -- dls --> C["grep FpsCounter average lines<br/>extract per-stream= values<br/>tail -n streams → last N values<br/>one final cumulative average per counter"]
    B -- ds --> D["grep PERF lines<br/>extract values in parentheses"]
    C --> E{values empty?}
    D --> E
    E -- yes --> F[return 0]
    E -- no --> G{platform?}
    G -- dls --> H["awk: return minimum<br/>slowest stream sets the limit"]
    G -- ds --> I["awk: return average<br/>across all sources"]
    H --> J([return FPS])
    I --> J
```

> **Note:** Steps detecting `/dev/dri`, `/dev/accel` and INPUT sources must happen before sourcing `utils.sh` because `utils.sh` expands `${DEVICE_DRI}`, `${DEVICE_ACCEL}`, and `${SOURCE_INTEL}` / `${SOURCE_NVIDIA}` at source time when building Docker command strings and pipeline definitions.

