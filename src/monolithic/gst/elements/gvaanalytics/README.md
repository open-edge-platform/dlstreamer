# GVA Analytics Plugin

## Overview

The GVA Analytics element is a GStreamer-based analytics plugin that detects tripwire crossings, object presence in configured zones, and optional dwell time using tracking metadata. It processes video frames with associated object tracking data and generates metadata events for security and surveillance applications.

## Features

- **Tripwire Detection**: Detects when tracked objects cross defined virtual lines
- **Zone Detection**: Detects when objects enter defined polygon or circular zones
- **Zone Dwell-Time Tracking**: Tracks per-object dwell time for zones configured with `track-dwell-time=true`
- **Flexible Configuration**: JSON-based configuration via file or inline property
- **Metadata Output**: Generates `GstAnalyticsTripwireMtd`, `GstAnalyticsZoneMtd`, and `GstAnalyticsDwellTimeMtd` for downstream processing
- **Watermark Support**: Optionally attaches `WatermarkDrawMeta`/`WatermarkCircleMeta` for visualization

## Properties

### config (string)
Path to JSON configuration file containing zones and/or tripwires definitions.

**Example:**
```
gvaanalytics config=/path/to/config.json
```

### zones (string)
Inline JSON string defining zones. Zones defined here are appended to any zones loaded from `config`.

**Example:**
```
gvaanalytics zones='[{"id":"zone_1","points":[{"x":500,"y":0},{"x":500,"y":1080},{"x":200,"y":1080}]}]'
```

### tripwires (string)
Inline JSON string defining tripwires. Tripwires defined here are appended to any tripwires loaded from `config`.

**Example:**
```
gvaanalytics tripwires='[{"id":"exit_line","points":[{"x":500,"y":0},{"x":500,"y":1080}]}]'
```

### draw-zones (boolean)
Enable or disable attachment of watermark metadata (WatermarkPolygonMeta) for drawing zone polygons. Default: true

**Example:**
```
gvaanalytics draw-zones=true
gvaanalytics draw-zones=false
```

### draw-tripwires (boolean)
Enable or disable attachment of watermark metadata (WatermarkDrawMeta) for drawing tripwire lines. Default: true

**Example:**
```
gvaanalytics draw-tripwires=true
gvaanalytics draw-tripwires=false
```

### evaluation-point (enum)
Select which point from each object bounding box is used for zone and tripwire evaluation.

Values:
- `center` (default): `(x + w/2, y + h/2)`
- `bottom-center`: `(x + w/2, y + h)`

**Example:**
```
gvaanalytics evaluation-point=bottom-center
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

#### Zone Optional Parameters

Each zone object can include optional fields:

- `track-dwell-time` (bool, default `false`): enables dwell-time tracking for objects in this zone.
- `object-retention` (number, seconds, default `0.5`): keeps zone dwell state for a short grace period after an object leaves the zone.
- `color` (object `{r,g,b}`): drawing color used when zone visualization is enabled.
- `thickness` (integer): drawing line thickness used when zone visualization is enabled.

Example:

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

Dwell-time tracking requires upstream tracking metadata, so pipelines should include `gvatrack` before `gvaanalytics`.

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
  - `GstAnalyticsDwellTimeMtd`: Relation metadata for per-zone dwell time and first-seen timestamp (for zones with `track-dwell-time=true`)
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
  gvaanalytics config=analytics.json draw-zones=true draw-tripwires=true ! \
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
      ],
      "track-dwell-time": true,
      "object-retention": 1.0
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
Attached as a relation to `GstAnalyticsODMtd` when the selected evaluation point is inside a zone.

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
