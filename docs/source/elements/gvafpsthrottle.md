# gvafpsthrottle

The `gvafpsthrottle` element throttles framerate by limiting the rate at which buffers pass through. It sleeps between buffers to ensure the pipeline doesn't exceed a specified target framerate, independent of sink synchronization. Unlike `videorate` element which can both increase and decrease framerate, this element does not duplicate or drop frames to match the framerate. It cannot increase FPS, any slowdown in upstream processing cannot be recovered.

```none
Pad Templates:
  SINK template: 'sink'
    Availability: Always
    Capabilities:
      ANY

  SRC template: 'src'
    Availability: Always
    Capabilities:
      ANY

Element has no clocking capabilities.
Element has no URI handling capabilities.

Pads:
  SINK: 'sink'
    Pad Template: 'sink'
  SRC: 'src'
    Pad Template: 'src'

Element Properties:

  name                : The name of the object
                        flags: readable, writable
                        String. Default: "gvafpsthrottle0"

  parent              : The parent of the object
                        flags: readable, writable
                        Object of type "GstObject"

  qos                 : Handle Quality-of-Service events
                        flags: readable, writable
                        Boolean. Default: false

  target-fps          : Target frames per second to limit buffer flow
                        flags: readable, writable
                        Double. Range:               0 -   1.797693e+308 Default:               0
```
