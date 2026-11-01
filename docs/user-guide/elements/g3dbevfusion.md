# g3dbevfusion

Runs BEVFusion camera + LiDAR 3D object detection on a `gvastreammux` batch of six surround-view cameras and one LiDAR, and attaches the detections to the LiDAR stream.

## Overview

The `g3dbevfusion` element is a `GstBaseTransform` that runs in place on the **container batch** (`multistream/x-analytics-batch`) produced by `gvastreammux output-mode=container`. It runs the OpenVINO port of BEVFusion from openvino_contrib ([`modules/openvino_bevfusion`](https://github.com/openvinotoolkit/openvino_contrib/tree/master/modules/openvino_bevfusion)), which reproduces the mmdetection3d BEVFusion lidar-camera model trained on nuScenes.

The model runs in one of two modes. The element picks the mode for each batch from the streams it carries:

| Sensors declared in `calibration` | Streams in the batch | Mode for this batch |
|---|---|---|
| LiDAR + 6 cameras | LiDAR and all 6 cameras | **camera + LiDAR fusion** |
| LiDAR + 6 cameras | LiDAR, some cameras missing | **LiDAR-only**, with a warning on the first occurrence |
| LiDAR only | LiDAR | **LiDAR-only** |
| any | no LiDAR | passed through without detections, with a warning on the first occurrence |

BEVFusion has no camera-only mode: its camera branch takes a depth map projected from the LiDAR points. Declaring between 1 and 5 cameras is an error at startup, because the camera branch is exported for exactly six. A summary of how many frames ran in each mode is logged at `INFO` when the element stops.

Detections are attached as `GstAnalytics3DODMtd` to the **LiDAR buffer** inside the batch, like `g3dinference` does. A sticky `g3d/calibration` event carries every camera's projection and the class names downstream, so `g3drender` can draw the boxes on each camera image and colour them per class.

## Properties

| Property | Type | Default | Description |
|---|---|---|---|
| `config` | String | NULL | Runtime JSON describing the model files (see [Runtime configuration](#runtime-configuration)). Required. |
| `calibration` | String | NULL | JSON naming the mux pad of the LiDAR and of each camera, with each camera's calibration (see [Calibration JSON schema](#calibration-json-schema)). Required. |
| `device` | String | `GPU` | Device for the dense networks: camera encoder, geometry and detection head. The custom voxelize and BEV-pool ops always run on the GPU. |
| `score-threshold` | Float [0, 1] | 0.3 | Minimum detection score to attach. The model proposes 200 boxes per frame; most score close to 0. |

## Preparing the model

`samples/gstreamer/gst_launch/g3dbevfusion/g3dbevfusion_prepare.sh` prepares everything the element needs from openvino_contrib's `openvino_bevfusion` module:

1. builds the three custom-op extension libraries against DL Streamer's OpenVINO (`OpenVINO_DIR`);
2. downloads the published mmdetection3d BEVFusion lidar-camera checkpoint and strips it to its weights;
3. exports the OpenVINO IRs with `export.py` (needs a Python environment with `torch`, `numpy` and `openvino`);
4. writes the runtime configuration `g3dbevfusion.json`;
5. with `NUSCENES_ROOT` set, converts nuScenes scenes into per-sensor files that `multifilesrc` can read (see below).

```bash
source /opt/intel/openvino_2026/setupvars.sh
PYTHON=~/bevfusion_venv/bin/python NUSCENES_ROOT=/data/nuscenes ./g3dbevfusion_prepare.sh
```

Run `./g3dbevfusion_prepare.sh --help` for every option, including `BEVFUSION_ROOT` to use an existing openvino_contrib checkout.

Until the openvino_bevfusion fixes that g3dbevfusion relies on are merged upstream, the script clones them from the `bevfusion-dlstreamer-fixes` branch of [qianlongding/openvino_contrib](https://github.com/qianlongding/openvino_contrib/tree/bevfusion-dlstreamer-fixes) (override with `OPENVINO_CONTRIB_URL` and `OPENVINO_CONTRIB_REF`).

### Runtime configuration

```json
{
  "model_dir": "/path/to/openvino_model",
  "extensions": [
    "/path/to/libopenvino_bevpool_extension.so",
    "/path/to/libopenvino_voxelize_extension.so",
    "/path/to/libopenvino_sparse_encoder_extension.so"
  ],
  "gpu_custom_layers": "/path/to/openvino_bevfusion/openvino_extensions/gpu_custom_layers.xml",
  "kernel_dir": "/path/to/dir/with/sparse_conv.cl"
}
```

Relative paths are resolved against the configuration file's directory. At startup the element checks `model_dir/config.json` (written by `export.py`) against the image size, feature size, camera count and class count it was built for, and fails if they differ.

The extension libraries only load into the OpenVINO version they were built against. Rebuild them when DL Streamer moves to a new OpenVINO release.

## Calibration JSON schema

```json
{
  "lidar": { "pad": 6 },
  "cameras": {
    "0": {
      "slot": 0,
      "name": "CAM_FRONT",
      "camera2lidar": [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1],
      "intrinsics": [1266.4, 0, 816.3, 0, 1266.4, 491.5, 0, 0, 1]
    }
  }
}
```

The example shows one camera with placeholder values; a fusion rig lists six, keyed `"0"` to `"5"` for pads 0-5.

- `camera2lidar` is a 4x4 row-major matrix (16 values) and `intrinsics` a 3x3 row-major matrix (9 values).
- `lidar.pad` and the keys of `cameras` are `gvastreammux` sink pad indices (`mux.sink_<pad>`).
- `slot` is the camera's position in the model's batch, in the nuScenes order `CAM_FRONT`, `CAM_FRONT_RIGHT`, `CAM_FRONT_LEFT`, `CAM_BACK`, `CAM_BACK_LEFT`, `CAM_BACK_RIGHT`. Every slot 0-5 must appear exactly once.
- `camera2lidar` takes camera coordinates (x right, y down, z forward) to the LiDAR frame.
- Omit `cameras` (or leave it empty) for a LiDAR-only rig.

The calibration is static. nuScenes compensates each camera for the ego motion between its exposure and the LiDAR timestamp, which a static calibration cannot do. The resulting misalignment is about 0.1 m typical, and up to 0.4 m.

`nuscenes_to_sequence.py` writes `calibration.json` (six cameras) and `calibration_lidar_only.json` for each converted scene.

## Pipeline Examples

### Camera + LiDAR fusion on a converted nuScenes scene

```bash
./g3dbevfusion.sh .bevfusion/nuscenes/scene-0103 .bevfusion/g3dbevfusion.json fusion -1 out/
```

which runs:

```bash
gst-launch-1.0 -e \
  multifilesrc location=scene/CAM_FRONT/%06d.jpg caps=image/jpeg,framerate=2/1 ! jpegparse ! \
    jpegdec idct-method=islow ! videoconvert ! video/x-raw,format=BGR ! queue ! mux.sink_0 \
  ... the other five cameras on mux.sink_1 .. mux.sink_5 ... \
  multifilesrc location=scene/LIDAR_TOP/%06d.bin caps=application/octet-stream ! \
    g3dlidarparse frame-rate=2 ! queue ! mux.sink_6 \
  gvastreammux name=mux output-mode=container sync-mode=first-pts max-wait-time=5000000000 ! \
  g3dbevfusion config=g3dbevfusion.json calibration=scene/calibration.json ! \
  g3drender bev-forward=y width=2400 height=900 ! videoconvert ! jpegenc ! multifilesink location=out/%06d.jpg
```

Every branch must use the same frame rate: the streams are paired by timestamp, and the timestamps come from the frame index.

### LiDAR-only

```bash
gst-launch-1.0 -e \
  multifilesrc location=scene/LIDAR_TOP/%06d.bin caps=application/octet-stream ! \
    g3dlidarparse frame-rate=2 ! queue ! mux.sink_6 \
  gvastreammux name=mux output-mode=container sync-mode=first-pts ! \
  g3dbevfusion config=g3dbevfusion.json calibration=scene/calibration_lidar_only.json ! fakesink
```

## Input / Output

| | Capability |
|---|---|
| **Sink** | `multistream/x-analytics-batch(meta:GstAnalyticsBatchMeta)` — a `gvastreammux` container batch |
| **Src** | `multistream/x-analytics-batch(meta:GstAnalyticsBatchMeta)` — the same batch, with detections added |

Inside the batch:

- **LiDAR**: `application/x-lidar` from `g3dlidarparse` or `g3dlidarsrc`, with `LidarMeta`. Points are `(x, y, z, intensity)` float32. nuScenes intensities (0..255) are used as-is, as the model was trained on them.
- **Cameras**: `video/x-raw, format=BGR` in system memory. Images are resized by 0.48 and the bottom-centre 704×256 is cropped, as in training, so they must be at least 1467×534 (nuScenes is 1600×900).

## Metadata

One `GstAnalytics3DODMtd` per detection is added to the LiDAR buffer's `GstAnalyticsRelationMeta`:

- `x, y, z`: box centre in the LiDAR frame (metres)
- `length, width, height`: extents along the box's own x, y and z axes (metres)
- `yaw`: counter-clockwise rotation about +z from the LiDAR's +x axis to the box's length axis; `pitch` and `roll` are 0
- `class_id`: index into the model's classes: `car`, `truck`, `construction_vehicle`, `bus`, `trailer`, `barrier`, `motorcycle`, `bicycle`, `pedestrian`, `traffic_cone`
- `confidence`: detection score
- `modality`: `GST_ANALYTICS_3D_SENSOR_LIDAR`

The model also predicts a velocity per box, which is not attached: it needs multi-sweep LiDAR input, and the element runs on single key frames (see [Limitations](#limitations)).

## Calibration propagated downstream (`g3d/calibration` event)

Before the first buffer, the element pushes a sticky `GST_EVENT_CUSTOM_DOWNSTREAM_STICKY` event named `g3d/calibration`, in the same layout as `g3dobjectfuser`:

- `modality` = `"lidar"`, `cameras` = number of cameras
- `camera-<pad>`: `index`, `name`, and the camera's projection as `tr_velo_to_cam` = inverse(`camera2lidar`), `r0_rect` = identity, `p2` = `[K | 0]`
- `class-names`: array of strings, one per `class_id`

## Limitations

- **Six fixed cameras.** The camera branch is exported for a batch of six images of 1600×900-class resolution.
- **Key frames only.** The reference model aggregates 9 previous LiDAR sweeps, with ego-motion compensation and per-point time offsets. The element uses a single sweep with time offset 0, which lowers recall and makes the velocity output meaningless.
- **One frame at a time.** Inference is synchronous in the streaming thread: about 2.4 s per frame for fusion (dominated by the BEV-pool op) and 0.14 s for LiDAR-only on an Intel Arc Pro B60.
- **One sparse-encoder context per process.** The sparse-encoder extension keeps process-wide OpenCL state. Several `g3dbevfusion` instances in one process are serialised around it.
- **JPEG decoding matters.** The camera branch is sensitive to small pixel differences. `jpegdec` and OpenCV decode the same JPEG slightly differently, and scores can move by a few hundredths as a result.

## Debugging

- `G3DBEVFUSION_DUMP_DIR=<dir>` writes each frame's decoded boxes as `boxes_<frame>.npy`: float32 `[N, 11]` holding `x, y, z_bottom, dx, dy, dz, yaw, vx, vy, score, label`.
- With `G3DBEVFUSION_DUMP_TENSORS=1`, every stage's input and output tensors are written too. That is about 90 MB per frame.
- `BEVFUSION_VERBOSE=1` prints the sparse-encoder extension's timing and kernel selection.
- `samples/gstreamer/gst_launch/g3dbevfusion/bevfusion_reference.py` runs openvino_contrib's Python runtime on the same inputs, for comparison. `--decoder gstreamer` decodes the JPEGs the way the pipeline does.

## Element Details (gst-inspect-1.0)

```
Pad Templates:
  SINK template: 'sink'
    Availability: Always
    Capabilities:
      multistream/x-analytics-batch(meta:GstAnalyticsBatchMeta)

  SRC template: 'src'
    Availability: Always
    Capabilities:
      multistream/x-analytics-batch(meta:GstAnalyticsBatchMeta)

Element has no clocking capabilities.
Element has no URI handling capabilities.

Pads:
  SINK: 'sink'
    Pad Template: 'sink'
  SRC: 'src'
    Pad Template: 'src'

Element Properties:

  calibration         : JSON naming the lidar's and each camera's mux pad, with each camera's model slot, camera2lidar and intrinsics
                        flags: readable, writable
                        String. Default: null
  config              : Runtime JSON: model_dir, extensions, gpu_custom_layers and kernel_dir of the OpenVINO BEVFusion model
                        flags: readable, writable
                        String. Default: null
  device              : Device for the dense networks (camera encoder, geometry, detection head); the custom voxelize/bev_pool ops always run on the GPU
                        flags: readable, writable
                        String. Default: "GPU"
  name                : The name of the object
                        flags: readable, writable
                        String. Default: "g3dbevfusion0"
  parent              : The parent of the object
                        flags: readable, writable
                        Object of type "GstObject"
  qos                 : Handle Quality-of-Service events
                        flags: readable, writable
                        Boolean. Default: false
  score-threshold     : Minimum detection score to attach
                        flags: readable, writable
                        Float. Range:               0 -               1 Default:             0.3
```
