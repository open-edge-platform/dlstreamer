# gvatrack

Performs object tracking using zero-term, zero-term-imageless, short-term-imageless or
deepsort tracking algorithms.

Zero-term tracking assigns unique object IDs and requires object detection to run on every frame.
Short-term tracking allows for tracking objects between frames, reducing the need to run object detection on each frame.
Imageless tracking forms object associations based on the movement and shape of objects, and does not use image data.
Deep SORT tracking combines motion prediction (Kalman filter) with appearance features (deep learning re-identification) for robust multi-object tracking with reduced ID switches.

Please refer to [Object Tracking](../dev_guide/object_tracking.md)
for more information on the element properties.

```bash
Pad Templates:
 SINK template: 'sink'
   Availability: Always
   Capabilities:
     video/x-raw
                format: { (string)BGRx, (string)BGRA, (string)BGR, (string)NV12, (string)I420 }
                 width: [ 1, 2147483647 ]
                height: [ 1, 2147483647 ]
             framerate: [ 0/1, 2147483647/1 ]
     video/x-raw(memory:DMABuf)
                format: { (string)RGBA, (string)I420 }
                 width: [ 1, 2147483647 ]
                height: [ 1, 2147483647 ]
             framerate: [ 0/1, 2147483647/1 ]
     video/x-raw(memory:VASurface)
                format: { (string)NV12 }
                 width: [ 1, 2147483647 ]
                height: [ 1, 2147483647 ]
             framerate: [ 0/1, 2147483647/1 ]
     video/x-raw(memory:VAMemory)
                format: { (string)NV12 }
                 width: [ 1, 2147483647 ]
                height: [ 1, 2147483647 ]
             framerate: [ 0/1, 2147483647/1 ]

 SRC template: 'src'
   Availability: Always
   Capabilities:
     video/x-raw
                format: { (string)BGRx, (string)BGRA, (string)BGR, (string)NV12, (string)I420 }
                 width: [ 1, 2147483647 ]
                height: [ 1, 2147483647 ]
             framerate: [ 0/1, 2147483647/1 ]
     video/x-raw(memory:DMABuf)
                format: { (string)RGBA, (string)I420 }
                 width: [ 1, 2147483647 ]
                height: [ 1, 2147483647 ]
             framerate: [ 0/1, 2147483647/1 ]
     video/x-raw(memory:VASurface)
                format: { (string)NV12 }
                 width: [ 1, 2147483647 ]
                height: [ 1, 2147483647 ]
             framerate: [ 0/1, 2147483647/1 ]
     video/x-raw(memory:VAMemory)
                format: { (string)NV12 }
                 width: [ 1, 2147483647 ]
                height: [ 1, 2147483647 ]
             framerate: [ 0/1, 2147483647/1 ]

Element has no clocking capabilities.
Element has no URI handling capabilities.

Pads:
 SINK: 'sink'
   Pad Template: 'sink'
 SRC: 'src'
   Pad Template: 'src'

Element Properties:
 config              : Comma separated list of KEY=VALUE parameters specific to platform/tracker. Please see user guide for more details
                       flags: readable, writable
                       String. Default: null
 device              : Target device for tracking. This version supports only CPU device
                       flags: readable, writable
                       String. Default: ""
 name                : The name of the object
                       flags: readable, writable, 0x2000
                       String. Default: "gvatrack0"
 parent              : The parent of the object
                       flags: readable, writable, 0x2000
                       Object of type "GstObject"
 qos                 : Handle Quality-of-Service events
                       flags: readable, writable
                       Boolean. Default: false
 tracking-type       : Tracking algorithm used to identify the same object in multiple frames. Please see user guide for more details
                       flags: readable, writable
                       Enum "GstGvaTrackingType" Default: 0, "zero-term"
                          (0): zero-term        - Zero-term tracker
                          (1): short-term-imageless - Short-term imageless tracker
                          (2): zero-term-imageless - Zero-term imageless tracker
                          (3): deep-sort        - Deep SORT tracker , Requires external feature extractor (using gvainference before gvatrack):
                               .. gvainference model=mars_small128_fp32.xml device=GPU inference-region=roi-list ! queue ! gvatrack tracking-type=deep-sort ..
 deepsort-trck-cfg   : Comma separated list of KEY=VALUE parameters to configure Deep SORT tracker behavior.
                       Available parameters:
                         - max_iou_distance: Maximum IoU distance threshold for matching detections to tracks (default: 0.7)
                           Lower values = stricter spatial matching. Typical range: 0.5-0.8
                         - max_age: Maximum number of frames a track survives without detection before deletion (default: 30)
                           Higher values = tracks survive longer during occlusions. Increase to 60-90 to reduce object loss.
                         - n_init: Number of consecutive detections required to confirm a new track (default: 3)
                           Lower values = faster track initialization but more false positives.
                         - max_cosine_distance: Maximum cosine distance for appearance feature matching (default: 0.2)
                           Higher values = more lenient appearance matching. Increase to 0.3-0.4 to handle appearance variations.
                         - nn_budget: Maximum number of appearance features stored per track (default: 100)
                           Higher values = better appearance matching but more memory usage.
                       Example: deepsort-trck-cfg="max_age=60,max_cosine_distance=0.3,max_iou_distance=0.6"
                       flags: readable, writable
                       String. Default: null
```
