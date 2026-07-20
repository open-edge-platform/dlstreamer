# API Reference

The DL Streamer API consists of two parts:

1. **Elements and their properties** — the GStreamer elements provided by DL Streamer and the properties used to configure them.
2. **Interface between elements** — the metadata attached to buffers that flows between elements in a pipeline.

## Elements & Properties

DL Streamer provides GStreamer elements for inference, tracking, analytics, and
post-processing. Each element exposes properties that control its behaviour
(model path, device, thresholds, etc.).

> **[Elements Reference](../elements/elements.md)** — full list of elements with
> property descriptions.

---

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