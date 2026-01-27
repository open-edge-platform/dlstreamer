# G3D Lidar Parse Sample (Linux)

This sample demonstrates how to construct a LiDAR parsing pipeline using `gst-launch-1.0` on Linux.

## How It Works
`gst-launch-1.0` is a command-line utility included with the GStreamer media framework. It makes the construction and execution of media pipelines easy based on a simple string format. Pipelines are represented as strings containing the names of GStreamer elements separated by exclamation marks `!`. Users can specify properties of an element using `property`=`value` pairs after an element name and before the next exclamation mark.

This sample builds a GStreamer pipeline using the following elements:
* `multifilesrc` for reading a sequence of LiDAR frame files
* [g3dlidarparse](../../../../../docs/source/elements/g3dlidarparse.md) for parsing binary LiDAR data and attaching metadata
* `fakesink` for terminating the pipeline

## Prerequisites

### 1. Verify DL Streamer Installation

Ensure DL Streamer is properly compiled and the `g3dradarprocess` element is available:

```bash
gst-inspect-1.0 g3dlidarparse
```

If the element is found, you should see detailed information about the element, its properties, and pad templates.

### 2. Download Radar Data and Configuration

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
This will create a `velodyne` directory containing lidar binary files.

### Environment Variables

You can enable detailed logging for the LiDAR parser using `GST_DEBUG`:

```sh
export GST_DEBUG=g3dlidarparse:5
```

## Running
```sh
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
* [Elements overview](../../../../../docs/source/elements/elements.md)
* [g3dlidarparse element](../../../../../docs/source/elements/g3dlidarparse.md)
