# API Reference

## Inference Output & Metadata

DL Streamer represents inference results (detections, classifications, tracking,
keypoints, zone/tripwire analytics) as structured metadata attached to GStreamer
buffers. The primary API is based on
[GStreamer Analytics](https://gstreamer.freedesktop.org/documentation/analytics/index.html)
(`GstAnalyticsRelationMeta`), with typed entries connected by relations.

### Full documentation

For detailed API documentation, code examples, element input/output tables, and
migration guidance from the legacy API, see:

> **[Metadata Developer Guide](../dev_guide/metadata.md)** — primary reference
> for reading and writing inference output in DL Streamer pipelines.

Sub-pages:

- [GStreamer Analytics Metadata](../dev_guide/metadata_analytics.md) — recommended API, keypoint descriptors, code examples
- [Watermark Metadata](../dev_guide/metadata_watermark.md) — custom drawing primitives for `gvawatermark`
- [Legacy Metadata](../dev_guide/metadata_legacy.md) — deprecated `GstVideoRegionOfInterestMeta` / `GstGVATensorMeta` API