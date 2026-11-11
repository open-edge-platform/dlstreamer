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

`samples/gstreamer/gst_launch/g3dbevfusion/g3dbevfusion_prepare_model.sh <openvino-version>` prepares `bevfusion_ov_model_<openvino-version>/`, everything the element loads, from openvino_contrib's `openvino_bevfusion` module:

1. creates a temporary Python virtual environment with CPU PyTorch, NumPy and `openvino==<openvino-version>`;
2. builds the three custom-op extension libraries against that `openvino` wheel;
3. downloads the published mmdetection3d BEVFusion lidar-camera checkpoint and strips it to its weights;
4. exports the OpenVINO IRs with `export.py`;
5. copies the IRs, the extension libraries and the GPU kernels into the output directory and writes the runtime configuration `g3dbevfusion.json` there.

The virtual environment, sources, checkpoint and build trees are removed at the end. `g3dbevfusion_prepare_data.sh` downloads nuScenes v1.0-mini and converts it into `nuscenes_mini_seq/` (see below).

```bash
sudo apt-get install git curl g++ python3-venv opencl-c-headers ocl-icd-opencl-dev
./g3dbevfusion_prepare_model.sh 2026.3.1
./g3dbevfusion_prepare_data.sh
```

Pass the full OpenVINO version of the DL Streamer release that runs the pipeline. The extensions only load into the OpenVINO release they were built against. For a DL Streamer built on an OpenVINO nightly, pass the matching pre-release wheel version, for example `2026.5.0.dev20261030`, with `PIP_EXTRA_INDEX_URL=https://storage.openvinotoolkit.org/simple/wheels/nightly`. The export compiles the custom ops on the GPU, so in a container the GPU has to be available (`--device /dev/dri`).

Run either script with `--help` for every option, including `BEVFUSION_ROOT` to use an existing openvino_contrib checkout and `NUSCENES_ROOT` to convert an existing nuScenes copy.

The model script checks out a fixed commit of [openvinotoolkit/openvino_contrib](https://github.com/openvinotoolkit/openvino_contrib) and applies the patches in `samples/gstreamer/gst_launch/g3dbevfusion/patches/` on top of it. They carry the openvino_bevfusion fixes that g3dbevfusion relies on, from [openvino_contrib#1197](https://github.com/openvinotoolkit/openvino_contrib/pull/1197), until that PR is merged (override the source with `OPENVINO_CONTRIB_URL` and `OPENVINO_CONTRIB_REF`). They also include a fix that gives the BEV-pool op a zeroed accumulator: without it the camera branch pools into stale GPU memory and fusion detects nothing on OpenVINO 2026.5. A third patch removes a per-frame allocation from the sparse encoder that took about half of its time.

### Runtime configuration

```json
{
  "model_dir": ".",
  "extensions": [
    "extensions/libopenvino_bevpool_extension.so",
    "extensions/libopenvino_voxelize_extension.so",
    "extensions/libopenvino_sparse_encoder_extension.so"
  ],
  "gpu_custom_layers": "gpu_kernels/gpu_custom_layers.xml"
}
```

This is the file `g3dbevfusion_prepare_model.sh` writes. Relative paths are resolved against the configuration file's directory. The sparse encoder library reads its OpenCL kernel, `sparse_conv.cl`, from its own directory, and the kernels named in `gpu_custom_layers.xml` are resolved relative to that file. At startup the element checks `model_dir/config.json` (written by `export.py`) against the image size, feature size, camera count and class count it was built for, and fails if they differ.

The extension libraries only load into the OpenVINO version they were built against. Prepare a new model directory when DL Streamer moves to a new OpenVINO release.

`bev_pool.xml` must take the BEV-pool accumulator as an input (`bev_accumulator`), as `g3dbevfusion_prepare_model.sh` exports it; the element fails to start with a model exported without it.

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
./g3dbevfusion.sh nuscenes_mini_seq/scene-0103 bevfusion_ov_model_2026.3.1/g3dbevfusion.json fusion -1 out/
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
  g3drender forward-axis=y point-stride=2 width=2400 height=900 ! videoconvert ! jpegenc ! multifilesink location=out/%06d.jpg
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
- **One frame at a time.** Inference is synchronous in the streaming thread. For fusion, the LiDAR and camera branches run in parallel inside the element. On an Intel Arc Pro B60 a frame takes about 100 ms for fusion and about 60 ms for LiDAR-only.
- **One sparse-encoder context per process.** The sparse-encoder extension keeps process-wide OpenCL state. Several `g3dbevfusion` instances in one process are serialised around it.
- **JPEG decoding matters.** The camera branch is sensitive to small pixel differences. `jpegdec` and OpenCV decode the same JPEG slightly differently, and scores can move by a few hundredths as a result.

## Debugging

- `G3DBEVFUSION_DUMP_DIR=<dir>` writes each frame's decoded boxes as `boxes_<frame>.npy`: float32 `[N, 11]` holding `x, y, z_bottom, dx, dy, dz, yaw, vx, vy, score, label`.
- With `G3DBEVFUSION_DUMP_TENSORS=1`, every stage's input and output tensors are written too. That is about 90 MB per frame.
- `BEVFUSION_VERBOSE=1` prints the sparse-encoder extension's timing and kernel selection.

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
  config              : Runtime JSON: model_dir, extensions and gpu_custom_layers of the OpenVINO BEVFusion model
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
