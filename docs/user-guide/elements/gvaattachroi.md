# gvaattachroi

Provides the ability to define one or more regions of interest to
perform inference on (instead of the full frame).

```text
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
                format: { (string)DMA_DRM }
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
                format: { (string)DMA_DRM }
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

  file-path           : Absolute path to input file with ROIs to attach to buffer.
                        flags: readable, writable
                        String. Default: null

  mode                : Mode used to attach ROIs from JSON file
                        flags: readable, writable
                        Enum "GstGVAAttachRoiMode" Default: 0, "in-order"
                          (0): in-order         - Attach ROIs in order. Number of frames in the pipeline must match to number of ROIs in JSON.
                          (1): in-loop          - Attach ROIs in cyclic manner. Same as in-order, but for cases when the number of frames in the pipeline exceeds ROIs in JSON.
                          (2): by-timestamp     - Attach ROIs using timestamping. ROIs in JSON file must be timestamped.

  name                : The name of the object
                        flags: readable, writable
                        String. Default: "gvaattachroi0"

  parent              : The parent of the object
                        flags: readable, writable
                        Object of type "GstObject"

  qos                 : Handle Quality-of-Service events
                        flags: readable, writable
                        Boolean. Default: false

  roi                 : Specifies pixel absolute coordinates of ROI to attach to buffer in form: x_top_left,y_top_left,x_bottom_right,y_bottom_right
                        flags: readable, writable
                        String. Default: null
```
