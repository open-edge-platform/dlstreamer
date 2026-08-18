# gvaanalytics

Analyzes video frames and applies analytics rules such as tripwire crossings, zone violations, and optional dwell-time tracking.
Attaches metadata for downstream analytics and optional watermark metadata for visualizing tripwires and zones on output frames.

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

  evaluation-point    : Point used for zone and tripwire evaluation
                        flags: readable, writable
                        Enum "GvaAnalyticsEvaluationPoint" Default: 0, "center"
                           (0): center          - Object center
                           (1): bottom-center   - Object bottom-center

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

### Zone options for dwell time and drawing

Each zone supports optional parameters beyond geometry:

- `track-dwell-time` (boolean, default `false`): when `true`, `gvaanalytics` tracks how long each tracked object stays inside this zone.
- `object-retention` (number, default `0.5` seconds): grace period used to keep zone state after an object leaves the zone.
- `color` (object): drawing color for the zone when `draw-zones=true`.
- `thickness` (integer): drawing line thickness for the zone when `draw-zones=true`.

Example zone configuration with dwell options:

```json
{
  "zones": [
    {
      "id": "pathway",
      "type": "polygon",
      "points": [
        {"x": 45, "y": 395},
        {"x": 110, "y": 431},
        {"x": 445, "y": 323},
        {"x": 368, "y": 279}
      ],
      "track-dwell-time": true,
      "object-retention": 1.0,
      "color": {"r": 255, "g": 0, "b": 0},
      "thickness": 3
    }
  ]
}
```

When dwell tracking is enabled, `gvaanalytics` attaches `GstAnalyticsDwellTimeMtd` related to each object detection in the zone.
This requires tracking metadata upstream, typically by adding `gvatrack` before `gvaanalytics` in the pipeline.

### Using inline configuration

Configure tripwires and zones directly via properties:

```bash
gst-launch-1.0 ... ! gvaanalytics \
  tripwires='[{"id":"entrance","points":[{"x":960,"y":0},{"x":960,"y":1080}]}]' \
  zones='[{"id":"restricted_area","type":"polygon","points":[{"x":400,"y":200},{"x":800,"y":200},{"x":800,"y":600},{"x":400,"y":600}]},{"id":"danger_zone","type":"circle","center":{"x":960,"y":540},"radius":150}]' \
  ! ...
```

### Drawing visualization

Control whether tripwires and zones are drawn as watermark metadata:

```bash
gst-launch-1.0 ... ! gvaanalytics draw-tripwires=true draw-zones=true ! gvawatermark ! ...
```

### Evaluation point

Control which object point is used for zone and tripwire logic:

```bash
# Default behavior
gst-launch-1.0 ... ! gvaanalytics evaluation-point=center ! ...

# Use bottom-center of bounding box
gst-launch-1.0 ... ! gvaanalytics evaluation-point=bottom-center ! ...
```
