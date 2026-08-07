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

### Options

| Option | Description | Default |
|---|---|---|
| `--dls-only` | Run DL Streamer only (skip DeepStream) | both platforms |
| `--ds-only` | Run DeepStream only (skip DL Streamer) | both platforms |
| `--dls-fps-threshold=N` | Minimum acceptable per-stream FPS for DL Streamer | `30` |
| `--ds-fps-threshold=N` | Minimum acceptable per-source FPS for DeepStream | `30` |
| `--measure-seconds=N` | Measurement duration per round, in seconds (positive integer) | `20` |

### Examples

Benchmark both platforms (first Deep Leearning Streamer, next DeepStream):
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
  `--measure-seconds` seconds before stopping and cleaning up.
- Any other answer skips the parallel run.
- The prompt appears only when at least one platform reached a result greater than `0`.
- A platform whose maximum is `0` is skipped in the parallel run.

## Execution Flow

### Overall script flow

```mermaid
flowchart TD
    A([Start]) --> D0["Source utils.sh<br/>Defines helpers, pipeline builders, welcome / usage"]
    D0 --> B["detect_intel_devices_for_docker<br/>/dev/dri → DEVICE_DRI, /dev/accel → DEVICE_ACCEL"]
    B --> C["determine_source_dls / determine_source_ds<br/>SOURCE + EXTRA_INPUT_VOLUME from INPUT_DLS / INPUT_DS"]
    C --> C2["detect_preferred_intel_render_device<br/>prefer dGPU over iGPU → INTEL_RENDER_DEVICE"]
    C2 --> D["Build DLSTREAMER_DOCKER / DEEPSTREAM_DOCKER<br/>(main script; embeds DEVICE_* / EXTRA_INPUT_VOLUME_*)"]
    D --> E["Parse arguments<br/>RUN_DLS / RUN_DS, FPS thresholds, --measure-seconds"]
    E --> F[Validate input arguments]
    F --> G["Detect hardware<br/>lspci → INTEL_GPU / NVIDIA_GPU<br/>lscpu → INTEL_CPU"]
    G --> H{Models present?}
    H -- DLS missing --> I["Download DL Streamer models<br/>benchmark_dls_download container"]
    H -- DS missing --> J["Download DeepStream TAO models<br/>benchmark_ds_download container"]
    I --> N{RUN_DLS?}
    J --> N
    H -- all present --> N
    N -- yes --> O[run_phase dls]
    N -- no --> P{RUN_DS?}
    O --> P
    P -- yes --> Q[run_phase ds]
    P -- no --> R[Print BENCHMARK RESULTS]
    Q --> R
    R --> T{Run in parallel?<br/>y/N prompt}
    T -- yes --> U["run_parallel_max_streams<br/>DLS_MAX_STREAMS + DS_MAX_STREAMS"]
    T -- no --> S([End])
    U --> S
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
    A([run_phase platform]) --> F[streams = 1]
    F --> G[run_one_round platform streams]
    G --> H["_finish_round<br/>collect FPS"]
    H --> I{FPS < threshold?}
    I -- yes --> J[Report max = streams - 1]
    J --> E([Return])
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
    G --> J["Sleep MEASURE_SECONDS<br/>live FPS displayed"]
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

> **Note:** `utils.sh` is sourced first (it only defines functions). The device/source detection steps then run before the `DLSTREAMER_DOCKER` / `DEEPSTREAM_DOCKER` command strings are declared in the main script, because those strings embed `${DEVICE_DRI}`, `${DEVICE_ACCEL}` and `${EXTRA_INPUT_VOLUME_DLS}` / `${EXTRA_INPUT_VOLUME_DS}` by value at declaration time. The pipeline builders in `utils.sh` expand `${SOURCE_DLS}` / `${SOURCE_DS}` when called, not at source time.

