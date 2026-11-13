# BEVFusion 3D Detection with g3dbevfusion

This sample runs BEVFusion camera + LiDAR 3D object detection on [nuScenes](https://www.nuscenes.org/) scenes with `g3dbevfusion`, and renders the detections on the six cameras and a bird's-eye view with `g3drender`.

The sample has four scripts:

- `g3dbevfusion_prepare_model.sh <openvino-version>` prepares `bevfusion_ov_model_<openvino-version>/` from openvino_contrib's [`openvino_bevfusion`](https://github.com/openvinotoolkit/openvino_contrib/tree/master/modules/openvino_bevfusion) module. It:
  - builds the custom-op extensions against that OpenVINO release;
  - downloads the published mmdetection3d checkpoint and exports the OpenVINO IRs;
  - writes the element's `g3dbevfusion.json`.
- `g3dbevfusion_prepare_data.sh` downloads nuScenes `v1.0-mini` and converts its scenes into `nuscenes_mini_seq_12Hz/`.
- `nuscenes_to_sequence.py`, which `g3dbevfusion_prepare_data.sh` calls, turns nuScenes scenes into numbered per-sensor files for `multifilesrc`, so that frame N of every sensor is the same moment. It also writes the calibration and the ground-truth boxes. See [Sequence frames](#sequence-frames) for what goes into a frame.
- `g3dbevfusion.sh` runs the pipeline on a converted scene, in `fusion` (six cameras + LiDAR) or `lidar` mode.

## Prerequisites

- An Intel GPU: the voxelize and BEV-pool ops are GPU kernels.
- For `g3dbevfusion_prepare_model.sh`: `apt-get install git curl g++ python3-venv opencl-c-headers ocl-icd-opencl-dev`. The script installs CPU PyTorch, NumPy and `openvino` into a temporary Python virtual environment of its own.
- About 14 GB of free disk space while `g3dbevfusion_prepare_data.sh` runs: 4 GB for the nuScenes `v1.0-mini` download, which is removed at the end, and 11 GB for the converted scenes.

## Prepare

```bash
./g3dbevfusion_prepare_model.sh 2026.3.1
./g3dbevfusion_prepare_data.sh
```

Pass the full OpenVINO version of the DL Streamer release that will run the pipeline, on the host or in its container. The custom-op extensions only load into the OpenVINO release they were built against, so a model directory prepared for 2026.3.1 does not work with 2026.3.0 or 2026.4.0. The script needs no DL Streamer or OpenVINO environment: it builds the extensions against the `openvino` wheel of that version. For a DL Streamer built on an OpenVINO nightly, pass the matching pre-release wheel version, for example `2026.5.0.dev20261030`, with `PIP_EXTRA_INDEX_URL=https://storage.openvinotoolkit.org/simple/wheels/nightly`.

The model script checks out a fixed commit of [openvinotoolkit/openvino_contrib](https://github.com/openvinotoolkit/openvino_contrib) and applies the patches in [`patches/`](patches/) on top of it. They carry the openvino_bevfusion fixes that g3dbevfusion relies on, from [openvino_contrib#1197](https://github.com/openvinotoolkit/openvino_contrib/pull/1197), until that PR is merged (override the source with `OPENVINO_CONTRIB_URL` and `OPENVINO_CONTRIB_REF`). They also include a fix that gives the BEV-pool op a zeroed accumulator: without it the camera branch pools into stale GPU memory and fusion detects nothing on OpenVINO 2026.5. Two more patches speed up the model without changing its results: one removes a per-frame allocation that took about half of the sparse encoder's time, and one exports the camera backbone without the 5-D tensors that the GPU plugin runs slowly. Another raises the point and voxel limits of the LiDAR branch from one sweep to the ten the model is trained on. The last one makes the sparse encoder's kernel choice reproducible: it was made by timing, between kernels that give slightly different results. Detections can still differ slightly from run to run, because voxelization picks the points it averages in GPU-scheduling order. The export takes several minutes.

The two scripts write only what the pipeline needs, next to themselves:

```text
bevfusion_ov_model_<openvino-version>/
  g3dbevfusion.json            the element's config=, with paths relative to itself
  config.json                  model parameters written by export.py
  *.xml, *.bin                 voxelize, bev_pool, sparse_encoder, geometry, camera_encoder_b6, full_detection
  extensions/                  the three custom-op libraries, and sparse_conv.cl
  gpu_kernels/                 gpu_custom_layers.xml and its OpenCL kernels
nuscenes_mini_seq_12Hz/<scene>/
  CAM_*/%06d.jpg, LIDAR_TOP/%06d.bin
  calibration.json, calibration_lidar_only.json   calibration, and the per-frame table
  annotations.json, manifest.csv   key-frame ground truth and timestamps, for checking the output
```

The Python virtual environment, the sources, the checkpoint, the build trees and the nuScenes download are temporary and removed at the end. Both directories can be moved anywhere. A script does nothing if its directory already exists; remove the directory to prepare it again. Run either script with `--help` for its options, for example `NUSCENES_ROOT` to convert a nuScenes copy you already have.

## Sequence frames

The nuScenes LiDAR turns at 20 Hz, and the six cameras fire together, as the beam sweeps past each of them, on 12 of every 20 turns. Only every tenth turn (2 Hz) is an annotated key frame.

- **Frames.** `nuscenes_to_sequence.py` writes every turn on which all six cameras fired, so every frame is a camera + LiDAR frame. That is about 11 to 12 frames per second, because some turns lose an image. `--frames keyframes` writes only the 2 Hz key frames instead.
- **Point clouds.** Each frame's point cloud is its own turn followed by the 9 turns before it, as BEVFusion is trained. These 9 turns include the ones without camera images. The older turns are moved into the current LiDAR frame using the vehicle's motion. Points within 1 m of the sensor are dropped from them. This is about 270,000 points per frame. `--sweeps` changes the number of older turns, and `--sweeps 0` keeps only the frame's own turn.
- **Calibration.** Each camera exposes up to 45 ms before the LiDAR timestamp, while the vehicle moves. `calibration.json` gives every frame its own `camera2lidar`, which includes that motion.
- **Per-frame table.** The `frames` table in `calibration.json` gives, for each frame:
  - the capture time;
  - each camera's `camera2lidar`;
  - how many points each turn contributed and its time lag, which BEVFusion reads as a fifth point column.

  `g3dbevfusion` applies this table, and each frame's capture time becomes its PTS.

## Run

```bash
# camera + LiDAR fusion, all frames, rendered to an MP4 video that plays at the recorded pace
./g3dbevfusion.sh nuscenes_mini_seq_12Hz/scene-0103 bevfusion_ov_model_2026.3.1/g3dbevfusion.json fusion -1 out.mp4

# the same in a window, or as numbered JPEG files
./g3dbevfusion.sh nuscenes_mini_seq_12Hz/scene-0103 bevfusion_ov_model_2026.3.1/g3dbevfusion.json fusion -1 display
./g3dbevfusion.sh nuscenes_mini_seq_12Hz/scene-0103 bevfusion_ov_model_2026.3.1/g3dbevfusion.json fusion -1 out/

# LiDAR only, first 10 frames, no rendering
./g3dbevfusion.sh nuscenes_mini_seq_12Hz/scene-0103 bevfusion_ov_model_2026.3.1/g3dbevfusion.json lidar 10
```

Each sensor is read with `multifilesrc`. Every branch is read at the same nominal rate, 12 fps by default (set `FPS`, for example `FPS=2` for a key-frame sequence), so `gvastreammux` pairs frame N of each sensor. `g3dbevfusion` then gives each frame its recorded capture time as its PTS. Consecutive frames are 50 or 100 ms apart, so a video or a window shows the scene at the pace it was recorded. The pipeline is:

```bash
gst-launch-1.0 -e \
  multifilesrc location=scene/CAM_FRONT/%06d.jpg caps=image/jpeg,framerate=12/1 ! jpegparse ! \
    jpegdec idct-method=islow ! videoconvert ! video/x-raw,format=BGR ! queue ! mux.sink_0 \
  ... CAM_FRONT_RIGHT, CAM_FRONT_LEFT, CAM_BACK, CAM_BACK_LEFT, CAM_BACK_RIGHT on mux.sink_1 .. mux.sink_5 ... \
  multifilesrc location=scene/LIDAR_TOP/%06d.bin caps=application/octet-stream ! \
    g3dlidarparse frame-rate=12 ! queue ! mux.sink_6 \
  gvastreammux name=mux output-mode=container sync-mode=first-pts max-wait-time=5000000000 ! \
  g3dbevfusion config=bevfusion_ov_model_2026.3.1/g3dbevfusion.json calibration=scene/calibration.json ! \
  g3drender forward-axis=y point-stride=8 width=2400 height=900 ! videoconvert ! video/x-raw,format=NV12 ! \
  vah264enc rate-control=cqp ! h264parse ! mp4mux ! filesink location=out.mp4
```

`g3dlidarparse frame-rate=12` reads the sensors at the sequence's rate. On an Intel Arc Pro B60, with ten accumulated LiDAR sweeps, inference takes about 90 ms per frame for fusion and about 70 ms for LiDAR-only. The frames are about 89 ms apart on average, so fusion only just keeps up with the recording in a window. A video file is timed by its PTS, so it plays at the recorded pace whatever the inference speed.

See [g3dbevfusion](../../../../docs/user-guide/elements/g3dbevfusion.md) for the element's modes, configuration and limitations.
