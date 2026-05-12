# GStreamer Analytics Metadata

DL Streamer uses the
[GStreamer Analytics](https://gstreamer.freedesktop.org/documentation/analytics/index.html)
metadata library as the primary way to represent inference results.

For the full API reference (iteration, relations, typed accessors), see the
upstream
[GstAnalytics documentation](https://gstreamer.freedesktop.org/documentation/analytics/index.html).

## Metadata types used by DL Streamer

| Type | Description |
|------|-------------|
| `GstAnalyticsODMtd` | Object detection (bbox, label, confidence, rotation) |
| `GstAnalyticsClsMtd` | Classification (confidence + label) |
| `GstAnalyticsTrackingMtd` | Object tracking (ID, timestamps) |
| `GstAnalyticsKeypointMtd` | Single keypoint (x, y, z, confidence, visibility) |
| `GstAnalyticsGroupMtd` | Ordered group of metadata |
| `GstAnalyticsKeypointDescriptor` | Static keypoint layout registry (DL Streamer extension) |
| `GstAnalyticsZoneMtd` | Zone presence — carries the zone ID string (DL Streamer extension) |
| `GstAnalyticsTripwireMtd` | Tripwire crossing — carries the tripwire ID and crossing direction (DL Streamer extension) |

## Metadata flow examples

All analytics metadata for a given buffer lives in a single
`GstAnalyticsRelationMeta` container. Each inference result is stored as a
typed `GstAnalyticsMtd` entry, and entries are connected via directional
relations (`CONTAIN`, `IS_PART_OF`, `RELATE_TO`). The examples below show
how DL Streamer elements populate this structure in typical pipelines.

### Object detection

When `gvadetect` processes a frame, it adds one `GstAnalyticsODMtd` per
detected object:

```
gvadetect post-processor
  │
  └─ GstAnalyticsODMtd (label="car", x, y, w, h, confidence=0.92)
```

If the model has additional output heads (e.g., classification attributes or
keypoints), `gvadetect` attaches them to the detection via `CONTAIN`
relations:

```
  ODMtd (label="person") ─CONTAIN→ ClsMtd (label="male", confidence=0.87)
                          ─CONTAIN→ GroupMtd (semantic_tag="body-pose/coco-17")
                                      ├─CONTAIN→ KeypointMtd (point 0)
                                      ├─CONTAIN→ KeypointMtd (point 1)
                                      └─ ...
```

### Object detection + classification / keypoints

A typical two-stage pipeline `gvadetect ! gvaclassify` produces:

```
gvadetect
  └─ ODMtd (label="person", x, y, w, h, confidence)

gvaclassify (inference-region=roi-list)
  └─ ODMtd ─CONTAIN→ ClsMtd (label="wearing_hat", confidence=0.95)
```

The classification result is attached to the existing `ODMtd` via a
`CONTAIN` relation.

`gvaclassify` can also attach keypoints when using a pose estimation model
as a second stage (per-ROI inference):

```
gvadetect
  └─ ODMtd (label="person", x, y, w, h, confidence)

gvaclassify (inference-region=roi-list, model=pose_model)
  └─ ODMtd ─CONTAIN→ GroupMtd (semantic_tag="body-pose/coco-17")
                        ├─CONTAIN→ KeypointMtd (point 0)
                        ├─CONTAIN→ KeypointMtd (point 1)
                        └─ ...
```

### Object detection + tracking

After `gvadetect ! gvatrack`:

```
gvadetect
  └─ ODMtd (label="car", x, y, w, h, confidence)

gvatrack
  └─ ODMtd ─RELATE_TO→ TrackingMtd (id=42, first_seen, last_seen)
```

The `TrackingMtd` carries the persistent object ID across frames.
Downstream elements (e.g., `gvawatermark`) read the tracking ID via the
relation to display it on screen.

### Object detection + tracking + analytics (zones and tripwires)

After `gvadetect ! gvatrack ! gvaanalytics`:

```
gvadetect
  └─ ODMtd (label="car", x, y, w, h, confidence)

gvatrack
  └─ ODMtd ─RELATE_TO→ TrackingMtd (id=42, first_seen, last_seen)

gvaanalytics (when object center is inside a configured zone)
  └─ ODMtd ─RELATE_TO→ ZoneMtd (zone_id="zone1")

gvaanalytics (when object trajectory crosses a configured tripwire)
  └─ ODMtd ─RELATE_TO→ TripwireMtd (tripwire_id="wire1", direction=1)
```

A single detection may relate to multiple `ZoneMtd` entries (one per zone
it occupies) and zero or more `TripwireMtd` entries (one per crossing
detected on that frame).

`gvametaconvert` reads these relations and serialises them into the JSON
output as `zone_violations` (array of zone ID strings) and
`tripwire_crossings` (array of `{"tripwire_id": ..., "direction": ...}` objects).

## GstAnalyticsZoneMtd

`GstAnalyticsZoneMtd` is a DL Streamer extension added by `gvaanalytics`
when an object's bounding-box center lies inside a configured zone.

### GstAnalyticsZoneData

The payload stored inside `GstAnalyticsRelationMeta` for each `ZoneMtd` entry:

```C
struct _GstAnalyticsZoneData {
    gsize id_len; /* length of id string including null terminator */
    gchar id[];   /* flexible array member — zone identifier string */
};
```

### Zone API

| Function | Description |
|----------|-------------|
| `gst_analytics_zone_mtd_get_mtd_type()` | Returns the metadata type ID for `GstAnalyticsZoneMtd`. |
| `gst_analytics_zone_mtd_get_info(handle, &zone_id)` | Fills `zone_id` (caller-owned string) with the zone identifier. Returns `TRUE` on success. |
| `gst_analytics_relation_meta_add_zone_mtd(rmeta, zone_id, &zone_mtd)` | Adds a `ZoneMtd` entry to `rmeta`. Returns `TRUE` on success. |

### Zone C example

```C
gpointer state = NULL;
GstAnalyticsODMtd od_mtd;
while (gst_analytics_relation_meta_iterate(rmeta, &state,
           gst_analytics_od_mtd_get_mtd_type(), &od_mtd)) {
    GstAnalyticsZoneMtd zone_mtd;
    gpointer rel_state = NULL;
    while (gst_analytics_relation_meta_get_direct_related(
               rmeta, od_mtd.id, GST_ANALYTICS_REL_TYPE_RELATE_TO,
               gst_analytics_zone_mtd_get_mtd_type(), &rel_state, &zone_mtd)) {
        gchar *zone_id = NULL;
        if (gst_analytics_zone_mtd_get_info(&zone_mtd, &zone_id)) {
            g_print("Object in zone: %s\n", zone_id);
            g_free(zone_id);
        }
    }
}
```

## GstAnalyticsTripwireMtd

`GstAnalyticsTripwireMtd` is a DL Streamer extension added by `gvaanalytics`
when a tracked object's trajectory crosses a configured tripwire line between
two consecutive frames.

### GstAnalyticsTripwireData

The payload stored inside `GstAnalyticsRelationMeta` for each `TripwireMtd` entry:

```C
struct _GstAnalyticsTripwireData {
    gint  direction; /* crossing direction: 1 forward, -1 backward, 0 undefined */
    gsize id_len;    /* length of id string including null terminator */
    gchar id[];      /* flexible array member — tripwire identifier string */
};
```

### Direction values

| Value | Meaning |
|-------|---------|
| `1` | Forward — object crossed from the left-hand side to the right-hand side of the tripwire vector (t1 → t2) |
| `-1` | Backward — object crossed from the right-hand side to the left-hand side |
| `0` | Undefined |

For a **vertical** tripwire defined top-to-bottom (`{x,0}` → `{x,height}`),
`direction=1` means left-to-right and `direction=-1` means right-to-left.

### Tripwire API

| Function | Description |
|----------|-------------|
| `gst_analytics_tripwire_mtd_get_mtd_type()` | Returns the metadata type ID for `GstAnalyticsTripwireMtd`. |
| `gst_analytics_tripwire_mtd_get_info(handle, &tripwire_id, &direction)` | Fills `tripwire_id` (caller-owned string) and `direction`. Returns `TRUE` on success. |
| `gst_analytics_relation_meta_add_tripwire_mtd(rmeta, tripwire_id, direction, &tripwire_mtd)` | Adds a `TripwireMtd` entry to `rmeta`. Returns `TRUE` on success. |

### Tripwire C example

```C
gpointer state = NULL;
GstAnalyticsODMtd od_mtd;
while (gst_analytics_relation_meta_iterate(rmeta, &state,
           gst_analytics_od_mtd_get_mtd_type(), &od_mtd)) {
    GstAnalyticsTripwireMtd tw_mtd;
    gpointer rel_state = NULL;
    while (gst_analytics_relation_meta_get_direct_related(
               rmeta, od_mtd.id, GST_ANALYTICS_REL_TYPE_RELATE_TO,
               gst_analytics_tripwire_mtd_get_mtd_type(), &rel_state, &tw_mtd)) {
        gchar *tw_id = NULL;
        gint direction = 0;
        if (gst_analytics_tripwire_mtd_get_info(&tw_mtd, &tw_id, &direction)) {
            g_print("Tripwire crossed: %s direction=%d\n", tw_id, direction);
            g_free(tw_id);
        }
    }
}
```

### Object detection + keypoints (pose estimation)

When a pose estimation model runs through `gvadetect`:

```
gvadetect post-processor
  │
  ├─ GstAnalyticsODMtd (person bounding box)
  │
  ├─ GstAnalyticsKeypointDescriptor lookup("body-pose/coco-17")
  │     → 17 point names, 18 skeleton edges
  │
  ├─ gst_analytics_relation_meta_add_keypoints_group(...)
  │     → GstAnalyticsGroupMtd(semantic_tag="body-pose/coco-17")
  │        ├─ 17 × GstAnalyticsKeypointMtd (pixel positions + confidence)
  │        └─ RELATE_TO relations (skeleton connections)
  │
  └─ OD ─CONTAIN→ GroupMtd
```

The `GstAnalyticsGroupMtd` groups individual keypoints together and carries
the semantic tag that identifies the keypoint layout. Skeleton connections
between keypoints are stored as `RELATE_TO` relations within the group.

## GstAnalyticsKeypointDescriptor

`GstAnalyticsKeypointDescriptor` is a DL Streamer extension that provides a
static registry of keypoint layouts. Each descriptor associates a
**semantic tag** string with:

- An array of **point names** (e.g., `"nose"`, `"eye_l"`, `"wrist_r"`)
- An array of **skeleton connections** — pairs of point indices that define
  the skeleton edges for visualization

### Structure

```C
typedef struct {
    const char *semantic_tag;             /* e.g. "body-pose/coco-17" */
    const char *const *point_names;       /* array of point name strings */
    gsize point_count;                    /* number of points */
    const gint *skeleton_connections;     /* flat array of (from, to) index pairs */
    gsize skeleton_connection_count;      /* number of connection pairs */
} GstAnalyticsKeypointDescriptor;
```

### Built-in descriptors

| Semantic Tag | Points | Skeleton Edges | Use Case |
|---|---|---|---|
| `body-pose/coco-17` | 17 | 18 | COCO body pose (nose, eyes, ears, shoulders, elbows, wrists, hips, knees, ankles) |
| `body-pose/openpose-18` | 18 | 13 | OpenPose body model (adds neck point) |
| `body-pose/hrnet-coco-17` | 17 | 13 | HRNet with COCO keypoint ordering |
| `face-landmarks/centerface-5` | 5 | 0 | Facial landmarks (eyes, nose tip, mouth corners) |

### Keypoint Descriptor API

| Function | Description |
|----------|-------------|
| `gst_analytics_keypoint_descriptor_lookup(semantic_tag)` | Find a built-in descriptor by its semantic tag. Returns `NULL` if not found. |
| `gst_analytics_keypoint_descriptor_get_point_name(desc, index)` | Get point name at index (Python bindings only). |
| `gst_analytics_keypoint_descriptor_get_skeleton_connection(desc, index, &from, &to)` | Get skeleton edge at index (Python bindings only). |

### Keypoint Descriptor C example

```C
const GstAnalyticsKeypointDescriptor *desc =
    gst_analytics_keypoint_descriptor_lookup("body-pose/coco-17");
if (desc) {
    printf("Format: %s (%zu points, %zu skeleton edges)\n",
           desc->semantic_tag, desc->point_count, desc->skeleton_connection_count);

    for (gsize i = 0; i < desc->point_count; i++)
        printf("  Point %zu: %s\n", i, desc->point_names[i]);

    for (gsize i = 0; i < desc->skeleton_connection_count; i++)
        printf("  Edge %zu: %s → %s\n", i,
               desc->point_names[desc->skeleton_connections[i * 2]],
               desc->point_names[desc->skeleton_connections[i * 2 + 1]]);
}
```

### Python example

In Python, array fields cannot be accessed directly (GObject Introspection
limitation), so indexed accessor methods are provided:

```python
from gi.repository import DLStreamerMeta

desc = DLStreamerMeta.KeypointDescriptor.lookup("body-pose/coco-17")
if desc:
    print(f"Format: {desc.semantic_tag} ({desc.point_count} points)")

    for i in range(desc.point_count):
        print(f"  Point {i}: {desc.get_point_name(i)}")

    for i in range(desc.skeleton_connection_count):
        ret, from_idx, to_idx = desc.get_skeleton_connection(i)
        if ret:
            print(f"  Edge {i}: {desc.get_point_name(from_idx)} → {desc.get_point_name(to_idx)}")
```

### Role in the metadata pipeline

The `GstAnalyticsKeypointDescriptor` acts as a **shared schema** between
producers and consumers of keypoint metadata.

#### Producer side (writing keypoints)

When a post-processor (in `gvadetect` or `gvaclassify`) creates keypoint
metadata from model output:

1. Each converter hardcodes the appropriate descriptor constant for the model
   it supports (e.g., `GST_ANALYTICS_KEYPOINT_BODY_POSE_COCO_17` in the
   YOLO converter).
2. It calls `gst_analytics_keypoint_descriptor_lookup(semantic_tag)` to
   obtain the descriptor.
3. It reads `skeleton_connections` from the descriptor to pass into
   `gst_analytics_relation_meta_add_keypoints_group(...)`.
4. The resulting `GstAnalyticsGroupMtd` stores the semantic tag and contains
   individual `GstAnalyticsKeypointMtd` entries with pixel-space positions.
5. Skeleton edges are stored as `RELATE_TO` relations between the keypoint
   entries within the group.

#### Consumer side (reading keypoints)

When downstream code needs to interpret keypoint metadata (e.g.,
`gvawatermark` for visualization, or a Python callback for analytics):

1. It iterates the group's member `GstAnalyticsKeypointMtd` entries to read
   positions and confidences.
2. **Skeleton connections are encoded directly in the metadata** as
   `RELATE_TO` relations between keypoint entries within the group. The
   consumer can traverse these relations to reconstruct the skeleton without
   any descriptor lookup.
3. If human-readable **point names** are needed (e.g., labelling "nose",
   "eye_l" on an overlay), the consumer reads the `semantic_tag` from the
   `GstAnalyticsGroupMtd` and calls
   `gst_analytics_keypoint_descriptor_lookup(semantic_tag)` to obtain the
   descriptor's `point_names` array.

In other words, the descriptor is **not required** to draw the skeleton — the
relation graph already carries that information. The descriptor is only
necessary when you need semantic labels for individual keypoints.

#### Data flow diagram

```text
+-----------------------------------------------------------+
| Post-processor / converter (producer)                     |
|                                                           |
|  1. descriptor = lookup(HARDCODED_TAG)                    |
|     (e.g., "body-pose/coco-17" in the YOLOv26 converter)  |
|  2. skeleton = descriptor->skeleton_connections           |
|  3. add_keypoints_group(tag, positions, skeleton)         |
|                                                           |
|  Output on buffer:                                        |
|    GstAnalyticsRelationMeta                               |
|      +-- GroupMtd (semantic_tag="body-pose/coco-17")      |
|           +-- KeypointMtd[0] (nose, x=320, y=180)         |
|           +-- KeypointMtd[1] (eye_l, x=310, y=170)        |
|           +-- ...                                         |
|           +-- RELATE_TO: 0<>1, 0<>2, 1<>3 (skeleton)      |
+-----------------------------------------------------------+
                         | buffer flows downstream
                         v
+-----------------------------------------------------------+
| Consumer (e.g., gvawatermark, Python app)                 |
|                                                           |
|  1. for each keypoint in group:                           |
|       read position (x, y) and confidence                 |
|  2. for each RELATE_TO relation in group:                 |
|       draw skeleton line between connected keypoints      |
|  3. (optional) if labels needed:                          |
|       tag = group.get_semantic_tag()                      |
|       descriptor = lookup(tag)                            |
|       name = descriptor->point_names[index]               |
+-----------------------------------------------------------+
```

The descriptor adds value only as a naming dictionary. Adding support for a
new keypoint layout (e.g., a hand skeleton) only requires registering a new
`GstAnalyticsKeypointDescriptor` with point names; the consumer drawing code
remains unchanged.

### C example: creating a keypoints group using the descriptor

The following shows how to create a `GstAnalyticsGroupMtd` with keypoints
and skeleton connections from a descriptor. This is what `gvadetect` /
`gvaclassify` post-processors do internally.

```C
#include <dlstreamer/gst/metadata/gstanalyticskeypointdescriptor.h>
#include <dlstreamer/gst/metadata/gstanalyticskeypointmtd.h>
#include <gst/analytics/analytics.h>

void attach_pose_keypoints(GstAnalyticsRelationMeta *rmeta,
                           GstAnalyticsODMtd *od_mtd,
                           const gint *positions,
                           gsize keypoint_count,
                           const gfloat *confidences) {
    /* 1. Look up the descriptor by semantic tag */
    const GstAnalyticsKeypointDescriptor *desc =
        gst_analytics_keypoint_descriptor_lookup("body-pose/coco-17");

    /* 2. Get skeleton data from the descriptor */
    gsize skeleton_pairs_len = 0;
    const gint *skeleton_pairs = NULL;
    if (desc->skeleton_connections && desc->skeleton_connection_count > 0) {
        skeleton_pairs_len = desc->skeleton_connection_count * 2;
        skeleton_pairs = desc->skeleton_connections;
    }

    /* 3. Create the keypoints group in one call */
    GstAnalyticsGroupMtd group;
    gst_analytics_relation_meta_add_keypoints_group(
        rmeta,
        desc->semantic_tag,                   /* stored in GroupMtd */
        GST_ANALYTICS_KEYPOINT_DIMENSIONS_2D,
        keypoint_count * 2,                   /* positions array length (x,y pairs) */
        positions,                            /* flat array: [x0, y0, x1, y1, ...] */
        keypoint_count,
        confidences,                          /* per-keypoint confidence, or NULL */
        NULL,                                 /* visibilities (optional) */
        skeleton_pairs_len,
        skeleton_pairs,                       /* from descriptor */
        &group);

    /* 4. Relate the keypoints group to the detection */
    gst_analytics_relation_meta_set_relation(rmeta, GST_ANALYTICS_REL_TYPE_CONTAIN,
                                             od_mtd->id, group.id);
    gst_analytics_relation_meta_set_relation(rmeta, GST_ANALYTICS_REL_TYPE_IS_PART_OF,
                                             group.id, od_mtd->id);
}
```

`gst_analytics_relation_meta_add_keypoints_group` creates the
`GstAnalyticsGroupMtd`, adds individual `GstAnalyticsKeypointMtd` entries as
members, and establishes `RELATE_TO` relations between keypoints according to
`skeleton_pairs` — all in a single call.

### Supported keypoint layouts

#### `body-pose/coco-17`

Standard COCO body pose with 17 keypoints and 18 skeleton edges.

| Index | Point Name |
|---|---|
| 0 | nose |
| 1 | eye_l |
| 2 | eye_r |
| 3 | ear_l |
| 4 | ear_r |
| 5 | shoulder_l |
| 6 | shoulder_r |
| 7 | elbow_l |
| 8 | elbow_r |
| 9 | wrist_l |
| 10 | wrist_r |
| 11 | hip_l |
| 12 | hip_r |
| 13 | knee_l |
| 14 | knee_r |
| 15 | ankle_l |
| 16 | ankle_r |

Skeleton connections:

```
nose─eye_l─ear_l─shoulder_l─elbow_l─wrist_l
nose─eye_r─ear_r─shoulder_r─elbow_r─wrist_r
shoulder_l─shoulder_r
shoulder_l─hip_l─knee_l─ankle_l
shoulder_r─hip_r─knee_r─ankle_r
hip_l─hip_r
```

#### `body-pose/openpose-18`

OpenPose 18-keypoint body model. Adds a **neck** point (index 1) compared to
COCO. 13 skeleton edges.

| Index | Point Name |
|---|---|
| 0 | nose |
| 1 | neck |
| 2 | shoulder_r |
| 3 | elbow_r |
| 4 | wrist_r |
| 5 | shoulder_l |
| 6 | elbow_l |
| 7 | wrist_l |
| 8 | hip_r |
| 9 | knee_r |
| 10 | ankle_r |
| 11 | hip_l |
| 12 | knee_l |
| 13 | ankle_l |
| 14 | eye_r |
| 15 | eye_l |
| 16 | ear_r |
| 17 | ear_l |

Skeleton connections:

```
nose─eye_l─ear_l
nose─eye_r─ear_r
shoulder_l─shoulder_r
shoulder_l─elbow_l─wrist_l
shoulder_r─elbow_r─wrist_r
hip_l─knee_l─ankle_l
hip_r─knee_r─ankle_r
```

#### `body-pose/hrnet-coco-17`

HRNet with COCO 17-keypoint ordering (same point names as `body-pose/coco-17`).
13 skeleton edges — differs from COCO-17 in which edges are included (no
nose-to-ear or shoulder-to-hip connections).

Skeleton connections:

```
nose─eye_l─ear_l
nose─eye_r─ear_r
shoulder_l─shoulder_r
shoulder_l─elbow_l─wrist_l
shoulder_r─elbow_r─wrist_r
hip_l─knee_l─ankle_l
hip_r─knee_r─ankle_r
```

#### `face-landmarks/centerface-5`

CenterFace 5-point facial landmarks. No skeleton connections.

| Index | Point Name |
|---|---|
| 0 | eye_l |
| 1 | eye_r |
| 2 | nose_tip |
| 3 | mouth_l |
| 4 | mouth_r |
