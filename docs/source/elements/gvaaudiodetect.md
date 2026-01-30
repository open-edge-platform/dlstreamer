# gvaaudiodetect

Performs audio event detection using AclNet model.

```bash
Pad Templates:
  SRC template: 'src'
    Availability: Always
    Capabilities:
      audio/x-raw
                format: S16LE
                  rate: 16000
              channels: 1
                layout: interleaved

  SINK template: 'sink'
    Availability: Always
    Capabilities:
      audio/x-raw
                format: S16LE
                  rate: 16000
              channels: 1
                layout: interleaved

Element has no clocking capabilities.
Element has no URI handling capabilities.

Pads:
  SINK: 'sink'
    Pad Template: 'sink'
  SRC: 'src'
    Pad Template: 'src'

Element Properties:
  device              : Target device for inference. Please see OpenVINO™ Toolkit documentation for list of supported devices.
                        flags: readable, writable
                        String. Default: "CPU"
  model               : Path to inference model network file
                        flags: readable, writable
                        String. Default: null
  model-proc          : Path to JSON file with description of input/output layers pre-processing/post-processing
                        flags: readable, writable
                        String. Default: null
  name                : The name of the object
                        flags: readable, writable
                        String. Default: "gvaaudiodetect0"
  parent              : The parent of the object
                        flags: readable, writable
                        Object of type "GstObject"
  qos                 : Handle Quality-of-Service events
                        flags: readable, writable
                        Boolean. Default: false
  sliding-window      : Sliding window increment in seconds. Audio event detection is performed using a window of 1 second with an increment specified by the user. The default value of 1 implies no overlap between successive inferences. An increment value of 0.5 implies inference requests every 0.5 seconds with 0.5 seconds overlap
                        flags: readable, writable
                        Float. Range: 0.1 - 1 Default: 1
  threshold           : When model-proc contains only array of labels, event type with confidence value above the threshold set here will be added to metadata
                        flags: readable, writable
                        Float. Range: 0 - 1 Default: 0.5
```
