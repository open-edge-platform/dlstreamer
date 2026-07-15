/*******************************************************************************
 * Copyright (C) 2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#include "g3d_test_harness.h"
#include "g3d_test_synth.h"

#include <gst/analytics/gstanalyticsmeta.h>
#include <gst/analytics/gstanalyticsbatchmeta.h>
#include <gst/analytics/gstanalyticsobjectdetectionmtd.h>
#include <gst/analytics/gstanalyticsobjecttrackingmtd.h>
#include <gst/video/video.h>
#include <dlstreamer/gst/metadata/g3d_lidar_meta.h>
#include <dlstreamer/gst/metadata/g3d_od_mtd.h>

#include <cstring>

/* ── synthetic detection data (private to this translation unit) ─────── */

/*
 * 3D detections — LiDAR: X=forward, Y=left.  All at R=20 m, z=0, yaw=0.
 *   D: 30° left  → (17.32,  10,    0)  matched to camera box B
 *   E: 30° right → (17.32, -10,    0)  matched to camera box C
 *   F: 60° right → (10,    -17.32, 0)  unmatched (outside camera FOV)
 */
typedef struct { float x, y, z, l, w, h, yaw, score; gint class_id; } G3drDet3D;

static const G3drDet3D SYNTH_DETS_3D[] = {
    { 17.32f,   10.0f, 0.f, 4.f, 2.f, 1.5f, 0.f, 0.9f, 0 }, /* D: 30°L */
    { 17.32f,  -10.0f, 0.f, 4.f, 2.f, 1.5f, 0.f, 0.8f, 0 }, /* E: 30°R */
    { 10.0f,  -17.32f, 0.f, 4.f, 2.f, 1.5f, 0.f, 0.7f, 1 }, /* F: 60°R */
};

/*
 * 2D detections — 640×240 camera image.
 * f=180, cx=320 → 30° lands at ±104 px from cx; ~60° lands near left edge.
 *   A: ~58° left  (u=30)  — unmatched
 *   B: ~30° left  (u=214) — matched to lidar D  (D projects to u≈214 ✓)
 *   C: ~30° right (u=424) — matched to lidar E  (E projects to u≈426 ✓)
 */
typedef struct { gint x, y, w, h; gfloat conf; const gchar *cls; } SynthDet2D;

static const SynthDet2D SYNTH_DETS_2D[] = {
    {  10, 105, 40, 30, 0.7f, "truck" }, /* A — unmatched        */
    { 194, 105, 40, 30, 0.9f, "car"   }, /* B — matched to D     */
    { 404, 105, 40, 30, 0.8f, "car"   }, /* C — matched to E     */
};

/*
 * Calibration matrices (cam-proj scenarios).
 * tr_velo_to_cam: real KITTI sequence-07 extrinsic.
 * r0_rect: identity (same as calib.json).
 * P2: synthetic intrinsics for 640×240 image, f=180, cx=320, cy=120.
 */
static const gfloat SYNTH_TR_VELO_TO_CAM[16] = {
    -1.857739385241e-03f, -9.999659513510e-01f, -8.039975204516e-03f, -4.784029760483e-03f,
    -6.481465826011e-03f,  8.051860151134e-03f, -9.999466081774e-01f, -7.337429464231e-02f,
     9.999773098287e-01f, -1.805528627661e-03f, -6.496203536139e-03f, -3.339968064433e-01f,
     0.f,                  0.f,                  0.f,                  1.f,
};
static const gfloat SYNTH_R0_RECT[16] = {
    1.f, 0.f, 0.f, 0.f,
    0.f, 1.f, 0.f, 0.f,
    0.f, 0.f, 1.f, 0.f,
    0.f, 0.f, 0.f, 1.f,
};
static const gfloat SYNTH_P2[12] = {
    180.f,   0.f, 320.f, 0.f,
      0.f, 180.f, 120.f, 0.f,
      0.f,   0.f,   1.f, 0.f,
};

/* ── internal helpers ────────────────────────────────────────────────── */

static void
apply_scenario_props(const Scenario *sc, GstHarness *h)
{
    GstElement *el = gst_harness_find_element(h, "g3drender");
    g_object_set(el,
        "width",            sc->width,
        "height",           sc->height,
        "range-x-min",      sc->range_x_min,
        "range-x-max",      sc->range_x_max,
        "range-y-min",      sc->range_y_min,
        "range-y-max",      sc->range_y_max,
        "point-radius",     sc->point_radius,
        "point-stride",     sc->point_stride,
        "zoom",             sc->zoom,
        "view-mode",        sc->view_mode,
        "cam-distance",     sc->cam_distance,
        "cam-elevation",    sc->cam_elevation,
        "cam-azimuth",      sc->cam_azimuth,
        "cam-azimuth-step", sc->cam_azimuth_step,
        "cam-fov",          sc->cam_fov,
        NULL);
    gst_object_unref(el);
}

/*
 * Attach 3D OD detections to @buf.
 * If @ids_out is non-NULL it receives the GstAnalyticsMtd.id for each detection
 * (needed to build camera↔lidar associations afterwards).
 */
static void
attach_3d_dets(GstBuffer *buf, guint32 n_dets, const G3drDet3D *dets, guint *ids_out)
{
    if (n_dets == 0)
        return;
    GstAnalyticsRelationMeta *rmeta = gst_buffer_add_analytics_relation_meta(buf);
    for (guint32 i = 0; i < n_dets; i++) {
        GstAnalytics3DODMtd mtd = {};
        gst_analytics_relation_meta_add_3d_od_mtd(
            rmeta,
            dets[i].x, dets[i].y, dets[i].z,
            dets[i].l, dets[i].w, dets[i].h,
            dets[i].yaw, 0.f, 0.f,
            dets[i].class_id, dets[i].score,
            GST_ANALYTICS_3D_SENSOR_LIDAR, &mtd);
        if (ids_out)
            ids_out[i] = mtd.id;
    }
}

/*
 * Attach 2D detections A, B, C to a camera buffer:
 *   A — standalone (unmatched, drawn blue)
 *   B — IS_PART_OF → TrackingMtd(tracking_id=@d_id)  → D drawn white
 *   C — IS_PART_OF → TrackingMtd(tracking_id=@e_id)  → E drawn white
 */
static void
attach_2d_dets_assoc(GstBuffer *buf, guint d_id, guint e_id)
{
    GstAnalyticsRelationMeta *rmeta = gst_buffer_add_analytics_relation_meta(buf);

    GstAnalyticsODMtd od_a = {};
    gst_analytics_relation_meta_add_od_mtd(rmeta,
        g_quark_from_static_string(SYNTH_DETS_2D[0].cls),
        SYNTH_DETS_2D[0].x, SYNTH_DETS_2D[0].y,
        SYNTH_DETS_2D[0].w, SYNTH_DETS_2D[0].h,
        SYNTH_DETS_2D[0].conf, &od_a);

    GstAnalyticsODMtd od_b = {};
    gst_analytics_relation_meta_add_od_mtd(rmeta,
        g_quark_from_static_string(SYNTH_DETS_2D[1].cls),
        SYNTH_DETS_2D[1].x, SYNTH_DETS_2D[1].y,
        SYNTH_DETS_2D[1].w, SYNTH_DETS_2D[1].h,
        SYNTH_DETS_2D[1].conf, &od_b);
    GstAnalyticsTrackingMtd trk_b = {};
    gst_analytics_relation_meta_add_tracking_mtd(rmeta, (guint64)d_id, GST_CLOCK_TIME_NONE, &trk_b);
    gst_analytics_relation_meta_set_relation(rmeta,
        GST_ANALYTICS_REL_TYPE_IS_PART_OF, od_b.id, trk_b.id);

    GstAnalyticsODMtd od_c = {};
    gst_analytics_relation_meta_add_od_mtd(rmeta,
        g_quark_from_static_string(SYNTH_DETS_2D[2].cls),
        SYNTH_DETS_2D[2].x, SYNTH_DETS_2D[2].y,
        SYNTH_DETS_2D[2].w, SYNTH_DETS_2D[2].h,
        SYNTH_DETS_2D[2].conf, &od_c);
    GstAnalyticsTrackingMtd trk_c = {};
    gst_analytics_relation_meta_add_tracking_mtd(rmeta, (guint64)e_id, GST_CLOCK_TIME_NONE, &trk_c);
    gst_analytics_relation_meta_set_relation(rmeta,
        GST_ANALYTICS_REL_TYPE_IS_PART_OF, od_c.id, trk_c.id);
}

static void
append_float_array_field(GstStructure *s, const gchar *key, const gfloat *data, gint n)
{
    GValue arr = G_VALUE_INIT;
    g_value_init(&arr, GST_TYPE_ARRAY);
    for (gint i = 0; i < n; i++) {
        GValue v = G_VALUE_INIT;
        g_value_init(&v, G_TYPE_FLOAT);
        g_value_set_float(&v, data[i]);
        gst_value_array_append_value(&arr, &v);
        g_value_unset(&v);
    }
    gst_structure_take_value(s, key, &arr);
}

/* ── public API ──────────────────────────────────────────────────────── */

GstHarness *
make_lidar_harness(const Scenario *sc)
{
    GstHarness *h = gst_harness_new("g3drender");
    gst_harness_set_src_caps_str(h, "application/x-lidar");
    apply_scenario_props(sc, h);
    return h;
}

GstHarness *
make_batch_harness(const Scenario *sc)
{
    GstHarness *h = gst_harness_new("g3drender");
    gst_harness_set_src_caps_str(h,
        "multistream/x-analytics-batch(meta:GstAnalyticsBatchMeta)");
    apply_scenario_props(sc, h);
    return h;
}

GstBuffer *
build_lidar_buf(GstHarness *h, const Scenario *sc)
{
    float     *pts  = make_lidar_pts();
    gsize      size = SYNTH_N_LIDAR_TOTAL * 4 * sizeof(float);
    GstBuffer *buf  = gst_harness_create_buffer(h, size);
    GstMapInfo m;
    gst_buffer_map(buf, &m, GST_MAP_WRITE);
    memcpy(m.data, pts, size);
    gst_buffer_unmap(buf, &m);
    g_free(pts);
    add_lidar_meta(buf, SYNTH_N_LIDAR_TOTAL, 0, GST_CLOCK_TIME_NONE, 0);
    if (sc->has_3d_dets)
        attach_3d_dets(buf, G_N_ELEMENTS(SYNTH_DETS_3D), SYNTH_DETS_3D, NULL);
    return buf;
}

/*
 * Batch buffer: n_cams chessboard cameras + one lidar stream.
 * Lidar is built first so that 3D OD mtd.ids are known before constructing
 * camera buffers that reference them via IS_PART_OF → TrackingMtd.
 */
GstBuffer *
build_batch_buf(GstHarness *h, const Scenario *sc)
{
    GstBuffer             *batch_buf = gst_buffer_new();
    GstAnalyticsBatchMeta *meta      = gst_buffer_add_analytics_batch_meta(batch_buf);
    guint32 n_streams = (guint32)sc->n_cams + 1;
    meta->n_streams = n_streams;
    meta->streams   = g_new0(GstAnalyticsBatchStream, n_streams);

    /* lidar stream */
    float     *pts      = make_lidar_pts();
    gsize      lidar_sz = SYNTH_N_LIDAR_TOTAL * 4 * sizeof(float);
    GstBuffer *lidar_buf = gst_harness_create_buffer(h, lidar_sz);
    {
        GstMapInfo lm;
        gst_buffer_map(lidar_buf, &lm, GST_MAP_WRITE);
        memcpy(lm.data, pts, lidar_sz);
        gst_buffer_unmap(lidar_buf, &lm);
    }
    g_free(pts);
    add_lidar_meta(lidar_buf, SYNTH_N_LIDAR_TOTAL, 0, GST_CLOCK_TIME_NONE, 0);

    guint det3d_ids[G_N_ELEMENTS(SYNTH_DETS_3D)] = {};
    if (sc->has_3d_dets)
        attach_3d_dets(lidar_buf, G_N_ELEMENTS(SYNTH_DETS_3D), SYNTH_DETS_3D, det3d_ids);

    GstAnalyticsBatchStream *lidar_stream = &meta->streams[sc->n_cams];
    lidar_stream->index      = sc->n_cams;
    lidar_stream->n_objects  = 1;
    lidar_stream->objects    = g_new0(GstMiniObject *, 1);
    lidar_stream->objects[0] = GST_MINI_OBJECT_CAST(lidar_buf);

    GstCaps *lidar_caps = gst_caps_from_string("application/x-lidar");
    lidar_stream->n_sticky_events  = 1;
    lidar_stream->sticky_events    = g_new0(GstEvent *, 1);
    lidar_stream->sticky_events[0] = gst_event_new_caps(lidar_caps);
    gst_caps_unref(lidar_caps);

    /* camera streams */
    for (gint i = 0; i < sc->n_cams; i++) {
        GstAnalyticsBatchStream *stream = &meta->streams[i];
        stream->index = i;

        guint8 *raw_bgr = make_chessboard_bgr(sc->cam_w, sc->cam_h, i);

        GstVideoInfo vinfo;
        gst_video_info_set_format(&vinfo, GST_VIDEO_FORMAT_BGR, sc->cam_w, sc->cam_h);
        gsize      cam_size = GST_VIDEO_INFO_SIZE(&vinfo);
        gint       stride   = GST_VIDEO_INFO_PLANE_STRIDE(&vinfo, 0);
        GstBuffer *cam_buf  = gst_buffer_new_allocate(NULL, cam_size, NULL);
        GstMapInfo m;
        gst_buffer_map(cam_buf, &m, GST_MAP_WRITE);
        memset(m.data, 0, cam_size);
        for (gint r = 0; r < sc->cam_h; r++)
            memcpy(m.data + (gsize)r * stride,
                   raw_bgr + (gsize)r * sc->cam_w * 3,
                   sc->cam_w * 3);
        gst_buffer_unmap(cam_buf, &m);
        g_free(raw_bgr);

        if (sc->has_2d_dets)
            attach_2d_dets_assoc(cam_buf, det3d_ids[0], det3d_ids[1]);

        stream->n_objects  = 1;
        stream->objects    = g_new0(GstMiniObject *, 1);
        stream->objects[0] = GST_MINI_OBJECT_CAST(cam_buf);

        GstCaps *caps = gst_caps_new_simple("video/x-raw",
            "format", G_TYPE_STRING, "BGR",
            "width",  G_TYPE_INT,    sc->cam_w,
            "height", G_TYPE_INT,    sc->cam_h, NULL);
        stream->n_sticky_events  = 1;
        stream->sticky_events    = g_new0(GstEvent *, 1);
        stream->sticky_events[0] = gst_event_new_caps(caps);
        gst_caps_unref(caps);
    }

    return batch_buf;
}

/* ── edge-case buffer builders (TC-20 / TC-21 / TC-22) ──────────────────── */

/*
 * TC-20: LidarMeta is present but point_count == 0.
 * All rendering loops iterate zero times — the element must output a valid
 * (dark background + grid) frame without touching the NULL/empty data pointer.
 */
GstBuffer *
build_empty_lidar_buf(GstHarness *h)
{
    GstBuffer *buf = gst_buffer_new_allocate(NULL, 0, NULL);
    (void)h;
    add_lidar_meta(buf, 0, 0, GST_CLOCK_TIME_NONE, 0);
    return buf;
}

/*
 * TC-21: LiDAR buffer carries one dummy point but has no LidarMeta attached.
 * g3drender logs a WARNING and skips rendering — the element must still
 * produce a valid output frame (blank canvas).
 */
GstBuffer *
build_no_meta_lidar_buf(GstHarness *h)
{
    float dummy[4] = { 1.f, 0.f, 0.f, 0.5f };  /* x y z intensity */
    gsize      sz  = sizeof(dummy);
    GstBuffer *buf = gst_harness_create_buffer(h, sz);
    GstMapInfo m;
    gst_buffer_map(buf, &m, GST_MAP_WRITE);
    memcpy(m.data, dummy, sz);
    gst_buffer_unmap(buf, &m);
    /* intentionally omit add_lidar_meta — tests the no-meta code path */
    return buf;
}

/*
 * TC-22: Batch buffer contains only camera sub-streams — no "application/x-lidar"
 * sub-stream.  extract_lidar_from_batch() returns NULL, the element logs a WARNING,
 * skips LiDAR rendering, and still composites the camera panel into the output.
 */
GstBuffer *
build_cam_only_batch_buf(GstHarness *h, const Scenario *sc)
{
    (void)h;
    GstBuffer             *batch_buf = gst_buffer_new();
    GstAnalyticsBatchMeta *meta      = gst_buffer_add_analytics_batch_meta(batch_buf);
    guint32 n_cams = (guint32)sc->n_cams;
    meta->n_streams = n_cams;
    meta->streams   = g_new0(GstAnalyticsBatchStream, n_cams);

    for (gint i = 0; i < sc->n_cams; i++) {
        GstAnalyticsBatchStream *stream = &meta->streams[i];
        stream->index = i;

        guint8 *raw_bgr = make_chessboard_bgr(sc->cam_w, sc->cam_h, i);
        GstVideoInfo vinfo;
        gst_video_info_set_format(&vinfo, GST_VIDEO_FORMAT_BGR, sc->cam_w, sc->cam_h);
        gsize      cam_size = GST_VIDEO_INFO_SIZE(&vinfo);
        gint       stride   = GST_VIDEO_INFO_PLANE_STRIDE(&vinfo, 0);
        GstBuffer *cam_buf  = gst_buffer_new_allocate(NULL, cam_size, NULL);
        GstMapInfo m;
        gst_buffer_map(cam_buf, &m, GST_MAP_WRITE);
        memset(m.data, 0, cam_size);
        for (gint r = 0; r < sc->cam_h; r++)
            memcpy(m.data + (gsize)r * stride,
                   raw_bgr + (gsize)r * sc->cam_w * 3,
                   sc->cam_w * 3);
        gst_buffer_unmap(cam_buf, &m);
        g_free(raw_bgr);

        stream->n_objects  = 1;
        stream->objects    = g_new0(GstMiniObject *, 1);
        stream->objects[0] = GST_MINI_OBJECT_CAST(cam_buf);

        GstCaps *caps = gst_caps_new_simple("video/x-raw",
            "format", G_TYPE_STRING, "BGR",
            "width",  G_TYPE_INT,    sc->cam_w,
            "height", G_TYPE_INT,    sc->cam_h, NULL);
        stream->n_sticky_events  = 1;
        stream->sticky_events    = g_new0(GstEvent *, 1);
        stream->sticky_events[0] = gst_event_new_caps(caps);
        gst_caps_unref(caps);
    }
    return batch_buf;
}

GstEvent *
build_calib_event(void)
{
    GstStructure *cam = gst_structure_new_empty("camera");
    gst_structure_set(cam, "index", G_TYPE_INT, 0, NULL);
    append_float_array_field(cam, "tr_velo_to_cam", SYNTH_TR_VELO_TO_CAM, 16);
    append_float_array_field(cam, "r0_rect",        SYNTH_R0_RECT, 16);
    append_float_array_field(cam, "p2",             SYNTH_P2, 12);

    GstStructure *root = gst_structure_new_empty("g3d/calibration");
    gst_structure_set(root, "modality", G_TYPE_STRING, "lidar", NULL);
    gst_structure_set(root, "cameras",  G_TYPE_UINT,   1u,      NULL);

    GValue cam_val = G_VALUE_INIT;
    g_value_init(&cam_val, GST_TYPE_STRUCTURE);
    g_value_take_boxed(&cam_val, cam);
    gst_structure_take_value(root, "camera-0", &cam_val);

    return gst_event_new_custom(GST_EVENT_CUSTOM_DOWNSTREAM_STICKY, root);
}
