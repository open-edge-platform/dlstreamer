# gvaanalytics

Analyzes video frames and applies analytics rules such as tripwires crossings and zones violations.
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

Example `analytics-config.json`:

```json
{
  "zones": [
    {
      "id": "restricted_area",
      "type": "polygon",
      "points": [
        {"x": 400, "y": 200},
        {"x": 800, "y": 200},
        {"x": 800, "y": 600},
        {"x": 400, "y": 600}
      ]
    },
    {
      "id": "danger_zone",
      "type": "circle",
      "center": {"x": 960, "y": 540},
      "radius": 150
    }
  ],
  "tripwires": [
    {
      "id": "entrance",
      "points": [
        {"x": 960, "y": 0},
        {"x": 960, "y": 1080}
      ]
    }
  ]
}
```

### Using inline configuration

Configure tripwires and zones directly via properties:

```bash
gst-launch-1.0 ... ! gvaanalytics tripwires='[{"id":"entrance","points":[{"x":100,"y":100},{"x":500,"y":100}]}]' zones='[{"id":"zone_1","points":[{"x":0,"y":0},{"x":640,"y":0},{"x":640,"y":480},{"x":0,"y":480}]}]' ! ...
```

### Drawing visualization

Control whether tripwires and zones are drawn as watermark metadata:

```bash
gst-launch-1.0 ... ! gvaanalytics draw-tripwires=true draw-zones=true ! gvawatermark ! ...
```
