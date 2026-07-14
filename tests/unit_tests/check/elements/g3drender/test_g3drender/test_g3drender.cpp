/*******************************************************************************
 * Copyright (C) 2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#include <gst/check/gstcheck.h>
#include <gst/check/gstharness.h>
#include <gst/analytics/gstanalyticsmeta.h>
#include <gst/analytics/gstanalyticsbatchmeta.h>
#include <gst/video/video.h>

#include <cstdio>
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image.h"
#include "stb_image_write.h"
#include <dlstreamer/gst/metadata/g3d_lidar_meta.h>
#include <dlstreamer/gst/metadata/g3d_od_mtd.h>

/* ── scenario table ───────────────────────────────────────────────────── */

typedef struct {
    const gchar *subdir;      /* scenario label used in log/error messages */
    const gchar *raw_file;    /* path to .g3dr fixture under source_files/, relative to TEST_FILES_DIR */
    const gchar *out_name;    /* golden PPM filename under golden_files/  */
    gboolean     is_batch;
    /* canvas */
    gint   width;
    gint   height;
    /* BEV range (meters) */
    gfloat range_x_min;
    gfloat range_x_max;
    gfloat range_y_min;
    gfloat range_y_max;
    /* point rendering */
    gint   point_radius;
    gint   point_stride;
    gfloat zoom;
    /* view mode: 0=bev  1=perspective */
    gint   view_mode;
    /* perspective camera */
    gfloat cam_distance;
    gfloat cam_elevation;
    gfloat cam_azimuth;
    gfloat cam_azimuth_step;
    gfloat cam_fov;
    /* calib JSON for cam-proj mode (NULL for other modes) */
    const gchar *calib_file;
} Scenario;

/*             subdir                    raw_file                                     out_name                    batch   W     H    xmin xmax  ymin ymax  rad str  zoom  mode dist elev  az   az_step fov  calib */
static const Scenario SCENARIOS[] = {
    { "01_pure_lidar",           "source_files/01_pure_lidar.g3dr",           "01_pure_lidar.png",           FALSE, 800,  800, -50, 50, -50, 50, 2, 16, 2.0f, 0, 35, 30, 180, 0, 60, NULL },
    { "02_lidar_inference",      "source_files/02_lidar_inference.g3dr",      "02_lidar_inference.png",      FALSE, 800,  800, -50, 50, -50, 50, 2, 16, 2.0f, 1, 35, 30, 180, 0, 60, NULL },
    { "03_batch_1cam",           "source_files/03_batch_1cam.g3dr",           "03_batch_1cam.png",           TRUE,  1600, 800, -50, 50, -50, 50, 2, 16, 2.0f, 0, 35, 30, 180, 0, 60, NULL },
    { "04_batch_1cam_inference", "source_files/04_batch_1cam_inference.g3dr", "04_batch_1cam_inference.png", TRUE,  1600, 800, -50, 50, -50, 50, 2, 16, 2.0f, 1, 35, 30, 180, 0, 60, NULL },
    { "05_batch_2cam",           "source_files/05_batch_2cam.g3dr",           "05_batch_2cam.png",           TRUE,  1600, 800, -50, 50, -50, 50, 2, 16, 2.0f, 0, 35, 30, 180, 0, 60, NULL },
    { "06_batch_2cam_inference", "source_files/06_batch_2cam_inference.g3dr", "06_batch_2cam_inference.png", TRUE,  1600, 800, -50, 50, -50, 50, 2, 16, 2.0f, 1, 35, 30, 180, 0, 60, NULL },
};

/* cam-proj scenarios: require a g3d/calibration sticky event */
static const Scenario CAM_PROJ_SCENARIOS[] = {
    { "07_batch_1cam_project",   "source_files/03_batch_1cam.g3dr",           "07_batch_1cam_project.png",   TRUE,  1600, 800, -50, 50, -50, 50, 2, 16, 2.0f, 2, 35, 30, 180, 0, 60, "source_files/calib.json" },
};

/* ── .g3dr data structures ────────────────────────────────────────────── */

#ifndef TEST_FILES_DIR
#define TEST_FILES_DIR ""
#endif

#define G3DR_MAGIC   0x47334452u
#define G3DR_VERSION 1u

typedef struct {
    float x, y, z, l, w, h, yaw, score;
    gint  class_id;
} G3drDet3D;

typedef struct {
    gint   x, y, w, h;
    gfloat conf;
    gint   class_id;
} G3drDet2D;

typedef struct {
    guint32    width;
    guint32    height;
    guint8    *bgr;      /* g_malloc'd, width*height*3 bytes */
    guint32    n_dets;
    G3drDet2D *dets;     /* g_malloc'd */
} G3drCam;

typedef struct {
    gboolean   is_batch;
    guint32    n_pts;
    float     *pts;      /* g_malloc'd, n_pts*4 floats */
    guint32    n_3d_dets;
    G3drDet3D *dets3d;   /* g_malloc'd */
    /* batch-only */
    guint32    n_cams;
    G3drCam   *cams;     /* g_malloc'd */
} G3drFrame;

static void
g3dr_frame_free(G3drFrame *fr)
{
    g_free(fr->pts);
    g_free(fr->dets3d);
    for (guint32 i = 0; i < fr->n_cams; i++) {
        g_free(fr->cams[i].bgr);
        g_free(fr->cams[i].dets);
    }
    g_free(fr->cams);
}

/* ── .g3dr fixture parser ─────────────────────────────────────────────── */

static gboolean
g3dr_read_frame(const gchar *path, G3drFrame *out)
{
    FILE *f = fopen(path, "rb");
    if (!f)
        return FALSE;

    memset(out, 0, sizeof(*out));

#define RCHK(expr) do { if (!(expr)) { fclose(f); g3dr_frame_free(out); return FALSE; } } while(0)

    guint32 magic, version;
    guint8  is_batch_u8;
    RCHK(fread(&magic,      sizeof(magic),    1, f) == 1);
    RCHK(fread(&version,    sizeof(version),  1, f) == 1);
    RCHK(fread(&is_batch_u8, 1,               1, f) == 1);
    RCHK(magic == G3DR_MAGIC && version == G3DR_VERSION);
    out->is_batch = (gboolean)is_batch_u8;

    /* lidar points */
    RCHK(fread(&out->n_pts, sizeof(out->n_pts), 1, f) == 1);
    gsize nf = (gsize)out->n_pts * 4;
    out->pts = (float *)g_malloc(nf * sizeof(float));
    if (out->n_pts)
        RCHK(fread(out->pts, sizeof(float), nf, f) == nf);

    /* 3D detections */
    RCHK(fread(&out->n_3d_dets, sizeof(out->n_3d_dets), 1, f) == 1);
    out->dets3d = (G3drDet3D *)g_malloc((out->n_3d_dets ? out->n_3d_dets : 1) * sizeof(G3drDet3D));
    for (guint32 i = 0; i < out->n_3d_dets; i++) {
        RCHK(fread(&out->dets3d[i].x,        sizeof(float), 1, f) == 1);
        RCHK(fread(&out->dets3d[i].y,        sizeof(float), 1, f) == 1);
        RCHK(fread(&out->dets3d[i].z,        sizeof(float), 1, f) == 1);
        RCHK(fread(&out->dets3d[i].l,        sizeof(float), 1, f) == 1);
        RCHK(fread(&out->dets3d[i].w,        sizeof(float), 1, f) == 1);
        RCHK(fread(&out->dets3d[i].h,        sizeof(float), 1, f) == 1);
        RCHK(fread(&out->dets3d[i].yaw,      sizeof(float), 1, f) == 1);
        RCHK(fread(&out->dets3d[i].score,    sizeof(float), 1, f) == 1);
        RCHK(fread(&out->dets3d[i].class_id, sizeof(gint),  1, f) == 1);
    }

    /* camera streams (batch only) */
    if (out->is_batch) {
        RCHK(fread(&out->n_cams, sizeof(out->n_cams), 1, f) == 1);
        out->cams = (G3drCam *)g_malloc0((out->n_cams ? out->n_cams : 1) * sizeof(G3drCam));
        for (guint32 i = 0; i < out->n_cams; i++) {
            G3drCam *cam = &out->cams[i];
            guint32 data_sz;
            RCHK(fread(&cam->width,  sizeof(cam->width),  1, f) == 1);
            RCHK(fread(&cam->height, sizeof(cam->height), 1, f) == 1);
            RCHK(fread(&data_sz,     sizeof(data_sz),     1, f) == 1);
            cam->bgr = (guint8 *)g_malloc(data_sz ? data_sz : 1);
            if (data_sz)
                RCHK(fread(cam->bgr, 1, data_sz, f) == data_sz);
            /* 2D detections */
            RCHK(fread(&cam->n_dets, sizeof(cam->n_dets), 1, f) == 1);
            cam->dets = (G3drDet2D *)g_malloc((cam->n_dets ? cam->n_dets : 1) * sizeof(G3drDet2D));
            for (guint32 j = 0; j < cam->n_dets; j++) {
                RCHK(fread(&cam->dets[j].x,        sizeof(gint),   1, f) == 1);
                RCHK(fread(&cam->dets[j].y,        sizeof(gint),   1, f) == 1);
                RCHK(fread(&cam->dets[j].w,        sizeof(gint),   1, f) == 1);
                RCHK(fread(&cam->dets[j].h,        sizeof(gint),   1, f) == 1);
                RCHK(fread(&cam->dets[j].conf,     sizeof(gfloat), 1, f) == 1);
                RCHK(fread(&cam->dets[j].class_id, sizeof(gint),   1, f) == 1);
            }
        }
    }
#undef RCHK

    fclose(f);
    return TRUE;
}

/* ── output / golden helpers ──────────────────────────────────────────── */

/* Max per-channel deviation allowed against the golden reference. */
#define MAX_PIXEL_DIFF 1

/* Save BGR frame as PNG (BGR → RGB swap handled by stb_image_write). */
static void
save_png(const gchar *path, const guint8 *bgr, gint w, gint h)
{
    /* stb_image_write expects RGB; convert BGR → RGB into a temp buffer */
    gsize   n   = (gsize)w * h * 3;
    guint8 *rgb = (guint8 *)g_malloc(n);
    for (gsize i = 0; i < (gsize)w * h; i++) {
        rgb[i * 3 + 0] = bgr[i * 3 + 2];
        rgb[i * 3 + 1] = bgr[i * 3 + 1];
        rgb[i * 3 + 2] = bgr[i * 3 + 0];
    }
    if (!stbi_write_png(path, w, h, 3, rgb, w * 3))
        g_warning("save_png: failed to write %s", path);
    else
        g_print("  saved -> %s\n", path);
    g_free(rgb);
}

/*
 * Load a PNG file.  Returns g_malloc'd RGB data (w*h*3 bytes) via stb_image.
 * Sets *w_out / *h_out.  Returns NULL on any error.
 */
static guint8 *
load_png(const gchar *path, gint *w_out, gint *h_out)
{
    int w, h, ch;
    stbi_uc *raw = stbi_load(path, &w, &h, &ch, 3); /* force 3-channel RGB */
    if (!raw)
        return NULL;

    gsize   n    = (gsize)w * h * 3;
    guint8 *data = (guint8 *)g_memdup2(raw, n);
    stbi_image_free(raw);

    *w_out = w;
    *h_out = h;
    return data;
}

/*
 * Compare a rendered BGR frame against golden RGB (from PPM).
 * Returns the maximum per-channel absolute difference found,
 * or -1 if dimensions do not match.
 * On failure writes pixel coordinates and values to *fail_out (static buf).
 */
static gint
compare_with_golden(const guint8 *bgr,        gint w,  gint h,
                    const guint8 *golden_rgb,  gint gw, gint gh,
                    gchar        *fail_msg,    gsize fail_msg_len)
{
    if (w != gw || h != gh) {
        g_snprintf(fail_msg, fail_msg_len,
                   "size mismatch: render=%dx%d golden=%dx%d", w, h, gw, gh);
        return -1;
    }

    gint max_diff = 0;
    gsize worst   = 0;
    for (gsize i = 0; i < (gsize)w * h; i++) {
        /* render = BGR, golden PPM = RGB → swap R↔B */
        gint dr = abs((gint)bgr[i*3+2] - (gint)golden_rgb[i*3+0]);
        gint dg = abs((gint)bgr[i*3+1] - (gint)golden_rgb[i*3+1]);
        gint db = abs((gint)bgr[i*3+0] - (gint)golden_rgb[i*3+2]);
        gint d  = MAX(MAX(dr, dg), db);
        if (d > max_diff) { max_diff = d; worst = i; }
    }

    if (max_diff > MAX_PIXEL_DIFF) {
        gint px = (gint)(worst % w), py = (gint)(worst / w);
        g_snprintf(fail_msg, fail_msg_len,
                   "max diff %d at (%d,%d): render BGR=(%d,%d,%d) golden RGB=(%d,%d,%d)",
                   max_diff, px, py,
                   bgr[worst*3+2], bgr[worst*3+1], bgr[worst*3+0],
                   golden_rgb[worst*3+0], golden_rgb[worst*3+1], golden_rgb[worst*3+2]);
    }
    return max_diff;
}

/* ── harness setup ────────────────────────────────────────────────────── */

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

/* ── calib event helpers (for cam-proj mode) ──────────────────────────── */

/* Parse a named float array from a minimal JSON text.
 * Finds "key": [ f0, f1, ... ] and stores the first @n values into @out. */
static gboolean
parse_json_float_array(const gchar *text, const gchar *key, gfloat *out, gint n)
{
    const gchar *p = strstr(text, key);
    if (!p) return FALSE;
    p = strchr(p, '[');
    if (!p) return FALSE;
    p++; /* skip '[' */
    for (gint i = 0; i < n; i++) {
        char *end;
        while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r' || *p == ',')
            p++;
        if (*p == ']' || *p == '\0') return FALSE;
        out[i] = strtof(p, &end);
        if (end == p) return FALSE;
        p = end;
    }
    return TRUE;
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

/* Read calib.json and build a GST_EVENT_CUSTOM_DOWNSTREAM_STICKY matching
 * the "g3d/calibration" layout produced by g3dobjectfuser. */
static GstEvent *
build_calib_event(const gchar *calib_path)
{
    gchar *text = NULL;
    if (!g_file_get_contents(calib_path, &text, NULL, NULL)) {
        g_printerr("  [WARN] cannot read calib file: %s\n", calib_path);
        return NULL;
    }

    gfloat tr[16], r0[16], p2[12];
    gboolean ok =
        parse_json_float_array(text, "tr_velo_to_cam", tr, 16) &&
        parse_json_float_array(text, "r0_rect",        r0, 16) &&
        parse_json_float_array(text, "p2",             p2, 12);
    g_free(text);

    if (!ok) {
        g_printerr("  [WARN] failed to parse calib matrices from: %s\n", calib_path);
        return NULL;
    }

    GstStructure *cam = gst_structure_new_empty("camera");
    gst_structure_set(cam, "index", G_TYPE_INT, 0, NULL);
    append_float_array_field(cam, "tr_velo_to_cam", tr, 16);
    append_float_array_field(cam, "r0_rect",        r0, 16);
    append_float_array_field(cam, "p2",             p2, 12);

    GstStructure *root = gst_structure_new_empty("g3d/calibration");
    gst_structure_set(root, "modality", G_TYPE_STRING, "lidar", NULL);
    gst_structure_set(root, "cameras",  G_TYPE_UINT,   1u,      NULL);

    GValue cam_val = G_VALUE_INIT;
    g_value_init(&cam_val, GST_TYPE_STRUCTURE);
    g_value_take_boxed(&cam_val, cam);
    gst_structure_take_value(root, "camera-0", &cam_val);

    return gst_event_new_custom(GST_EVENT_CUSTOM_DOWNSTREAM_STICKY, root);
}

static GstHarness *
make_lidar_harness(const Scenario *sc)
{
    GstHarness *h = gst_harness_new("g3drender");
    gst_harness_set_src_caps_str(h, "application/x-lidar");
    apply_scenario_props(sc, h);
    return h;
}

static GstHarness *
make_batch_harness(const Scenario *sc)
{
    GstHarness *h = gst_harness_new("g3drender");
    gst_harness_set_src_caps_str(h,
        "multistream/x-analytics-batch(meta:GstAnalyticsBatchMeta)");
    apply_scenario_props(sc, h);
    return h;
}

/* ── buffer builders ──────────────────────────────────────────────────── */

static void
attach_3d_dets(GstBuffer *buf, guint32 n_dets, const G3drDet3D *dets)
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
    }
}

static GstBuffer *
build_lidar_buf(GstHarness *h, const G3drFrame *fr)
{
    gsize      size = (gsize)fr->n_pts * 4 * sizeof(float);
    GstBuffer *buf  = gst_harness_create_buffer(h, size);

    GstMapInfo m;
    gst_buffer_map(buf, &m, GST_MAP_WRITE);
    memcpy(m.data, fr->pts, size);
    gst_buffer_unmap(buf, &m);

    add_lidar_meta(buf, fr->n_pts, 0, GST_CLOCK_TIME_NONE, 0);
    attach_3d_dets(buf, fr->n_3d_dets, fr->dets3d);
    return buf;
}

/*
 * Build a batch buffer that mirrors what gvastreammux produces.
 *
 * Stream layout matches the dump pipeline (dump_meta.md):
 *   sink_0 … sink_(n_cams-1)  →  camera streams (video/x-raw BGR)
 *   sink_n_cams                →  lidar stream   (application/x-lidar)
 *
 * g3drender identifies streams by the caps name in the CAPS sticky event.
 */
static GstBuffer *
build_batch_buf(GstHarness *h, const G3drFrame *fr)
{
    GstBuffer             *batch_buf = gst_buffer_new();
    GstAnalyticsBatchMeta *meta      = gst_buffer_add_analytics_batch_meta(batch_buf);

    guint32 n_streams = fr->n_cams + 1; /* cameras + lidar */
    meta->n_streams = n_streams;
    meta->streams   = g_new0(GstAnalyticsBatchStream, n_streams);

    /* ── camera streams ── */
    for (guint32 i = 0; i < fr->n_cams; i++) {
        GstAnalyticsBatchStream *stream = &meta->streams[i];
        const G3drCam           *cam    = &fr->cams[i];

        stream->index = i;

        /* sub-buffer: BGR pixels laid out with GstVideoInfo stride so that
         * gst_video_frame_map() inside decode_cam_frame() succeeds.
         * GstVideoInfo pads each row to GST_ROUND_UP_4(width*3), which may
         * differ from the raw width*3 stored in the fixture. */
        GstVideoInfo vinfo;
        gst_video_info_set_format(&vinfo, GST_VIDEO_FORMAT_BGR,
                                  cam->width, cam->height);
        gsize      cam_size = GST_VIDEO_INFO_SIZE(&vinfo);
        gint       stride   = GST_VIDEO_INFO_PLANE_STRIDE(&vinfo, 0);
        gint       row_src  = (gint)cam->width * 3;
        GstBuffer *cam_buf  = gst_buffer_new_allocate(NULL, cam_size, NULL);
        GstMapInfo m;
        gst_buffer_map(cam_buf, &m, GST_MAP_WRITE);
        memset(m.data, 0, cam_size);
        if (cam->bgr) {
            for (guint32 r = 0; r < cam->height; r++)
                memcpy(m.data + (gsize)r * stride,
                       cam->bgr  + (gsize)r * row_src,
                       row_src);
        }
        gst_buffer_unmap(cam_buf, &m);

        stream->n_objects = 1;
        stream->objects   = g_new0(GstMiniObject *, 1);
        stream->objects[0] = GST_MINI_OBJECT_CAST(cam_buf);

        /* CAPS sticky event so g3drender can identify this as a video stream */
        GstCaps *caps = gst_caps_new_simple("video/x-raw",
            "format", G_TYPE_STRING, "BGR",
            "width",  G_TYPE_INT,    (gint)cam->width,
            "height", G_TYPE_INT,    (gint)cam->height,
            NULL);
        stream->n_sticky_events = 1;
        stream->sticky_events   = g_new0(GstEvent *, 1);
        stream->sticky_events[0] = gst_event_new_caps(caps);
        gst_caps_unref(caps);
    }

    /* ── lidar stream (last slot) ── */
    GstAnalyticsBatchStream *lidar_stream = &meta->streams[fr->n_cams];
    lidar_stream->index = fr->n_cams;

    gsize      lidar_size = (gsize)fr->n_pts * 4 * sizeof(float);
    GstBuffer *lidar_buf  = gst_harness_create_buffer(h, lidar_size);
    GstMapInfo m;
    gst_buffer_map(lidar_buf, &m, GST_MAP_WRITE);
    memcpy(m.data, fr->pts, lidar_size);
    gst_buffer_unmap(lidar_buf, &m);
    add_lidar_meta(lidar_buf, fr->n_pts, 0, GST_CLOCK_TIME_NONE, 0);
    attach_3d_dets(lidar_buf, fr->n_3d_dets, fr->dets3d);

    lidar_stream->n_objects  = 1;
    lidar_stream->objects    = g_new0(GstMiniObject *, 1);
    lidar_stream->objects[0] = GST_MINI_OBJECT_CAST(lidar_buf);

    GstCaps *lidar_caps = gst_caps_from_string("application/x-lidar");
    lidar_stream->n_sticky_events  = 1;
    lidar_stream->sticky_events    = g_new0(GstEvent *, 1);
    lidar_stream->sticky_events[0] = gst_event_new_caps(lidar_caps);
    gst_caps_unref(lidar_caps);

    return batch_buf;
}

/* ── parameterized test ───────────────────────────────────────────────── */

static void
run_scenario_impl(const Scenario *sc)
{
    gchar *fixture = g_build_filename(TEST_FILES_DIR, sc->raw_file, NULL);
    gchar *out_png = g_build_filename("/tmp", sc->out_name, NULL);

    g_print("\n[%s]\n", sc->subdir);

    G3drFrame fr;
    ck_assert_msg(g3dr_read_frame(fixture, &fr),
                  "Cannot parse fixture: %s", fixture);
    g_free(fixture);

    g_print("  %u pts, %u 3d-dets, %u cams\n", fr.n_pts, fr.n_3d_dets, fr.n_cams);

    GstHarness *h   = NULL;
    GstBuffer  *buf = NULL;

    if (!sc->is_batch) {
        h   = make_lidar_harness(sc);
        buf = build_lidar_buf(h, &fr);
    } else {
        h   = make_batch_harness(sc);
        buf = build_batch_buf(h, &fr);
    }

    g3dr_frame_free(&fr);

    if (sc->calib_file) {
        gchar *calib_path = g_build_filename(TEST_FILES_DIR, sc->calib_file, NULL);
        GstEvent *ev = build_calib_event(calib_path);
        g_free(calib_path);
        ck_assert_msg(ev != NULL, "[%s] failed to build calib event", sc->subdir);
        gst_harness_push_event(h, ev);
    }

    gst_harness_push(h, buf);
    GstBuffer *out = gst_harness_pull(h);
    ck_assert_msg(out != NULL, "g3drender produced no output for %s", sc->subdir);

    gsize out_size = gst_buffer_get_size(out);
    gint  out_h    = sc->height;
    gint  out_w    = (gint)(out_size / (3 * out_h));

    GstMapInfo m;
    gst_buffer_map(out, &m, GST_MAP_READ);

    save_png(out_png, m.data, out_w, out_h);

    gchar *golden_path = g_build_filename(TEST_FILES_DIR, "golden_files",
                                          sc->out_name, NULL);
    gint    gw = 0, gh = 0;
    guint8 *golden = load_png(golden_path, &gw, &gh);

    ck_assert_msg(golden != NULL,
                  "[%s] golden PPM not found: %s\n"
                  "  Run the test once with G3DRENDER_DUMP_GOLDEN=1 to generate it.",
                  sc->subdir, golden_path);
    {
        gchar fail_msg[256] = "";
        gint  max_diff = compare_with_golden(m.data, out_w, out_h,
                                             golden, gw, gh,
                                             fail_msg, sizeof(fail_msg));
        g_free(golden);
        g_print("  golden diff: max=%d (threshold=%d)\n", max_diff, MAX_PIXEL_DIFF);
        ck_assert_msg(max_diff >= 0,
                      "[%s] %s", sc->subdir, fail_msg);
        ck_assert_msg(max_diff <= MAX_PIXEL_DIFF,
                      "[%s] pixel diff exceeds threshold: %s", sc->subdir, fail_msg);
    }
    g_free(golden_path);

    gst_buffer_unmap(out, &m);
    gst_buffer_unref(out);

    g_free(out_png);
    gst_harness_teardown(h);
}

GST_START_TEST(test_render_scenario)
{
    run_scenario_impl(&SCENARIOS[__i__]);
}
GST_END_TEST;

GST_START_TEST(test_cam_proj_scenario)
{
    run_scenario_impl(&CAM_PROJ_SCENARIOS[__i__]);
}
GST_END_TEST;

/* ── suite ────────────────────────────────────────────────────────────── */

static Suite *
g3drender_suite(void)
{
    Suite *s = suite_create("g3drender");

    TCase *tc = tcase_create("fixture");
    tcase_add_loop_test(tc, test_render_scenario, 0, G_N_ELEMENTS(SCENARIOS));
    suite_add_tcase(s, tc);

    TCase *tc_proj = tcase_create("cam-proj");
    tcase_add_loop_test(tc_proj, test_cam_proj_scenario, 0, G_N_ELEMENTS(CAM_PROJ_SCENARIOS));
    suite_add_tcase(s, tc_proj);

    return s;
}

GST_CHECK_MAIN(g3drender);
