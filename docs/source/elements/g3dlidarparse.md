# g3dlidarparse

Parses raw LiDAR frames (BIN/PCD) into point-cloud metadata. The element reads binary LiDAR buffers, applies stride and frame-rate thinning, and attaches `LidarMeta` (points, frame_id, timestamps, stream_id) for downstream fusion, analytics, or visualization.

## Overview
The `g3dlidarparse` element ingests raw LiDAR frame data and produces buffers enriched with LiDAR metadata. It is typically used as a source-side pre-processor in 3D pipelines before visualization or sensor-fusion stages.

Key operations:
- **Input parsing**: Reads raw LiDAR frames (BIN/PCD) from `application/octet-stream` buffers
- **Frame thinning**: Applies `stride` and `frame-rate` controls to reduce processing load
- **Metadata attachment**: Emits `LidarMeta` containing point cloud data and frame identifiers

## Properties
| Property   | Type    | Description                                                                 | Default |
|------------|---------|-----------------------------------------------------------------------------|---------|
| stride     | Integer (>=1) | Process every Nth frame (1 = every frame).                                  | 1       |
| frame-rate | Float (>=0)   | Target output frame rate (0 = no limit).                                    | 0       |


## Pipeline Examples

### Basic parsing pipeline
```bash
gst-launch-1.0 multifilesrc location="velodyne/%06d.bin" start-index=250 caps=application/octet-stream ! \
  g3dlidarparse stride=1 frame-rate=5 ! fakesink
```

## Input/Output
- **Input Capability**: `application/octet-stream` (raw LiDAR frame data)
- **Output Capability**: `application/x-lidar` (buffer with attached LiDAR metadata)

## Metadata Structure
The element attaches `LidarMeta` to each output buffer, containing:
- Point cloud coordinates
- Frame identifier
- Timestamps and stream identifiers

## Processing Pipeline
1. **Buffer validation**: Ensures input buffer is present and readable
2. **Frame selection**: Applies `stride` and `frame-rate` logic
3. **Parsing**: Decodes BIN/PCD data into point cloud representation
4. **Metadata attachment**: Attaches `LidarMeta` to the buffer

## Element Details (gst-inspect-1.0)
```
Pad Templates:
  SINK template: 'sink'
    Availability: Always
    Capabilities:
      application/octet-stream

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

  frame-rate          : Desired output frame rate in frames per second. A value of 0 means no frame rate control.
                        flags: readable, writable
                        Float. Range:               0 -    3.402823e+38 Default:               0

  name                : The name of the object
                        flags: readable, writable
                        String. Default: "g3dlidarparse0"

  parent              : The parent of the object
                        flags: readable, writable
                        Object of type "GstObject"

  qos                 : Handle Quality-of-Service events
                        flags: readable, writable
                        Boolean. Default: false

  stride              : Specifies the interval of frames to process, controls processing granularity. 1 means every frame is processed, 2 means every second frame is processed.
                        flags: readable, writable
                        Integer. Range: 1 - 2147483647 Default: 1

```

