# G3D Lidar Parse Sample (Linux)

This sample demonstrates how to construct a LiDAR parsing pipeline using `gst-launch-1.0` on Linux.

## How It Works
`gst-launch-1.0` is a command-line utility included with the GStreamer media framework. It makes the construction and execution of media pipelines easy based on a simple string format. Pipelines are represented as strings containing the names of GStreamer elements separated by exclamation marks `!`. Users can specify properties of an element using `property`=`value` pairs after an element name and before the next exclamation mark.

This sample builds a GStreamer pipeline using the following elements:
* `multifilesrc` for reading a sequence of LiDAR frame files
* [g3dlidarparse](../../../../../docs/source/elements/g3dlidarparse.md) for parsing binary LiDAR data and attaching metadata
* `fakesink` for terminating the pipeline

## Environment Variables

You can enable detailed logging for the LiDAR parser using `GST_DEBUG`:

```sh
export GST_DEBUG=lidarparse:5
```

## Running
```sh
./g3dlidarparse.sh [FILE_LOCATION]
```
or
```sh
GST_DEBUG=lidarparse:5 gst-launch-1.0 multifilesrc location="velodyne/%06d.bin" start-index=260 caps=application/octet-stream ! lidarparse stride=5 frame-rate=5 ! fakesink
```

Where:
* `location` points to a sequence of `.bin` files (zero-padded index)
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
