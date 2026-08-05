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
| `GstAnalytics3DODMtd` | 3D object detection (oriented box, class, confidence, sensor modality) — DL Streamer extension |
| `GstAnalyticsClsMtd` | Classification |
| `GstAnalyticsTrackingMtd` | Object tracking |
| `GstAnalyticsKeypointMtd` | Single keypoint |
| `GstAnalyticsGroupMtd` | Ordered group of metadata |
| `GstAnalyticsSegmentationMtd` | Semantic segmentation (class-index mask) |
| `GstAnalyticsTensorMtd` | Raw tensor payload (used for instance-segmentation soft masks) |
| `GstAnalyticsKeypointDescriptor` | Static keypoint layout registry — DL Streamer extension |
| `GstAnalyticsZoneMtd` | Zone presence (zone ID) — DL Streamer extension |
| `GstAnalyticsTripwireMtd` | Tripwire crossing (tripwire ID, direction) — DL Streamer extension |

For the full API documentation, keypoint descriptor details, and code
examples, see [GStreamer Analytics Metadata](./metadata_analytics.md).\
You can also check out our post at [discourse.gstreamer.org](https://discourse.gstreamer.org/t/gstanalytics-adoption-in-dlstreamer-implementation-status-and-questions/5820)
for input from GStreamer community.

## Custom Watermark Metadata

`gvawatermark` automatically renders custom drawing primitives attached directly to GStreamer buffers
using the DL Streamer watermark metadata types.

Key types:

| Type                  | Description |
|-----------------------|-------------|
| `WatermarkDrawMeta`   | Polygon or polyline defined by an ordered list of (x, y) coordinate pairs (max 128 pairs) |
| `WatermarkCircleMeta` | Circle defined by center (cx, cy), radius, color, and thickness |
| `WatermarkTextMeta`   | Text label at position (x, y) with font, scale, color, and optional background |

For the full API documentation, see [Watermark Metadata](./metadata_watermark.md).

## 3D Sensor Metadata

DL Streamer's 3D pipelines (LiDAR and mmWave radar) carry metadata at two
levels:

- **3D detections** — oriented 3D bounding boxes are stored as `GstAnalytics3DODMtd`
  inside the buffer's `GstAnalyticsRelationMeta`.
- **Raw-sensor metadata** — standalone `GstMeta` types attached directly to the
  buffer that describe the sensor frame and its low-level results:

Key types:

| Type | Description |
|------|-------------|
| `GstAnalytics3DODMtd` | 3D object detection — oriented box (x, y, z, l, w, h, yaw, pitch, roll), class, confidence, sensor modality. |
| `LidarMeta` | Raw LiDAR frame framing: point count, frame/stream id, timestamps. The point cloud itself lives in the buffer payload. |
| `GstRadarProcessMeta` | Radar signal-processing results: point clouds, clusters, and per-object tracks. |

`GstAnalytics3DODMtd` is documented in [GStreamer Analytics Metadata](./metadata_analytics.md#gstanalytics3dodmtd).
For the raw-sensor `LidarMeta` and `GstRadarProcessMeta` types,
see [3D Sensor Metadata](./metadata_3d.md).

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
| `gvadetect` (full-frame) | Object detection on full frame | GstBuffer | ODMtd, ClsMtd, GstAnalyticsGroupMtd, GstAnalyticsKeypointMtd, GstAnalyticsTensorMtd | ROI + GstStructure params |
| `gvadetect` (roi-list) | Object detection per ROI | GstBuffer + ROI + ODMtd | ODMtd, ClsMtd, GstAnalyticsGroupMtd, GstAnalyticsKeypointMtd, GstAnalyticsTensorMtd | ROI (with parent_id) + GstStructure params |
| `gvaclassify` (roi-list) | Object classification per ROI | GstBuffer + ROI + ODMtd | ClsMtd, GstAnalyticsGroupMtd, GstAnalyticsKeypointMtd, GstAnalyticsSegmentationMtd | extended ROI params |
| `gvaclassify` (full-frame) | Full-frame classification | GstBuffer | ClsMtd, GstAnalyticsGroupMtd, GstAnalyticsKeypointMtd, GstAnalyticsSegmentationMtd | GstGVATensorMeta |
| `gvainference` (full-frame) | Generic full-frame inference | GstBuffer | — | GstGVATensorMeta |
| `gvainference` (roi-list) | Generic inference per ROI | GstBuffer + ROI + ODMtd | — | extended ROI params |
| `gvatrack` | Object tracking | GstBuffer + ROI + ODMtd | TrackingMtd | ROI + object_id param |
| `gvaanalytics` | Zone and tripwire analytics | GstBuffer + ODMtd + TrackingMtd | ZoneMtd, TripwireMtd, WatermarkDrawMeta, WatermarkCircleMeta | — |
| `gvametaconvert` | Metadata → JSON | GstBuffer + ROI + ODMtd + ClsMtd + GstAnalyticsGroupMtd + GstAnalyticsKeypointMtd + TrackingMtd + GstAnalyticsSegmentationMtd + GstAnalyticsTensorMtd + ZoneMtd + TripwireMtd + 3DODMtd + LidarMeta + GstRadarProcessMeta + GstAnalyticsBatchMeta + GstGVATensorMeta | — | GstGVAJSONMeta |
| `gvametapublish` | JSON → MQTT/Kafka/File | GstBuffer + GstGVAJSONMeta | — | — |
| `gvametaaggregate` | Merge from multiple streams | GstBuffer + ROI + GstGVATensorMeta + ODMtd + ClsMtd + GstAnalyticsGroupMtd + GstAnalyticsKeypointMtd + GstAnalyticsSegmentationMtd + GstAnalyticsTensorMtd | ODMtd, ClsMtd, GstAnalyticsGroupMtd, GstAnalyticsKeypointMtd, GstAnalyticsSegmentationMtd, GstAnalyticsTensorMtd | ROI + GstStructure params, GstGVATensorMeta |
| `gvawatermark` | Overlay on video | GstBuffer + ROI + ODMtd + ClsMtd + GstAnalyticsGroupMtd + GstAnalyticsKeypointMtd + TrackingMtd + GstAnalyticsSegmentationMtd + GstAnalyticsTensorMtd + GstGVATensorMeta + WatermarkDrawMeta + WatermarkCircleMeta + WatermarkTextMeta | — | — |
| `gvagenai` | VLM inference on video frames | GstBuffer | ClsMtd | GstGVAJSONMeta |
| `gvaaudiotranscribe` | Speech recognition (Whisper) | GstBuffer (audio) | ClsMtd | — |
| `gvastreammux` | Merge streams into one batch | GstBuffer (per stream) + metadata | GstAnalyticsBatchMeta (container) | — |
| `gvastreamdemux` | Split batch back to per-stream buffers | GstAnalyticsBatchMeta (container) | per-stream GstBuffer + metadata | — |
| `g3dradarprocess` | mmWave radar signal processing | GstBuffer | GstRadarProcessMeta | — |
| `g3dlidarsrc` | Live LiDAR capture from a device → point cloud | — (source element) | LidarMeta | — |
| `g3dlidarparse` | Parse raw LiDAR frame → point cloud | GstBuffer | LidarMeta | — |
| `g3dinference` | 3D detection on LiDAR | GstBuffer + LidarMeta | 3DODMtd | — |
| `g3dobjectfuser` | Fuse camera 2D with 3D (LiDAR/radar) | GstAnalyticsBatchMeta + ODMtd + 3DODMtd + GstRadarProcessMeta | 3DODMtd + TrackingMtd | — |
| `g3drender` | Render camera 2D + 3D point cloud, overlay detections | GstAnalyticsBatchMeta + ODMtd + 3DODMtd + TrackingMtd | — | — |

<!--hide_directive
:::{toctree}
:maxdepth: 1
:hidden:

metadata_analytics
metadata_3d
metadata_watermark
metadata_legacy
:::
hide_directive-->
