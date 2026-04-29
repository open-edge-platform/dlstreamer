# Keypoint Descriptor API

The `GstAnalyticsKeypointDescriptor` is a read-only descriptor that associates a **semantic tag** (e.g. `"body-pose/coco-17"`) with a named set of keypoints and their skeleton connections. It serves as a registry of well-known keypoint layouts used by pose-estimation, face-landmark, and other keypoint-based models.

Descriptors are built-in — they are not created at runtime. Code that produces or consumes keypoint metadata uses the semantic tag to look up the matching descriptor and then queries point names and skeleton edges through the API.

## Built-in descriptors

| Semantic tag | `#define` constant | Points | Skeleton edges | Typical use |
|---|---|---|---|---|
| `body-pose/coco-17` | `GST_ANALYTICS_KEYPOINT_BODY_POSE_COCO_17` | 17 | 18 | COCO body pose (YOLOv8-pose, etc.) |
| `body-pose/openpose-18` | `GST_ANALYTICS_KEYPOINT_BODY_POSE_OPENPOSE_18` | 18 | 13 | OpenPose body pose |
| `body-pose/hrnet-coco-17` | `GST_ANALYTICS_KEYPOINT_BODY_POSE_HRNET_COCO_17` | 17 | 13 | HRNet COCO body pose |
| `face-landmarks/centerface-5` | `GST_ANALYTICS_KEYPOINT_FACE_CENTERFACE_5` | 5 | 0 | CenterFace 5-point facial landmarks |

### COCO 17-keypoint layout

```
Index  Name          Index  Name          Index  Name
─────  ────          ─────  ────          ─────  ────
  0    nose            6    shoulder_r     12    hip_r
  1    eye_l           7    elbow_l        13    knee_l
  2    eye_r           8    elbow_r        14    knee_r
  3    ear_l           9    wrist_l        15    ankle_l
  4    ear_r          10    wrist_r        16    ankle_r
  5    shoulder_l     11    hip_l
```

### OpenPose 18-keypoint layout

```
Index  Name          Index  Name          Index  Name
─────  ────          ─────  ────          ─────  ────
  0    nose            6    elbow_l        12    knee_l
  1    neck            7    wrist_l        13    ankle_l
  2    shoulder_r      8    hip_r          14    eye_r
  3    elbow_r         9    knee_r         15    eye_l
  4    wrist_r        10    ankle_r        16    ear_r
  5    shoulder_l     11    hip_l          17    ear_l
```

### CenterFace 5-point layout

```
Index  Name
─────  ────
  0    eye_l
  1    eye_r
  2    nose_tip
  3    mouth_l
  4    mouth_r
```

---

## C API

**Header:** `#include <dlstreamer/gst/metadata/gstanalyticskeypointdescriptor.h>`

### Data structure

```c
typedef struct {
    const char *semantic_tag;
    const char *const *point_names;
    gsize point_count;
    const gint *skeleton_connections;
    gsize skeleton_connection_count;
} GstAnalyticsKeypointDescriptor;
```

All fields are read-only. The struct is returned by pointer from `lookup()` and the pointer remains valid for the lifetime of the process.

### Functions

#### `gst_analytics_keypoint_descriptor_lookup`

```c
const GstAnalyticsKeypointDescriptor *
gst_analytics_keypoint_descriptor_lookup (const gchar *semantic_tag);
```

Look up a built-in descriptor by its semantic tag. Returns `NULL` if no descriptor matches.

> **Note:** The `semantic_tag`, `point_count`, and `skeleton_connection_count` fields are accessible
> directly on the struct in both C/C++ and Python. The indexed accessors `get_point_name()` and
> `get_skeleton_connection()` are available as methods (needed in Python because the array fields
> cannot be indexed via GObject Introspection).

#### `gst_analytics_keypoint_descriptor_get_point_name`

```c
const gchar *
gst_analytics_keypoint_descriptor_get_point_name (const GstAnalyticsKeypointDescriptor *desc,
                                                  gsize index);
```

Returns the name of keypoint at `index`, or `NULL` if out of range.

#### `gst_analytics_keypoint_descriptor_get_skeleton_connection`

```c
gboolean
gst_analytics_keypoint_descriptor_get_skeleton_connection (const GstAnalyticsKeypointDescriptor *desc,
                                                           gsize index,
                                                           gint *from_idx,
                                                           gint *to_idx);
```

Gets the skeleton edge at `index`. Each edge connects keypoint `from_idx` to keypoint `to_idx` (indices into the point_names array). Returns `FALSE` if `index` is out of range or the descriptor has no skeleton.

### C example: list all keypoints and skeleton for COCO-17

```c
#include <dlstreamer/gst/metadata/gstanalyticskeypointdescriptor.h>
#include <gst/gst.h>
#include <stdio.h>

void print_descriptor_info (void)
{
    const GstAnalyticsKeypointDescriptor *desc =
        gst_analytics_keypoint_descriptor_lookup (GST_ANALYTICS_KEYPOINT_BODY_POSE_COCO_17);

    if (!desc) {
        g_printerr ("Descriptor not found\n");
        return;
    }

    g_print ("Descriptor: %s\n", desc->semantic_tag);
    g_print ("Keypoints (%zu):\n", desc->point_count);

    for (gsize i = 0; i < desc->point_count; i++) {
        g_print ("  [%zu] %s\n", i, desc->point_names[i]);
    }

    g_print ("Skeleton connections (%zu):\n", desc->skeleton_connection_count);

    for (gsize i = 0; i < desc->skeleton_connection_count; i++) {
        gint from = desc->skeleton_connections[i * 2];
        gint to   = desc->skeleton_connections[i * 2 + 1];
        g_print ("  %s -> %s\n", desc->point_names[from], desc->point_names[to]);
    }
}
```

### C example: use descriptor when creating a keypoints group

```c
#include <dlstreamer/gst/metadata/gstanalyticskeypointdescriptor.h>
#include <dlstreamer/gst/metadata/gstanalyticskeypointmtd.h>

void attach_pose_keypoints (GstAnalyticsRelationMeta *rmeta,
                            const gint *positions,
                            gsize keypoint_count,
                            const gfloat *confidences)
{
    const GstAnalyticsKeypointDescriptor *desc =
        gst_analytics_keypoint_descriptor_lookup (GST_ANALYTICS_KEYPOINT_BODY_POSE_COCO_17);

    GstAnalyticsGroupMtd group;
    gst_analytics_relation_meta_add_keypoints_group (
        rmeta,
        desc->semantic_tag,
        GST_ANALYTICS_KEYPOINT_DIMENSIONS_2D,
        keypoint_count * 2,       /* positions array length */
        positions,
        keypoint_count,
        confidences,
        NULL,                     /* visibilities (optional) */
        desc->skeleton_connection_count * 2,
        desc->skeleton_connections,
        &group);
}
```

### C example: reading keypoints from metadata using the descriptor

```c
void read_pose_from_group (GstAnalyticsRelationMeta *rmeta,
                           GstAnalyticsGroupMtd *group)
{
    gchar *tag = gst_analytics_group_mtd_get_semantic_tag (group);
    const GstAnalyticsKeypointDescriptor *desc =
        gst_analytics_keypoint_descriptor_lookup (tag);
    g_free (tag);

    if (!desc) {
        g_printerr ("Unknown keypoint layout\n");
        return;
    }

    gpointer state = NULL;
    GstAnalyticsMtd member;
    gsize idx = 0;

    while (gst_analytics_group_mtd_iterate (group, &state,
               gst_analytics_keypoint_mtd_get_mtd_type (), &member)) {
        GstAnalyticsKeypointMtd *kp = (GstAnalyticsKeypointMtd *) &member;
        gint x, y, z;
        GstAnalyticsKeypointDimensions dim;
        gfloat conf;

        gst_analytics_keypoint_mtd_get_position (kp, &x, &y, &z, &dim);
        gst_analytics_keypoint_mtd_get_confidence (kp, &conf);

        const gchar *name = (idx < desc->point_count)
            ? desc->point_names[idx]
            : "unknown";

        g_print ("  %s: (%d, %d) conf=%.2f\n", name, x, y, conf);
        idx++;
    }
}
```

---

## Python API (GObject Introspection)

**Namespace:** `DLStreamerMeta` (version `1.0`)

```python
import gi
gi.require_version('Gst', '1.0')
gi.require_version('GstAnalytics', '1.0')
gi.require_version('DLStreamerMeta', '1.0')

from gi.repository import Gst, GstAnalytics, DLStreamerMeta
Gst.init(None)
```

### Class: `DLStreamerMeta.KeypointDescriptor`

The Python binding is auto-generated via GObject Introspection from the GIR file. The class mirrors the C API with Pythonic conventions.

### Fields and Methods

| Access | Return type | Description |
|---|---|---|
| `KeypointDescriptor.lookup(semantic_tag)` | `KeypointDescriptor` or `None` | Static method. Look up a built-in descriptor by tag. |
| `desc.semantic_tag` | `str` | The semantic tag string (e.g. `"body-pose/coco-17"`). |
| `desc.point_count` | `int` | The number of keypoints. |
| `desc.get_point_name(index)` | `str` or `None` | Get keypoint name at index. |
| `desc.skeleton_connection_count` | `int` | Number of skeleton edges. |
| `desc.get_skeleton_connection(index)` | `(bool, int, int)` | Get skeleton edge: `(ok, from_idx, to_idx)`. |

> **Note:** `KeypointDescriptor.lookup(None)` raises `TypeError` because the parameter is not nullable in the GI binding. Always pass a string.

The `lookup()` function is also available at the namespace level as `DLStreamerMeta.keypoint_descriptor_lookup(tag)`.

### Python example: list all keypoints and skeleton for COCO-17

```python
import gi
gi.require_version('Gst', '1.0')
gi.require_version('DLStreamerMeta', '1.0')
from gi.repository import Gst, DLStreamerMeta

Gst.init(None)

desc = DLStreamerMeta.KeypointDescriptor.lookup('body-pose/coco-17')

print(f"Descriptor: {desc.semantic_tag}")
print(f"Keypoints ({desc.point_count}):")
for i in range(desc.point_count):
    print(f"  [{i}] {desc.get_point_name(i)}")

print(f"Skeleton connections ({desc.skeleton_connection_count}):")
for i in range(desc.skeleton_connection_count):
    ok, from_idx, to_idx = desc.get_skeleton_connection(i)
    if ok:
        print(f"  {desc.get_point_name(from_idx)} -> {desc.get_point_name(to_idx)}")
```

### Python example: reading keypoints from a GstAnalytics group

```python
import gi
gi.require_version('Gst', '1.0')
gi.require_version('GstAnalytics', '1.0')
gi.require_version('DLStreamerMeta', '1.0')
from gi.repository import Gst, GstAnalytics, DLStreamerMeta

Gst.init(None)

def read_keypoints_from_group(rmeta, group):
    """Read keypoint positions and names from a group using the descriptor."""
    tag = group.get_semantic_tag()
    desc = DLStreamerMeta.KeypointDescriptor.lookup(tag)

    if desc is None:
        print(f"Unknown keypoint layout: {tag}")
        return

    kp_type = DLStreamerMeta.KeypointMtd.get_mtd_type()
    state = None
    idx = 0

    while True:
        ok, state, member = group.iterate(state, kp_type)
        if not ok:
            break

        ok, kp = DLStreamerMeta.relation_meta_get_keypoint_mtd(rmeta, member.id)
        if not ok:
            continue

        ok, x, y, z, dim = kp.get_position()
        ok_c, conf = kp.get_confidence()

        name = desc.get_point_name(idx) if idx < desc.point_count else "unknown"
        print(f"  {name}: ({x}, {y}) conf={conf:.2f}")
        idx += 1
```

### Python example: using a descriptor to create a keypoints group

```python
def create_pose_group(rmeta, positions, confidences):
    """Create a COCO-17 keypoints group with skeleton from the descriptor."""
    desc = DLStreamerMeta.KeypointDescriptor.lookup('body-pose/coco-17')

    # Build skeleton pairs list from descriptor
    skeleton = []
    for i in range(desc.skeleton_connection_count):
        ok, from_idx, to_idx = desc.get_skeleton_connection(i)
        if ok:
            skeleton.extend([from_idx, to_idx])

    ok, group = DLStreamerMeta.relation_meta_add_keypoints_group(
        rmeta,
        desc.semantic_tag,
        DLStreamerMeta.KeypointDimensions(2),
        positions,
        confidences,
        None,       # visibilities (optional)
        skeleton)

    return group if ok else None
```

### Python example: comparing two descriptors

```python
coco = DLStreamerMeta.KeypointDescriptor.lookup('body-pose/coco-17')
hrnet = DLStreamerMeta.KeypointDescriptor.lookup('body-pose/hrnet-coco-17')

# Both have 17 keypoints with the same names
assert coco.point_count == hrnet.point_count
for i in range(coco.point_count):
    assert coco.get_point_name(i) == hrnet.get_point_name(i)

# But different skeleton connections
print(f"COCO-17 skeleton edges: {coco.skeleton_connection_count}")
print(f"HRNet skeleton edges:  {hrnet.skeleton_connection_count}")
```

---

## How descriptors relate to keypoint metadata

The typical workflow is:

1. **Producer** (e.g. `gvadetect` with a pose model) runs inference, obtains raw keypoint coordinates, looks up the appropriate `GstAnalyticsKeypointDescriptor` by the model's known semantic tag, and creates a `GstAnalyticsGroupMtd` with the tag and skeleton from the descriptor.

2. **Consumer** (e.g. a watermark overlay, a Python callback, or an analytics module) reads the semantic tag from the group metadata, looks up the descriptor, and uses point names and skeleton edges to interpret the keypoints.

This decouples the producer from the consumer — neither needs to hardcode knowledge of specific keypoint layouts. The descriptor serves as a shared schema.

```
┌──────────┐   semantic tag    ┌──────────────────────┐   semantic tag    ┌──────────┐
│ Producer │ ───────────────── │ KeypointDescriptor   │ ───────────────── │ Consumer │
│ (detect) │                   │ • point_names        │                   │ (overlay │
│          │   skeleton edges  │ • skeleton           │   point names     │  / app)  │
│          │ ◄──────────────── │ • point_count        │ ──────────────── │          │
└──────────┘                   └──────────────────────┘                   └──────────┘
```
