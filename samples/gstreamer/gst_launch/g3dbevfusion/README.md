# BEVFusion 3D Detection with g3dbevfusion

This sample runs BEVFusion camera + LiDAR 3D object detection on [nuScenes](https://www.nuscenes.org/) scenes with `g3dbevfusion`, and renders the detections on the six cameras and a bird's-eye view with `g3drender`.

The sample has four scripts:

- `g3dbevfusion_prepare.sh` prepares the assets from openvino_contrib's [`openvino_bevfusion`](https://github.com/openvinotoolkit/openvino_contrib/tree/master/modules/openvino_bevfusion) module. It:
  - builds the custom-op extensions against DL Streamer's OpenVINO;
  - downloads the published mmdetection3d checkpoint and exports the OpenVINO IRs;
  - writes the element's `g3dbevfusion.json`;
  - with `NUSCENES_ROOT` set, converts the nuScenes scenes.
- `nuscenes_to_sequence.py` turns nuScenes scenes into numbered per-sensor files for `multifilesrc`. It writes key frames only, so frame N of every sensor is the same sample, along with the static calibration and the ground-truth boxes.
- `g3dbevfusion.sh` runs the pipeline on a converted scene, in `fusion` (six cameras + LiDAR) or `lidar` mode.
- `bevfusion_reference.py` runs openvino_contrib's Python runtime on the same inputs, to check the element's output frame by frame.

## Prerequisites

- An Intel GPU: the voxelize and BEV-pool ops are GPU kernels.
- A Python environment with `torch`, `numpy` and `openvino` for the IR export (`PYTHON=...`).
- The nuScenes dataset, for example `v1.0-mini` (`samples/` and `v1.0-mini/` under `NUSCENES_ROOT`).

## Prepare

```bash
source /opt/intel/openvino_2026/setupvars.sh
PYTHON=~/bevfusion_venv/bin/python NUSCENES_ROOT=/data/nuscenes ./g3dbevfusion_prepare.sh
```

Until the openvino_bevfusion fixes that g3dbevfusion relies on are merged upstream, the script clones them from the `bevfusion-dlstreamer-fixes` branch of [qianlongding/openvino_contrib](https://github.com/qianlongding/openvino_contrib/tree/bevfusion-dlstreamer-fixes) (override with `OPENVINO_CONTRIB_URL` and `OPENVINO_CONTRIB_REF`).

Assets go to `.bevfusion/` next to the script: the config, the IRs, the extensions, and `nuscenes/<scene>/` per converted scene. The export takes several minutes; later runs reuse what is already there.

## Run

```bash
# camera + LiDAR fusion, all frames, rendered to JPEG files
./g3dbevfusion.sh .bevfusion/nuscenes/scene-0103 .bevfusion/g3dbevfusion.json fusion -1 out/

# LiDAR only, first 10 frames, no rendering
./g3dbevfusion.sh .bevfusion/nuscenes/scene-0103 .bevfusion/g3dbevfusion.json lidar 10
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
  g3dbevfusion config=.bevfusion/g3dbevfusion.json calibration=scene/calibration.json ! \
  g3drender bev-forward=y width=2400 height=900 ! videoconvert ! jpegenc ! multifilesink location=out/%06d.jpg
```

Fusion takes about 2.4 s per frame and LiDAR-only about 0.14 s, on an Intel Arc Pro B60.

See [g3dbevfusion](../../../../docs/user-guide/elements/g3dbevfusion.md) for the element's modes, configuration and limitations.
