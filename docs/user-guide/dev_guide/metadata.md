# Metadata

## Overview

DL Streamer is transitioning to the
[GStreamer Analytics](https://gstreamer.freedesktop.org/documentation/analytics/index.html)
metadata library as the primary way to represent inference results. The legacy
metadata API remains functional but is deprecated.

## GStreamer Analytics Metadata

This is the **recommended** metadata API for new code. All metadata for a
buffer is stored inside a single `GstAnalyticsRelationMeta` container with
typed entries (`GstAnalyticsMtd`) connected by relations (`CONTAIN`,
`IS_PART_OF`, `RELATE_TO`).

Key types:

| Type | Description |
|------|-------------|
| `GstAnalyticsODMtd` | Object detection (bbox, label, confidence) |
| `GstAnalyticsClsMtd` | Classification |
| `GstAnalyticsTrackingMtd` | Object tracking |
| `GstAnalyticsKeypointMtd` | Single keypoint |
| `GstAnalyticsGroupMtd` | Ordered group of metadata |
| `GstAnalyticsKeypointDescriptor` | Static keypoint layout registry (DL Streamer extension) |
| `GstAnalyticsZoneMtd` | Zone presence (zone ID) — DL Streamer extension |
| `GstAnalyticsTripwireMtd` | Tripwire crossing (tripwire ID, direction) — DL Streamer extension |

For the full API documentation, keypoint descriptor details, and code
examples, see [GStreamer Analytics Metadata](./metadata_analytics.md).

## Custom Watermark Metadata

`gvawatermark` automatically renders custom drawing primitives attached directly to GStreamer buffers
using the DLStreamer watermark metadata types.

Key types:

| Type                  | Description |
|-----------------------|-------------|
| `WatermarkDrawMeta`   | Polygon or polyline defined by an ordered list of (x, y) coordinate pairs (max 128 pairs) |
| `WatermarkCircleMeta` | Circle defined by center (cx, cy), radius, color, and thickness |
| `WatermarkTextMeta`   | Text label at position (x, y) with font, scale, color, and optional background |

For the full API documentation, see [Watermark Metadata](./metadata_watermark.md).

## Legacy Metadata (deprecated)

> **DEPRECATED:** The legacy metadata API based on
> `GstVideoRegionOfInterestMeta`, `GstGVATensorMeta`, and `GstGVAJSONMeta`
> is deprecated and will be removed in the future. New code should use
> the GStreamer Analytics API described below.

The legacy API attaches
[GstVideoRegionOfInterestMeta](https://gstreamer.freedesktop.org/documentation/video/gstvideometa.html?gi-language=c#GstVideoRegionOfInterestMeta)
to buffers with bounding box coordinates (`x`, `y`, `w`, `h`) and a
`GList *params` of `GstStructure` entries holding additional inference results
(detection confidence, classification labels, keypoints, etc.).
Two additional custom metadata types are defined:

- **GstGVATensorMeta** — raw tensor output from `gvainference`
- **GstGVAJSONMeta** — JSON conversion output from `gvametaconvert`

For reference documentation of the legacy API, see
[Legacy Metadata](./metadata_legacy.md).

## Element input/output summary

| Element | Description | Input | Output (Analytics) | Output (Legacy) |
|---|---|---|---|---|
| `gvadetect` (full-frame) | Object detection on full frame | GstBuffer | ODMtd, ClsMtd, KeypointGroupMtd | ROI + GstStructure params |
| `gvadetect` (roi-list) | Object detection per ROI | GstBuffer + ROI/ODMtd | ODMtd, ClsMtd, KeypointGroupMtd | ROI (with parent_id) + GstStructure params |
| `gvaclassify` (roi-list) | Object classification per ROI | GstBuffer + ROI/ODMtd | ClsMtd, KeypointGroupMtd | extended ROI params |
| `gvaclassify` (full-frame) | Full-frame classification | GstBuffer | — | GstGVATensorMeta |
| `gvainference` (full-frame) | Generic full-frame inference | GstBuffer | — | GstGVATensorMeta |
| `gvainference` (roi-list) | Generic inference per ROI | GstBuffer + ROI/ODMtd | — | extended ROI params |
| `gvatrack` | Object tracking | GstBuffer + ROI/ODMtd | TrackingMtd | ROI + object_id param |
| `gvaanalytics` | Zone and tripwire analytics | GstBuffer + ODMtd + TrackingMtd | ZoneMtd (related to ODMtd), TripwireMtd (related to ODMtd) | — |
| `gvametaconvert` | Metadata → JSON | GstBuffer + ROI/ODMtd (+ related ClsMtd, KeypointGroupMtd, TrackingMtd) + GstGVATensorMeta | — | GstGVAJSONMeta |
| `gvametapublish` | JSON → MQTT/Kafka/File | GstBuffer + GstGVAJSONMeta | — | — |
| `gvametaaggregate` | Merge from multiple streams | GstBuffer + any metadata | ODMtd, ClsMtd, KeypointGroupMtd | ROI + GstStructure params, GstGVATensorMeta |
| `gvawatermark` | Overlay on video | GstBuffer + ROI/ODMtd (+ related ClsMtd, KeypointGroupMtd, TrackingMtd) + GstGVATensorMeta + WatermarkDrawMeta + WatermarkCircleMeta + WatermarkTextMeta | — | — |
| `gvagenai` | VLM inference on video frames | GstBuffer | ClsMtd | GstGVATensorMeta, GstGVAJSONMeta |
| `gvaaudiotranscribe` | Speech recognition (Whisper) | GstBuffer (audio) | ClsMtd | — |

<!--hide_directive
:::{toctree}
:maxdepth: 1
:hidden:

metadata_analytics
metadata_watermark
metadata_legacy
:::
hide_directive-->
