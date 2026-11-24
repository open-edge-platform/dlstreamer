# gvaroiscale

Finds matching object-detection ROIs in `GstAnalyticsRelationMeta`, crops each ROI from an
RGB frame, preserves aspect ratio, and centers each resized image on a black canvas. Each
input frame produces one output buffer per selected ROI; frames without a valid ROI are dropped.
ROIs are processed in descending confidence order. Same-class detections with an
intersection-over-union (IoU) above 0.5 are deduplicated, keeping the detection with the
highest confidence.

If an input buffer carries the marked shared ROI created by `gvaframeselect`, that ROI takes
precedence over the detector boxes and produces one crop. In that case, target-class and
confidence filtering has already been applied by `gvaframeselect`.

| Property | Type | Range | Default | Description |
|---|---|---|---|---|
| `target-classes` | String | — | (empty) | Comma-separated detection labels to consider; an empty value allows all classes. |
| `confidence-threshold` | Double | [0.0, 1.0] | 0.0 | Minimum detection confidence required to select an ROI. |
| `out-width` | Unsigned Integer | [1, 8192] | 448 | Output width; configure for the model input expected by the next element. |
| `out-height` | Unsigned Integer | [1, 8192] | 448 | Output height; configure for the model input expected by the next element. |
| `output-dir` | String | — | (empty) | If set, each scaled ROI frame is also saved as `frame_XXXXXXXX.jpg` in this directory. The directory is created if needed. |

The element accepts and outputs `video/x-raw,format=RGB`. Output dimensions are negotiated
from `out-width` and `out-height`.
Set these dimensions according to the downstream model's input requirements. Models that accept
dynamic image sizes or resize inputs internally may not require an exact match; check the model
documentation. The defaults are not universal.
The output directory is optional; an empty value disables image saving. File numbering advances
for each crop, starts at zero, and restarts on each new stream. Every output crop carries a
`GstAnalyticsODMtd` with the class and confidence of its source detection; its box is the area the
scaled ROI occupies inside the output frame, excluding the black padding. Outputs from the same input
frame retain its PTS, duration and buffer offset.
When `target-classes` is set, only ROIs whose labels match one of the comma-separated values are
considered. Whitespace around labels is ignored.

```sh
... ! gvaframeselect frame-budget=8 target-classes=person,vehicle confidence-threshold=0.5 ! \
  videoconvert ! video/x-raw,format=RGB ! \
  gvaroiscale out-width=448 out-height=448 target-classes=person,vehicle ! gvagenai ...
```

For a runnable command-line example, see the [gvaroiscale sample](../../../samples/gstreamer/gst_launch/gvaroiscale/README.md).

## Element Details (gst-inspect-1.0)

```text
Pad Templates:
  SINK template: 'sink'
    Availability: Always
    Capabilities:
      video/x-raw
         format: RGB (gchararray)
        width: [ 1, 8192 ] (GstIntRange)
       height: [ 1, 8192 ] (GstIntRange)

  SRC template: 'src'
    Availability: Always
    Capabilities:
      video/x-raw
                 format: RGB (gchararray)
                  width: [ 1, 8192 ] (GstIntRange)
                 height: [ 1, 8192 ] (GstIntRange)

Element has no clocking capabilities.
Element has no URI handling capabilities.

Pads:
  SINK: 'sink'
    Pad Template: 'sink'
  SRC: 'src'
    Pad Template: 'src'

Element Properties:

  confidence-threshold: Minimum detection confidence required to select an ROI
                        flags: readable, writable
                        Double. Range: 0 - 1 Default: 0
  name                : The name of the object
                        flags: readable, writable
                        String. Default: "gvaroiscale0"
  out-height          : Scaled ROI output height
                        flags: readable, writable
                        Unsigned Integer. Range: 1 - 8192 Default: 448
  out-width           : Scaled ROI output width
                        flags: readable, writable
                        Unsigned Integer. Range: 1 - 8192 Default: 448
  output-dir          : If set, each scaled ROI frame is also saved as a JPEG in this directory
                        flags: readable, writable
                        String. Default: null
  parent              : The parent of the object
                        flags: readable, writable
                        Object of type "GstObject"
  qos                 : Handle Quality-of-Service events
                        flags: readable, writable
                        Boolean. Default: false
  target-classes      : Comma-separated detection labels to consider; empty means all classes
                        flags: readable, writable
                        String. Default: ""
```