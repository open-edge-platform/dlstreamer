# g3dlidarparse

A GST element designed for LiDAR point-cloud ingestion. It reads raw LiDAR frames (BIN/PCD), applies stride/frame-rate thinning, and outputs buffers enriched with LidarMeta (points, frame_id, timestamps, stream_id) for downstream fusion, analytics, or visualization.

```sh
Factory Details:
	Rank                     none (0)
	Long-name                G3D Lidar Parser
	Klass                    Filter/Converter
	Description              Parses binary lidar data to vector float format with stride and frame rate control (g3dlidarparse)
	Author                   Your Name <your.email@example.com>

Plugin Details:
	Name                     gst3delements
	Description              3D elements (g3dlidarparse, lidarmeta)
	Filename                 /usr/local/lib/gstreamer-1.0/libgst3delements.so
	Version                  1.0
	License                  LGPL
	Source module            dlstreamer
	Binary package           dlstreamer
	Origin URL               https://github.com/dlstreamer/dlstreamer

GObject
 +----GInitiallyUnowned
			 +----GstObject
						 +----GstElement
									 +----GstBaseTransform
												 +----GstG3DLidarParse

Element Flags:

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

