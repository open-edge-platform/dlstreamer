# g3dobjectfuser

Spatially associates 2D camera detections with 3D radar or LiDAR detections, performs internal per-modality object tracking, and emits a single output buffer carrying the cross-modal association as analytics metadata.

## Overview
The `g3dobjectfuser` element consumes a **pre-muxed `GstAnalyticsBatchMeta` container** from `gvastreammux` — one batch holding the camera stream(s) and one radar/LiDAR stream — associates the 2D camera detections with the 3D detections, performs per-modality object tracking, and writes the cross-modal links back into the same container in place.

It runs:

- **Per-camera 2D tracking** keyed by the `gvastreammux` stream index (`GstAnalyticsBatchMeta.streams[0].index`), so each camera maintains its own track-id space using the `vas::ot` tracker.
- **One LiDAR tracker**, running in the 2D frame selected by `tracking-space`. In **`bev`** (default) each 3D box's ground footprint `(x, y)` is rasterised to a fixed top-down metric grid and its rect is fed to the `vas::ot` tracker. This is camera-independent, free of perspective/depth ambiguity, and tracks every box (not just those inside a camera FOV). In **`image`** each box is projected to one canonical camera's image plane (the lowest stream index) and the bounding rect is tracked instead. Either way the LiDAR track-id space is a single consistent frame across all cameras. (Camera↔3D fusion always uses image projection regardless of this setting.)
- **3D ↔ 2D projection** via per-camera calibration matrices (KITTI-style for LiDAR, 3×3 homography for radar). Radar detections pass through with their existing `tracker_ids` set by `g3dradarprocess`.
- **Spatial association** between every projected 3D box and every 2D camera box using `vas::ot::HungarianAlgo` (the same Hungarian solver `gvatrack` uses for detection-to-tracklet assignment), with IoU costs and a minimum-IoU floor of `assoc-iou-threshold`.
- **Cross-modal track-to-track stability** via a `(camera_index, camera_track_id, 3d_track_id) → fused_id` table aged with `track-history-window`, keyed per camera so the same camera-track id from two different cameras maps to two distinct fused ids.

> **Detection must run *before* `gvastreammux`.** Once a 3D (LiDAR/radar) stream joins the mux, `gvastreammux` emits a CONTAINER batch (`multistream/x-analytics-batch`). So each camera runs its own `gvadetect` ahead of the mux; the fuser reads the resulting `GstAnalyticsODMtd` (camera streams) and `GstAnalytics3DODMtd` (3D stream) straight out of the batch.

## Properties
| Property              | Type           | Description                                                                                                      | Default |
|-----------------------|----------------|------------------------------------------------------------------------------------------------------------------|---------|
| `calibration`         | String         | Path to JSON file containing camera ↔ 3D calibration matrices. Required.                                          | NULL    |
| `assoc-iou-threshold` | Float [0, 1]   | Minimum IoU between a projected 3D box and a 2D camera box to count as a spatial association.                    | 0.3     |
| `track-history-window`| UInt           | Frames retained for the cross-modal track-to-track association table.                                            | 30      |
| `tracking-type`       | Enum `GstG3DFuserTrackingType` | Tracking algorithm used to identify the same object in multiple frames: `short-term-imageless` or `zero-term-imageless`. | `zero-term-imageless` |
| `tracking-space`      | Enum `GstG3DFuserTrackingSpace` | Coordinate frame the internal LiDAR tracker runs in: `bev` (top-down metric grid, camera-independent) or `image` (projected into the canonical camera plane). Fusion always uses image projection regardless. | `bev` |

## Pipeline Examples

`gvastreammux` performs the synchronisation; `sync-mode` makes the camera and 3D timelines comparable. The 3D sensor (LiDAR vs radar) is identified by the per-stream caps inside the batch (`application/x-lidar` vs `application/x-radar-processed`).

### Camera + Radar fusion
```bash
gst-launch-1.0 -v \
    rtspsrc location=$CAM_URI ! decodebin ! videoconvert ! \
        gvadetect model=$YOLO_MODEL ! mux.sink_0 \
    multifilesrc location=$RADAR_BIN caps="application/x-radar" ! \
        g3dradarprocess radar-config=$RADAR_CFG ! mux.sink_1 \
    gvastreammux name=mux output-mode=container sync-mode=first-pts ! \
        g3dobjectfuser calibration=samples/gstreamer/gst_launch/g3dobjectfuser/calib/radar_homography.json ! \
        g3drender ! videoconvert ! autovideosink
```

### Camera + LiDAR fusion
```bash
gst-launch-1.0 -v \
    rtspsrc location=$CAM_URI ! decodebin ! videoconvert ! \
        gvadetect model=$YOLO_MODEL ! mux.sink_0 \
    multifilesrc location=$LIDAR_BIN caps="application/octet-stream" ! \
        g3dlidarparse ! g3dinference config=$POINTPILLARS_CONFIG ! mux.sink_1 \
    gvastreammux name=mux output-mode=container sync-mode=first-pts ! \
        g3dobjectfuser calibration=samples/gstreamer/gst_launch/g3dobjectfuser/calib/kitti_calib.json ! \
        g3drender ! videoconvert ! autovideosink
```

For N cameras, add `gvadetect ! mux.sink_<n>` branches; the 3D stream goes on the next free `mux.sink_<n>`.

## Input / Output
Both the sink and src caps are `multistream/x-analytics-batch(meta:GstAnalyticsBatchMeta)`. The element transforms the container **in place** — it adds metadata to the inner per-stream buffers and forwards the same container.

**Input** (from `gvastreammux` in CONTAINER mode): one container buffer per batch tick. Its `GstAnalyticsBatchMeta` holds one `GstAnalyticsBatchStream` per source; each stream carries its source buffer in `objects[0]` and its original caps as a `GST_EVENT_CAPS` sticky event. The fuser classifies each stream by that caps name:
- `video/*` → a camera stream carrying `GstAnalyticsODMtd` from `gvadetect`.
- `application/x-lidar` → the LiDAR stream carrying `GstAnalytics3DODMtd` from `g3dinference`.
- `application/x-radar-processed` → the radar stream carrying `GstRadarProcessMeta` from `g3dradarprocess`.

**Output**: the same container buffer, with the fuser's analysis written onto the inner stream buffers' `GstAnalyticsRelationMeta`:
- Each camera stream keeps its `GstAnalyticsODMtd` untouched; the fuser adds a `GstAnalyticsTrackingMtd` per detection (linked via `RELATE_TO` to the OD mtd) for the per-camera tracker IDs. Cross-modal pairings are exposed by an `IS_PART_OF` relation from each camera OD mtd to a tracking mtd whose `tracking_id` is the id of the matching `GstAnalytics3DODMtd` on the 3D stream.
- The 3D stream carries every `GstAnalytics3DODMtd` (full LiDAR/radar pose) plus its tracking mtd.

Video-only batches (a tick with no coincident 3D frame) still get per-camera tracking; cross-modal fusion is simply a no-op for that tick.

When the batch is serialized with `gvametaconvert`, each `objects_3d` entry carries an `id` (its `GstAnalytics3DODMtd` id) and each fused camera detection carries `associated_3d_object_id`, the `id` of the 3D object it was paired with, so the `IS_PART_OF` link is resolvable by joining on that id.

## Metadata Structure: `GstAnalytics3DODMtd`
Tracking is carried separately in #GstAnalyticsTrackingMtd attached to the same relation meta and linked to the 3D OD mtd via #GST_ANALYTICS_REL_TYPE_RELATE_TO. Cross-modal pairs are linked via #GST_ANALYTICS_REL_TYPE_IS_PART_OF:
```c
/* Per detection: tracking */
gst_analytics_relation_meta_add_tracking_mtd(rmeta, track_id, pts, &tmtd);
gst_analytics_relation_meta_set_relation(rmeta,
    GST_ANALYTICS_REL_TYPE_RELATE_TO, three_d_mtd_id, tmtd.id);

/* Cross-modal pair. Relations are scoped to a single GstAnalyticsRelationMeta,
 * so the camera OD mtd cannot point directly at the 3D OD mtd (which lives on
 * the 3D-sensor buffer's relation meta). Materialise the link as a tracking mtd
 * on the camera relation meta whose tracking_id IS the 3D OD mtd's id, then
 * relate the camera OD mtd to it. Downstream looks that id up on the 3D stream. */
GstAnalyticsTrackingMtd link_mtd;
gst_analytics_relation_meta_add_tracking_mtd(cam_rmeta,
    /*tracking_id=*/threed_mtd_id, pts, &link_mtd);
gst_analytics_relation_meta_set_relation(cam_rmeta,
    GST_ANALYTICS_REL_TYPE_IS_PART_OF,
    camera_mtd_id, link_mtd.id);
```

## Calibration JSON schema
The `calibration` property points at one of two formats. Single-camera pipelines may use either the per-camera or root-level form; multi-camera pipelines (when `gvastreammux` merges several cameras) keep one entry per camera index (matching the `mux.sink_<n>` index).

LiDAR (KITTI style):
```text
{
  "tr_velo_to_cam": [r11,r12,r13,t1, r21,r22,r23,t2, r31,r32,r33,t3, 0,0,0,1],
  "r0_rect":        [r11,r12,r13,0,  r21,r22,r23,0,  r31,r32,r33,0,  0,0,0,1],
  "p2":             [p11,p12,p13,p14, p21,p22,p23,p24, p31,p32,p33,p34]
}
```

Radar (BEV → image plane):
```text
{
  "homography": [h11,h12,h13, h21,h22,h23, h31,h32,h33]
}
```

Multi-camera form (keyed by `gvastreammux` stream index — one entry per camera):
```text
{
  "cameras": {
    "0": { "tr_velo_to_cam": [...], "r0_rect": [...], "p2": [...] },
    "1": { "tr_velo_to_cam": [...], "r0_rect": [...], "p2": [...] }
  }
}
```

When `gvastreammux` merges multiple cameras, every output buffer's `GstAnalyticsBatchMeta.streams[0].index` selects which entry under `cameras` the fuser uses — so different intrinsics/extrinsics per camera Just Work.

## Calibration propagated downstream (`g3d/calibration` event)
The fuser is the single source of truth for calibration. Once the sensor modality is known (first 3D buffer), it pushes the loaded matrices downstream as a **sticky custom-downstream event** named `g3d/calibration`, so a downstream element (e.g., `g3drender`) can reproject the 3D boxes onto each camera image without being handed the file path again. The event is sticky, so late-linked or flushed branches still receive it.

Event `GstStructure` layout:

| Field | Type | Notes |
|---|---|---|
| *(name)* | — | `g3d/calibration` |
| `modality` | string | `"lidar"`, `"radar"`, or `"unknown"` |
| `cameras` | uint | number of `camera-<idx>` sub-structures |
| `camera-<idx>` | `GstStructure` | one per camera, keyed by the `gvastreammux` stream index |

Each `camera-<idx>` sub-structure holds (matrices as `GST_TYPE_ARRAY` of floats, row-major):
- LiDAR mode: `tr_velo_to_cam` (16), `r0_rect` (16), `p2` (12), plus `index` (int).
- Radar mode: `homography` (9), plus `index` (int).

Consumer sketch (sink_event):
```c
if (GST_EVENT_TYPE(ev) == GST_EVENT_CUSTOM_DOWNSTREAM_STICKY &&
    gst_event_has_name(ev, "g3d/calibration")) {
    const GstStructure *s = gst_event_get_structure(ev);
    /* read "modality", iterate "camera-<idx>" sub-structures, cache matrices */
}
```

## Element Details (gst-inspect-1.0)
```
Pad Templates:
  SINK template: 'sink'
    Availability: Always
    Capabilities:
      multistream/x-analytics-batch(meta:GstAnalyticsBatchMeta)

  SRC template: 'src'
    Availability: Always
    Capabilities:
      multistream/x-analytics-batch(meta:GstAnalyticsBatchMeta)

Element Properties:
  calibration          : Path to JSON file containing calibration matrices for camera <-> 3D sensor projection
                         flags: readable, writable
                         String. Default: null
  assoc-iou-threshold  : Minimum IoU between projected 3D box and 2D camera box for association
                         flags: readable, writable
                         Float. Range: 0 - 1 Default: 0.3
  track-history-window : Frames retained for the cross-modal track-to-track association table
                         flags: readable, writable
                         Unsigned Integer. Range: 1 - 1000 Default: 30
  tracking-type        : Tracking algorithm used to identify the same object in multiple frames.
                         flags: readable, writable
                         Enum "GstG3DFuserTrackingType" Default: 5, "zero-term-imageless"
                            (4): short-term-imageless - Short-term imageless tracker
                            (5): zero-term-imageless  - Zero-term imageless tracker
  tracking-space       : Coordinate frame the internal LiDAR tracker runs in: 'bev' (top-down, metric,
                         camera-independent) or 'image' (projected into the camera plane). Camera-to-3D
                         fusion always uses image projection regardless of this setting.
                         flags: readable, writable
                         Enum "GstG3DFuserTrackingSpace" Default: 1, "bev"
                            (0): image             - Track LiDAR boxes projected into the camera image plane
                            (1): bev               - Track LiDAR boxes in a top-down bird's-eye-view grid
```
