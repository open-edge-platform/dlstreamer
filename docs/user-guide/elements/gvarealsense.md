# gvarealsense

Reads depth and RGB data from Real Sense camera and provides
it in a PCD format suitable for further processing in DL Streamer
pipelines.

```bash
Pad Templates:
  SRC template: 'src'
    Availability: Always
    Capabilities:
      video/x-raw
               format: { (string)RgbZ16 }
                  width: [ 1, 2147483647 ]
                  height: [ 1, 2147483647 ]
             framerate: [ 0/1, 2147483647/1 ]

Element Flags:
  - SOURCE


Element has no clocking capabilities.
Element has no URI handling capabilities.

Pads:
  SRC: 'src'
    Pad Template: 'src'

Element Properties:
  camera              : Real Sense camera device
                        flags: readable, writable, changeable only in NULL or READY state
                        String. Default: null
```
