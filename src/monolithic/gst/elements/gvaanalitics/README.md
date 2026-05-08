# GVA Analitics Plugin

## Overview

The GVA Analitics element is a GStreamer-based analytics plugin that detects tripwire crossings and object presence in configured zones using tracking metadata. It processes video frames with associated object tracking data and generates metadata events for security and surveillance applications.

## Features

- **Tripwire Detection**: Detects when tracked objects cross defined virtual lines
- **Zone Detection**: Detects when objects enter defined polygon or circular zones
- **Flexible Configuration**: JSON-based configuration via file or inline property
- **Metadata Output**: Generates `GstAnalyticsTripwireMtd` and `GstAnalyticsZoneMtd` for downstream processing
- **Watermark Support**: Optionally attaches `WatermarkDrawMeta`/`WatermarkCircleMeta` for visualization

## Properties

### config (string)
Path to JSON configuration file containing zones and/or tripwires definitions.

**Example:**
```
gvaanalitics config=/path/to/config.json
```

### zones (string)
Inline JSON string defining zones. Zones defined here are appended to any zones loaded from `config`.

**Example:**
```
gvaanalitics zones='[{"id":"zone_1","points":[{"x":500,"y":0},{"x":500,"y":1080},{"x":200,"y":1080}]}]'
```

### tripwires (string)
Inline JSON string defining tripwires. Tripwires defined here are appended to any tripwires loaded from `config`.

**Example:**
```
gvaanalitics tripwires='[{"id":"exit_line","points":[{"x":500,"y":0},{"x":500,"y":1080}]}]'
```

### draw-zones (boolean)
Enable or disable attachment of watermark metadata (WatermarkPolygonMeta) for drawing zone polygons. Default: true

**Example:**
```
gvaanalitics draw-zones=true
gvaanalitics draw-zones=false
```

### draw-tripwires (boolean)
Enable or disable attachment of watermark metadata (WatermarkDrawMeta) for drawing tripwire lines. Default: true

**Example:**
```
gvaanalitics draw-tripwires=true
gvaanalitics draw-tripwires=false
```

## Configuration Format

### Zone Configuration

Zones can be defined as either **polygons** or **circles**:

#### Polygon Zones
Polygons are defined with multiple vertices (minimum 3 points):

```json
{
  "zones": [
    {
      "id": "zone_1",
      "type": "polygon",
      "points": [
        {"x": 100, "y": 100},
        {"x": 500, "y": 100},
        {"x": 500, "y": 500},
        {"x": 100, "y": 500}
      ]
    }
  ]
}
```

#### Circular Zones
Circles are defined with a center point and radius:

```json
{
  "zones": [
    {
      "id": "zone_2",
      "type": "circle",
      "center": {"x": 500, "y": 500},
      "radius": 100
    }
  ]
}
```

#### Mixed Zone Configuration
Both polygon and circular zones can be used in the same configuration:

```json
{
  "zones": [
    {
      "id": "restricted_polygon",
      "type": "polygon",
      "points": [
        {"x": 400, "y": 200},
        {"x": 800, "y": 200},
        {"x": 800, "y": 600},
        {"x": 400, "y": 600}
      ]
    },
    {
      "id": "danger_circle",
      "type": "circle",
      "center": {"x": 960, "y": 540},
      "radius": 150
    }
  ]
}
```

**Note:** If `type` field is omitted, it defaults to "polygon".

### Tripwire Configuration

Tripwires are defined as lines with two endpoints:

```json
{
  "tripwires": [
    {
      "id": "exit_line",
      "points": [
        {"x": 500, "y": 0},
        {"x": 500, "y": 1080}
      ]
    },
    {
      "id": "entrance_line",
      "points": [
        {"x": 100, "y": 0},
        {"x": 100, "y": 1080}
      ]
    }
  ]
}
```

## Element Structure

### Base Class
`GstBaseTransform` (in-place transform)

### Input
- Video frames with `GstAnalyticsODMtd` and optionally `GstAnalyticsTrackingMtd` metadata

### Output
- Original video frames with attached:
  - `GstAnalyticsTripwireMtd`: Relation metadata for tripwire crossings (requires tracking)
  - `GstAnalyticsZoneMtd`: Relation metadata for zone presence
  - `WatermarkDrawMeta`: Line/polygon visualization (zones and tripwires)
  - `WatermarkCircleMeta`: Circle visualization (circular zones)

## Usage Example

### GStreamer Pipeline with Watermark Drawing

```bash
gst-launch-1.0 \
  filesrc location=video.mp4 ! \
  decodebin ! \
  gvadetect model=detection.xml ! \
  gvatrack ! \
  gvaanalitics config=analytics.json draw-zones=true draw-tripwires=true ! \
  gvawatermark ! \
  fakesink
```

### Configuration File (analytics.json)

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
      "id": "danger_zone_center",
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

## Metadata Output Format

### GstAnalyticsTripwireMtd
Attached as a relation to `GstAnalyticsODMtd` when a tripwire crossing is detected.

**Fields:**
- `tripwire_id` (string): Tripwire identifier
- `direction` (int): Crossing direction — `1` forward, `-1` backward

**API:**
```c
gboolean gst_analytics_relation_meta_add_tripwire_mtd(
    GstAnalyticsRelationMeta *relation_meta,
    const gchar *tripwire_id,
    gint direction,
    GstAnalyticsTripwireMtd *tripwire_mtd);

gboolean gst_analytics_tripwire_mtd_get_info(
    const GstAnalyticsTripwireMtd *handle,
    gchar **tripwire_id,
    gint *direction);
```

### GstAnalyticsZoneMtd
Attached as a relation to `GstAnalyticsODMtd` when an object center is inside a zone.

**Fields:**
- `zone_id` (string): Zone identifier

**API:**
```c
gboolean gst_analytics_relation_meta_add_zone_mtd(
    GstAnalyticsRelationMeta *relation_meta,
    const gchar *zone_id,
    GstAnalyticsZoneMtd *zone_mtd);

gboolean gst_analytics_zone_mtd_get_info(
    const GstAnalyticsZoneMtd *handle,
    gchar **zone_id);
```

## Debugging

Enable GStreamer debug logging for this element:

```bash
export GST_DEBUG=gvaanalitics:5
gst-launch-1.0 ... gvaanalitics ...
```
