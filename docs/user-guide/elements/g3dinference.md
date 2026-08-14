# g3dinference

Runs PointPillars 3D object detection on LiDAR point clouds. The element consumes `application/x-lidar` buffers produced by `g3dlidarparse`, executes a PointPillars inference pipeline with OpenVINO, and attaches 3D object-detection analytics metadata for downstream consumers.

## Overview

The `g3dinference` element is intended for LiDAR-only 3D detection pipelines where raw point clouds have already been parsed into a dense float buffer and annotated with `LidarMeta`.

Key operations:
- **LiDAR metadata validation**: Requires `LidarMeta` on each input buffer and validates payload size against `lidar_point_count`
- **PointPillars inference**: Loads the `extension_lib`, `voxel_model`, `nn_model`, and `postproc_model` entries from a JSON config and executes them in sequence. `voxel_params` document the voxelization settings used when exporting the PointPillars models and are not applied separately by `g3dinference` at runtime.
- **3D detection metadata attachment**: Attaches one `GstAnalytics3DODMtd` per detection to the buffer's `GstAnalyticsRelationMeta`
- **Pipeline integration**: Preserves the LiDAR payload and metadata so downstream elements can combine point clouds, detections, and converted JSON output

## Properties

| Property | Type | Description | Default |
|----------|------|-------------|---------|
| config | String | Path to the PointPillars JSON configuration file. Required. | null |
| device | String | OpenVINO device used for the neural network stage. Currently `CPU`, `GPU`, and `GPU.<id>` are supported. | CPU |
| model-type | String | 3D detector model type. Currently only `pointpillars` is supported. | pointpillars |
| score-threshold | Float | Drops detections below this score. `0.0` keeps all post-processing output unchanged. | 0.7 |
| nireq | Unsigned Integer | Number of inference requests processed concurrently. `0` derives the value from the compiled network. See [Throughput and concurrency](#throughput-and-concurrency). | 0 |

## Configuration

The `config` property should point to a PointPillars JSON configuration file. In practice, the file contains the paths to the OpenVINO extension library and the three models used by the runtime. It may also include `voxel_params` for compatibility with the exported PointPillars config format.

> **Warning:** `voxel_params` is kept for compatibility with the exported PointPillars config format. These values are used when exporting the PointPillars models and are already encoded in the generated voxelization model. `g3dinference` does not currently apply `voxel_params` as runtime-tunable settings.

Expected top-level entries:

- `voxel_params`: PointPillars voxelization settings such as `voxel_size`, `point_cloud_range`, `max_num_points`, and `max_voxels`. These values are used when exporting the PointPillars models and are already encoded in the generated voxelization model; `g3dinference` does not currently apply them as runtime-tunable parameters.
- `extension_lib`: Path to the custom OpenVINO extension library
- `voxel_model`: Path to the voxelization model
- `nn_model`: Path to the main neural network model
- `postproc_model`: Path to the post-processing model

Example configuration:

```json
{
  "voxel_params": {
    "voxel_size": [0.16, 0.16, 4],
    "point_cloud_range": [0, -39.68, -3, 69.12, 39.68, 1],
    "max_num_points": 32,
    "max_voxels": 16000
  },
  "extension_lib": "/path/to/pointPillars/ov_extensions/build/libov_pointpillars_extensions.so",
  "voxel_model": "/path/to/pointPillars/pretrained/pointpillars_ov_pillar_layer.xml",
  "nn_model": "/path/to/pointPillars/pretrained/pointpillars_ov_nn.xml",
  "postproc_model": "/path/to/pointPillars/pretrained/pointpillars_ov_postproc.xml"
}
```


## Throughput and concurrency

`g3dinference` processes frames asynchronously. Each buffer is submitted to a pool of workers, and every worker owns a private set of voxelization, network, and post-processing inference requests. Several frames are therefore in flight at once: the CPU-side stages of one frame overlap with the accelerator stage of another, instead of the whole chain running serially on the streaming thread.

All three stages are compiled with OpenVINO's `THROUGHPUT` performance hint so that each one opens several execution streams and the frames in flight really do execute concurrently. This is not configurable: the `LATENCY` hint would restrict every stage to a single execution stream, which caps concurrency at one request per stage and makes `nireq` almost meaningless.

The `nireq` property controls how many frames may be in flight:

- `nireq=0` (default) derives the count from the compiled network via OpenVINO's optimal number of inference requests.
- `nireq=1` keeps one frame in flight, which is equivalent to fully serial processing.
- Higher values keep the accelerator busier at the cost of more memory and more concurrent CPU work.

When `nireq` is set explicitly it is also passed to OpenVINO as `PERFORMANCE_HINT_NUM_REQUESTS`, so the plugins size their execution streams to the number of requests this element actually submits rather than to the device alone. Without it a plugin may open more streams than there are workers to feed, splitting its threads across streams that stay idle.

```bash
gst-launch-1.0 multifilesrc location="lidar/%06d.bin" caps=application/octet-stream ! \
  g3dlidarparse ! \
  g3dinference config=pointpillars_ov_config.json device=GPU nireq=4 ! \
  fakesink
```

Concurrency does not change what the element outputs. Buffers are always pushed downstream in the order they arrived, and each buffer carries exactly the same detections it would have carried under serial processing. Raising `nireq` beyond the point where the accelerator saturates stops improving throughput.

## Pipeline Examples

Use `multifilesrc` for LiDAR input, including single-frame runs, so each frame is delivered as one buffer instead of `filesrc` block fragments.

### Basic LiDAR inference pipeline

```bash
gst-launch-1.0 multifilesrc location="lidar/%06d.bin" start-index=0 caps=application/octet-stream ! \
  g3dlidarparse stride=1 frame-rate=5 ! \
  g3dinference config=pointpillars_ov_config.json device=CPU ! \
  fakesink
```

### Inference with JSON export

```bash
gst-launch-1.0 multifilesrc location="lidar/%06d.bin" caps=application/octet-stream ! \
  g3dlidarparse ! \
  g3dinference config=pointpillars_ov_config.json device=GPU score-threshold=0.5 ! \
  gvametaconvert format=json json-indent=2 ! \
  gvametapublish file-format=2 file-path=pointpillars.json ! \
  fakesink
```

## Input/Output

- **Input Capability**: `application/x-lidar`
- **Output Capability**: `application/x-lidar`

The element operates in-place. It keeps the point cloud payload intact and appends inference results as metadata.

## Metadata

### Required input metadata

`g3dinference` expects `LidarMeta` attached by `g3dlidarparse`. It uses:

- `lidar_point_count`
- `frame_id`
- `stream_id`
- `exit_source_timestamp`

### Output metadata

The element adds one `GstAnalytics3DODMtd` per detection to the buffer's `GstAnalyticsRelationMeta`. Each `GstAnalytics3DODMtd` carries:

- `x, y, z`: 3D bounding box center coordinates (metres, sensor/world frame)
- `length, width, height`: box extents (metres)
- `yaw, pitch, roll`: orientation (radians). Only `yaw` is populated by PointPillars; `pitch` and `roll` are `0`.
- `class_id`: predicted class identifier
- `confidence`: detection score produced by the model post-processing stage
- `modality`: sensor modality, set to `GST_ANALYTICS_3D_SENSOR_LIDAR`

The PointPillars post-processing emits boxes as `(x, y, z, w, l, h, theta, score, label)`; `g3dinference` maps the model's `w`/`l` onto the metadata's `width`/`length` so the stored `length`/`width` keep their physical meaning.

When `gvametaconvert` converts these detections to JSON, each becomes a `bbox_3d` object with `x, y, z, l, w, h, yaw, pitch, roll`, plus `confidence`, `label_id`, and `modality` (`"lidar"`).

## Processing Pipeline

1. Validates that runtime initialization succeeded and `config` is present
2. Retrieves `LidarMeta` from the input buffer
3. Maps the LiDAR payload and verifies its size matches `lidar_point_count * 4 * sizeof(float)`
4. Submits the frame to the worker pool and accepts the next buffer without waiting for the result
5. On a worker thread, runs voxelization, network inference, and post-processing
6. Attaches one `GstAnalytics3DODMtd` per detection to the buffer's `GstAnalyticsRelationMeta`
7. Pushes the enriched LiDAR buffer downstream once every earlier frame has been pushed, preserving input order

Steps 1–3 run on the streaming thread, so malformed input still fails the pipeline immediately. Steps 5–7 run concurrently for up to `nireq` frames.

## Element Details (gst-inspect-1.0)

```text
Pad Templates:
  SINK template: 'sink'
    Availability: Always
    Capabilities:
      application/x-lidar

  SRC template: 'src'
    Availability: Always
    Capabilities:
      application/x-lidar

Element has no clocking capabilities.
Element has no URI handling capabilities.

Pads:
  SINK: 'sink'
    Pad Template: 'sink'
  SRC: 'src'
    Pad Template: 'src'

Element Properties:

  config              : Path to PointPillars OpenVINO JSON configuration
                        flags: readable, writable
                        String. Default: null

  device              : OpenVINO device for NN model. Supported values: CPU, GPU, GPU.<id>
                        flags: readable, writable
                        String. Default: "CPU"

  model-type          : 3D detector model type
                        flags: readable, writable
                        String. Default: "pointpillars"

  name                : The name of the object
                        flags: readable, writable
                        String. Default: "g3dinference0"

  nireq               : Number of inference requests processed concurrently. Frames are kept in flight across this many requests while output order is preserved. 0 derives the value from the compiled network
                        flags: readable, writable
                        Unsigned Integer. Range: 0 - 1024 Default: 0

  parent              : The parent of the object
                        flags: readable, writable
                        Object of type "GstObject"

  qos                 : Handle Quality-of-Service events
                        flags: readable, writable
                        Boolean. Default: false

  score-threshold     : Drop detections below this score (0 keeps all postproc output)
                        flags: readable, writable
                        Float. Range:               0 - 1 
                        Default:               0.7
```