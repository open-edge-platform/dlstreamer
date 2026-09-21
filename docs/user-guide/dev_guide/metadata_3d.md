# 3D Sensor Metadata (LiDAR & radar)

DL Streamer's 3D pipelines carry two kinds of metadata:

- **Raw-sensor metadata** — custom `GstMeta` types attached alongside the
  buffer payload by the sensor-parsing/processing elements. These describe the
  frame and the sensor-specific results (point clouds, clusters, low-level
  tracks). Two types are defined:
  - `LidarMeta` — produced by [`g3dlidarparse`](../elements/g3dlidarparse.md)
    (offline BIN/PCD replay) and [`g3dlidarsrc`](../elements/g3dlidarsrc.md)
    (live device capture)
  - `GstRadarProcessMeta` — produced by [`g3dradarprocess`](../elements/g3dradarprocess.md)
- **3D detections** — the oriented 3D bounding boxes produced by
  [`g3dinference`](../elements/g3dinference.md) and consumed by
  [`g3dobjectfuser`](../elements/g3dobjectfuser.md) are stored as
  `GstAnalytics3DODMtd` inside the buffer's `GstAnalyticsRelationMeta`
  container. That type is documented with the rest of the analytics metadata in
  [GStreamer Analytics Metadata](./metadata_analytics.md#gstanalytics3dodmtd).

This page documents the two raw-sensor `GstMeta` types. These are standalone
`GstMeta` attached directly to the buffer.

## LidarMeta

`LidarMeta` frames a raw LiDAR point cloud. It is attached by one of two
producers — `g3dlidarparse`, which parses a recorded BIN/PCD frame from disk,
or `g3dlidarsrc`, which captures frames live from a device — each emitting a
dense float payload with one `LidarMeta` per buffer. The two are byte-for-byte
compatible, so downstream elements accept either source unchanged.
`g3dinference` reads the meta to validate and voxelize the payload before
inference.

The **point coordinates themselves are not stored in the meta**: they live in
the buffer payload as a flat `float32` array, four floats per point
(`x, y, z, intensity`). `LidarMeta` only carries the point count and the framing
information needed to interpret and time-align that payload:

```c
typedef struct _LidarMeta {
    GstMeta      meta;
    guint        lidar_point_count;          /* points in this frame; payload = count * 4 * sizeof(float) */
    size_t       frame_id;                   /* sequential frame id from the source stream */
    GstClockTime exit_source_timestamp;      /* clock time when the buffer left the source element */
    GstClockTime exit_g3dinference_timestamp;/* clock time when the buffer left g3dinference */
    guint        stream_id;                  /* STREAM_START group-id, for multi-stream pipelines */
} LidarMeta;
```

### Fields

| Field | Type | Description |
|-------|------|-------------|
| `lidar_point_count` | `guint` | Number of points in the frame. The payload is `lidar_point_count * 4 * sizeof(float)` bytes (`x, y, z, intensity` per point). `g3dinference` rejects the buffer if the mapped payload size does not match. |
| `frame_id` | `size_t` | Sequential frame identifier from the source stream. |
| `exit_source_timestamp` | `GstClockTime` | Clock time when the buffer exited the source element (`g3dlidarparse` or `g3dlidarsrc`). Set by the producer. |
| `exit_g3dinference_timestamp` | `GstClockTime` | Clock time when the buffer exited `g3dinference`. Initialised to `GST_CLOCK_TIME_NONE` and filled by `g3dinference` (used for latency measurement). |
| `stream_id` | `guint` | Stream identifier (group-id from `STREAM_START`) for multi-stream pipelines. |

### LidarMeta API

| Symbol | Description |
|--------|-------------|
| `LIDAR_META_API_TYPE` | The `GType` for the meta API (`lidar_meta_api_get_type()`). Use it with `gst_buffer_get_meta()`. |
| `LIDAR_META_INFO` | The `GstMetaInfo` for the meta (`lidar_meta_get_info()`). |
| `add_lidar_meta(buffer, lidar_point_count, frame_id, exit_source_timestamp, stream_id)` | Attaches a `LidarMeta` to `buffer` and fills it. Returns the new meta or `NULL`. `exit_g3dinference_timestamp` is left as `GST_CLOCK_TIME_NONE` for `g3dinference` to set later. |

### LidarMeta C example

```c
#include <dlstreamer/gst/metadata/g3d_lidar_meta.h>

/* Consumer side (e.g. g3dinference): read the framing, validate the payload. */
LidarMeta *lm = (LidarMeta *)gst_buffer_get_meta(buffer, LIDAR_META_API_TYPE);
if (lm) {
    GstMapInfo map;
    gst_buffer_map(buffer, &map, GST_MAP_READ);
    gsize expected = (gsize)lm->lidar_point_count * 4 * sizeof(gfloat);
    if (map.size == expected) {
        const gfloat *points = (const gfloat *)map.data;   /* [x0,y0,z0,i0, x1,y1,z1,i1, ...] */
        /* ... voxelize / run inference ... */
    }
    gst_buffer_unmap(buffer, &map);
}
```

### LidarMeta lifecycle

```text
g3dlidarparse / g3dlidarsrc         g3dinference
  parse BIN/PCD | capture live        read LidarMeta (count, frame_id, stream_id)
  add_lidar_meta(count, ...)   ─────▶ map payload, verify size == count*4*float
  payload = float32[count*4]          run inference, add GstAnalytics3DODMtd
  caps: application/x-lidar           set exit_g3dinference_timestamp
```

## GstRadarProcessMeta

`GstRadarProcessMeta` carries the full output of the mmWave radar signal
pipeline for one frame. `g3dradarprocess` runs detection, clustering, and
tracking on each raw radar frame and attaches one `GstRadarProcessMeta` with
three result groups: detected reflection points, clusters, and tracked objects.
All the array fields are heap-allocated and owned by the meta.

```c
struct _GstRadarProcessMeta {
    GstMeta  meta;
    guint64  frame_id;

    /* Point clouds — one entry per detected reflection point */
    gint     point_clouds_len;
    gfloat  *ranges;   /* distance to the reflection point   */
    gfloat  *speeds;   /* Doppler (radial) velocity          */
    gfloat  *angles;   /* azimuth angle                      */
    gfloat  *snrs;     /* signal-to-noise ratio              */

    /* Cluster result — points grouped into objects */
    gint     num_clusters;
    gint    *cluster_idx; /* per-point cluster index          */
    gfloat  *cluster_cx;  /* cluster centre x                 */
    gfloat  *cluster_cy;  /* cluster centre y                 */
    gfloat  *cluster_rx;  /* cluster extent (radius) x        */
    gfloat  *cluster_ry;  /* cluster extent (radius) y        */
    gfloat  *cluster_av;  /* average velocity per cluster     */

    /* Tracking result — objects tracked across frames */
    gint     num_tracked_objects;
    gint    *tracker_ids; /* stable per-object track id       */
    gfloat  *tracker_x;   /* position estimate x  (sHat[0])   */
    gfloat  *tracker_y;   /* position estimate y  (sHat[1])   */
    gfloat  *tracker_vx;  /* velocity x           (sHat[2])   */
    gfloat  *tracker_vy;  /* velocity y           (sHat[3])   */
};
```

### Result groups

| Group | Count field | Arrays | Meaning |
|-------|-------------|--------|---------|
| Point clouds | `point_clouds_len` | `ranges`, `speeds`, `angles`, `snrs` | Raw detected reflection points (range, Doppler speed, azimuth angle, SNR). |
| Clusters | `num_clusters` | `cluster_idx`, `cluster_cx`, `cluster_cy`, `cluster_rx`, `cluster_ry`, `cluster_av` | Nearby points grouped into candidate objects, each with a centre `(cx, cy)`, extents `(rx, ry)`, and average velocity `av`. `cluster_idx` maps clusters to point clouds. |
| Tracking | `num_tracked_objects` | `tracker_ids`, `tracker_x`, `tracker_y`, `tracker_vx`, `tracker_vy` | Objects tracked across frames, each with a stable `tracker_id`, position `(x, y)`, and velocity `(vx, vy)`. |

### GstRadarProcessMeta API

| Symbol | Description |
|--------|-------------|
| `GST_RADAR_PROCESS_META_API_TYPE` | The `GType` for the meta API (`gst_radar_process_meta_api_get_type()`). Use it with `gst_buffer_get_meta()` / `gst_buffer_iterate_meta_filtered()`. |
| `GST_RADAR_PROCESS_META_INFO` | The `GstMetaInfo` for the meta (`gst_radar_process_meta_get_info()`). |
| `gst_buffer_add_radar_process_meta(buffer, frame_id, point_clouds, cluster_result, tracking_result)` | Attaches a `GstRadarProcessMeta` to `buffer` and **deep-copies** the `libradar` result structs into the meta's own arrays. Returns the new meta or `NULL`. |

> **Note:** `GstRadarProcessMeta` has no transform function, so it is
> **not** automatically copied when a buffer is transformed or copied.
> Read it on the buffer produced directly by `g3dradarprocess`.

### GstRadarProcessMeta C example

```c
#include <dlstreamer/gst/metadata/g3d_radarprocess_meta.h>

GstRadarProcessMeta *rm =
    (GstRadarProcessMeta *)gst_buffer_get_meta(buffer, GST_RADAR_PROCESS_META_API_TYPE);
if (rm) {
    g_print("frame %" G_GUINT64_FORMAT ": %d points, %d clusters, %d tracks\n",
            rm->frame_id, rm->point_clouds_len, rm->num_clusters, rm->num_tracked_objects);

    for (gint i = 0; i < rm->num_tracked_objects; i++)
        g_print("  track %d at (%.2f, %.2f) v=(%.2f, %.2f)\n",
                rm->tracker_ids[i], rm->tracker_x[i], rm->tracker_y[i],
                rm->tracker_vx[i], rm->tracker_vy[i]);
}
```
