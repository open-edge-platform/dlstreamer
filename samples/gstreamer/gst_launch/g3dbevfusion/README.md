# BEVFusion 3D Detection with g3dbevfusion

This sample runs BEVFusion camera + LiDAR 3D object detection on [nuScenes](https://www.nuscenes.org/) scenes with `g3dbevfusion`, and renders the detections on the six cameras and a bird's-eye view with `g3drender`.

The sample has four scripts:

- `g3dbevfusion_prepare_model.sh <openvino-version>` prepares `bevfusion_ov_model_<openvino-version>/` from openvino_contrib's [`openvino_bevfusion`](https://github.com/openvinotoolkit/openvino_contrib/tree/master/modules/openvino_bevfusion) module. It:
  - builds the custom-op extensions against that OpenVINO release;
  - downloads the published mmdetection3d checkpoint and exports the OpenVINO IRs;
  - writes the element's `g3dbevfusion.json`.
- `g3dbevfusion_prepare_data.sh` downloads nuScenes `v1.0-mini` and converts its scenes into `nuscenes_mini_seq/`.
- `nuscenes_to_sequence.py`, which `g3dbevfusion_prepare_data.sh` calls, turns nuScenes scenes into numbered per-sensor files for `multifilesrc`. It writes key frames only, so frame N of every sensor is the same sample, along with the static calibration and the ground-truth boxes.
- `g3dbevfusion.sh` runs the pipeline on a converted scene, in `fusion` (six cameras + LiDAR) or `lidar` mode.

## Prerequisites

- An Intel GPU: the voxelize and BEV-pool ops are GPU kernels.
- For `g3dbevfusion_prepare_model.sh`: `apt-get install git curl g++ python3-venv opencl-c-headers ocl-icd-opencl-dev`. The script installs CPU PyTorch, NumPy and `openvino` into a temporary Python virtual environment of its own.
- About 5 GB of free disk space while `g3dbevfusion_prepare_data.sh` runs, for the nuScenes `v1.0-mini` download.

## Prepare

```bash
./g3dbevfusion_prepare_model.sh 2026.3.1
./g3dbevfusion_prepare_data.sh
```

Pass the full OpenVINO version of the DL Streamer release that will run the pipeline, on the host or in its container. The custom-op extensions only load into the OpenVINO release they were built against, so a model directory prepared for 2026.3.1 does not work with 2026.3.0 or 2026.4.0. The script needs no DL Streamer or OpenVINO environment: it builds the extensions against the `openvino` wheel of that version. For a DL Streamer built on an OpenVINO nightly, pass the matching pre-release wheel version, for example `2026.5.0.dev20261030`, with `PIP_EXTRA_INDEX_URL=https://storage.openvinotoolkit.org/simple/wheels/nightly`.

The model script checks out a fixed commit of [openvinotoolkit/openvino_contrib](https://github.com/openvinotoolkit/openvino_contrib) and applies the patches in [`patches/`](patches/) on top of it. They carry the openvino_bevfusion fixes that g3dbevfusion relies on, from [openvino_contrib#1197](https://github.com/openvinotoolkit/openvino_contrib/pull/1197), until that PR is merged (override the source with `OPENVINO_CONTRIB_URL` and `OPENVINO_CONTRIB_REF`). They also include a fix that gives the BEV-pool op a zeroed accumulator: without it the camera branch pools into stale GPU memory and fusion detects nothing on OpenVINO 2026.5. A third patch removes a per-frame allocation from the sparse encoder that took about half of its time. The export takes several minutes.

The two scripts write only what the pipeline needs, next to themselves:

```text
bevfusion_ov_model_<openvino-version>/
  g3dbevfusion.json            the element's config=, with paths relative to itself
  config.json                  model parameters written by export.py
  *.xml, *.bin                 voxelize, bev_pool, sparse_encoder, geometry, camera_encoder_b6, full_detection
  extensions/                  the three custom-op libraries, and sparse_conv.cl
  gpu_kernels/                 gpu_custom_layers.xml and its OpenCL kernels
nuscenes_mini_seq/<scene>/
  CAM_*/%06d.jpg, LIDAR_TOP/%06d.bin
  calibration.json, calibration_lidar_only.json
  annotations.json, manifest.csv   ground truth and sample tokens, for checking the output
```

The Python virtual environment, the sources, the checkpoint, the build trees and the nuScenes download are temporary and removed at the end. Both directories can be moved anywhere. A script does nothing if its directory already exists; remove the directory to prepare it again. Run either script with `--help` for its options, for example `NUSCENES_ROOT` to convert a nuScenes copy you already have.

## Run

```bash
# camera + LiDAR fusion, all frames, rendered to JPEG files
./g3dbevfusion.sh nuscenes_mini_seq/scene-0103 bevfusion_ov_model_2026.3.1/g3dbevfusion.json fusion -1 out/

# LiDAR only, first 10 frames, no rendering
./g3dbevfusion.sh nuscenes_mini_seq/scene-0103 bevfusion_ov_model_2026.3.1/g3dbevfusion.json lidar 10
```

Each sensor is read with `multifilesrc`. Every branch is timestamped at the same 2 fps, so `gvastreammux` pairs frame N of each sensor. The pipeline is:

```bash
gst-launch-1.0 -e \
  multifilesrc location=scene/CAM_FRONT/%06d.jpg caps=image/jpeg,framerate=2/1 ! jpegparse ! \
    jpegdec idct-method=islow ! videoconvert ! video/x-raw,format=BGR ! queue ! mux.sink_0 \
  ... CAM_FRONT_RIGHT, CAM_FRONT_LEFT, CAM_BACK, CAM_BACK_LEFT, CAM_BACK_RIGHT on mux.sink_1 .. mux.sink_5 ... \
  multifilesrc location=scene/LIDAR_TOP/%06d.bin caps=application/octet-stream ! \
    g3dlidarparse frame-rate=2 ! queue ! mux.sink_6 \
  gvastreammux name=mux output-mode=container sync-mode=first-pts max-wait-time=5000000000 ! \
  g3dbevfusion config=bevfusion_ov_model_2026.3.1/g3dbevfusion.json calibration=scene/calibration.json ! \
  g3drender forward-axis=y point-stride=2 width=2400 height=900 ! videoconvert ! jpegenc ! multifilesink location=out/%06d.jpg
```

On an Intel Arc Pro B60, inference takes about 80 ms per frame for fusion and about 55 ms for LiDAR-only in this pipeline, and about 65 ms and 37 ms when frames arrive back to back. The pipeline runs at 2 fps because `g3dlidarparse frame-rate=2` plays the scene back at the nuScenes key-frame rate.

See [g3dbevfusion](../../../../docs/user-guide/elements/g3dbevfusion.md) for the element's modes, configuration and limitations.
