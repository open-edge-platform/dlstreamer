# gvaanalytics

Analyzes video frames and applies analytics rules such as tripwires crossings and zones violatons.
Attaches watermark metadata for visualizing tripwires and zones on the output frames.

```bash
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

  config              : Path to JSON configuration file
                        flags: readable, writable
                        String. Default: null

  draw-tripwires      : Attach watermark metadata for drawing tripwires
                        flags: readable, writable
                        Boolean. Default: true

  draw-zones          : Attach watermark metadata for drawing zones
                        flags: readable, writable
                        Boolean. Default: true

  name                : The name of the object
                        flags: readable, writable
                        String. Default: "gvaanalytics0"

  parent              : The parent of the object
                        flags: readable, writable
                        Object of type "GstObject"

  qos                 : Handle Quality-of-Service events
                        flags: readable, writable
                        Boolean. Default: false

  tripwires           : Inline JSON tripwires configuration
                        flags: readable, writable
                        String. Default: null

  zones               : Inline JSON zones configuration
                        flags: readable, writable
                        String. Default: null
```

## Configuration

### Using a configuration file

Pass a path to a JSON configuration file using the `config` property:

```bash
gst-launch-1.0 ... ! gvaanalytics config=/path/to/analytics-config.json ! ...
```

### Using inline configuration

Configure tripwires and zones directly via properties:

```bash
gst-launch-1.0 ... ! gvaanalytics tripwires='[{"points": [[100, 100], [500, 100]]}]' zones='[{"points": [[0, 0], [640, 0], [640, 480], [0, 480]]}]' ! ...
```

### Drawing visualization

Control whether tripwires and zones are drawn as watermark metadata:

```bash
gst-launch-1.0 ... ! gvaanalytics draw-tripwires=true draw-zones=true ! gvawatermark ! ...
```
