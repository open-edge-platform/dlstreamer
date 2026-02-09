# LiDAR Parsing with g3dlidarparse Element

This directory contains a script demonstrating how to use the `g3dlidarparse` element for parsing LiDAR point‑cloud frames.

The `g3dlidarparse` element ingests raw LiDAR frames (BIN/PCD) and attaches `LidarMeta` (points, frame_id, timestamps, stream_id) for downstream fusion, analytics, or visualization.

## How It Works

The sample uses the GStreamer command‑line tool `gst-launch-1.0` to build a pipeline string. Elements are separated by `!`, and properties are provided as `property=value` pairs.

This sample builds a pipeline of:
- `multifilesrc` for reading sequential LiDAR binary files
- `g3dlidarparse` for parsing LiDAR frames and attaching metadata
- `fakesink` for discarding output (metadata is attached to buffers)

The `g3dlidarparse` element performs:
1. **Input parsing**: Reads raw LiDAR frames from `application/octet-stream`
2. **Frame thinning**: Applies `stride` and `frame-rate` controls
3. **Metadata attachment**: Emits `LidarMeta` with point cloud data

## Prerequisites

### 1. Verify DL Streamer Installation

Ensure DL Streamer is properly compiled and the `g3dlidarparse` element is available:

```bash
gst-inspect-1.0 g3dlidarparse
```

If the element is found, you should see detailed information about the element, its properties, and pad templates.

### 2. Download Lidar Data and Configuration

Download the sample lidar binary dataset: 

```bash
DATA_DIR=velodyne
echo "Downloading sample LiDAR frames to ${DATA_DIR}..."
TMP_DIR=$(mktemp -d)
git clone --depth 1 --filter=blob:none --sparse https://github.com/open-edge-platform/edge-ai-suites.git "${TMP_DIR}/edge-ai-suites"
pushd "${TMP_DIR}/edge-ai-suites" >/dev/null
git sparse-checkout set metro-ai-suite/sensor-fusion-for-traffic-management/ai_inference/test/demo/kitti360/velodyne
popd >/dev/null
mkdir -p "${DATA_DIR}"
cp -a "${TMP_DIR}/edge-ai-suites/metro-ai-suite/sensor-fusion-for-traffic-management/ai_inference/test/demo/kitti360/velodyne"/* "${DATA_DIR}/"
rm -rf "${TMP_DIR}"
```
This will create a `velodyne` directory containing the binary files of the lidar data.

### Environment Variables

```sh
export GST_DEBUG=g3dlidarparse:5
```

## Running the Sample

**Usage:**
```bash
./g3dlidarparse.sh [LOCATION] [START_INDEX] [STRIDE] [FRAME_RATE]
```
or
```sh
GST_DEBUG=g3dlidarparse:5 gst-launch-1.0 multifilesrc location="velodyne/%06d.bin" start-index=260 caps=application/octet-stream ! g3dlidarparse stride=5 frame-rate=5 ! fakesink
```
or
```sh
GST_DEBUG=g3dlidarparse:5 gst-launch-1.0 multifilesrc location="pcd/%06d.pcd" start-index=1 caps=application/octet-stream ! g3dlidarparse stride=5 frame-rate=5 ! fakesink
```

Where:
* `location` points to a sequence of `.bin` or `.pcd` files (zero-padded index)
* `start-index` selects the starting frame index
* `stride` controls how often frames are processed
* `frame-rate` throttles the output frame rate

## Sample Output

The sample:
* prints the full `gst-launch-1.0` command to the console
* outputs LiDAR parser debug logs and metadata summaries

## See also
* [Elements overview](../../../../docs/source/elements/elements.md)
* [g3dlidarparse element](../../../../docs/source/elements/g3dlidarparse.md)
* [Samples overview](../../README.md)