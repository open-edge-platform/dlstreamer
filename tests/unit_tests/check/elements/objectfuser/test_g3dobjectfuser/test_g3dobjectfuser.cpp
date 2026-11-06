/*******************************************************************************
 * Copyright (C) 2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

/* g3dobjectfuser settings that change while the element runs: the g3d/calibration event it sends
 * downstream and reloading the calibration, and the association IoU threshold. */

#include <dlstreamer/gst/metadata/g3d_od_mtd.h>
#include <gst/analytics/analytics.h>
#include <gst/analytics/gstanalyticsbatchmeta.h>
#include <gst/check/gstcheck.h>
#include <gst/check/gstharness.h>

#include <glib/gstdio.h>

typedef struct {
    gchar *dir;
    gchar *calibration; /* path the fuser starts with */
    GstHarness *h;
    GstElement *fuser;
    GstBus *bus;
} Fuser;

/* A one-camera calibration file whose only difference is the principal point x. */
static gchar *write_calibration(const gchar *dir, const gchar *name, gfloat cx) {
    gchar *path = g_build_filename(dir, name, NULL);
    gchar *json = g_strdup_printf("{\"cameras\": {\"0\": {"
                                  "\"tr_velo_to_cam\": [0, -1, 0, 0, 0, 0, -1, 0, 1, 0, 0, 0, 0, 0, 0, 1], "
                                  "\"r0_rect\": [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1], "
                                  "\"p2\": [700, 0, %.1f, 0, 0, 700, 200, 0, 0, 0, 1, 0]}}}",
                                  cx);
    ck_assert(g_file_set_contents(path, json, -1, NULL));
    g_free(json);
    return path;
}

/* @properties: more properties for the launch line, set before the element starts. */
static Fuser start_fuser(gfloat cx, const gchar *properties = "") {
    Fuser f = {};
    f.dir = g_dir_make_tmp("g3dobjectfuser-XXXXXX", NULL);
    ck_assert(f.dir != NULL);
    f.calibration = write_calibration(f.dir, "calibration.json", cx);
    gchar *launch = g_strdup_printf("g3dobjectfuser calibration=\"%s\" %s", f.calibration, properties);
    f.h = gst_harness_new_parse(launch);
    g_free(launch);
    gst_harness_set_src_caps_str(f.h, "multistream/x-analytics-batch(meta:GstAnalyticsBatchMeta)");
    f.fuser = gst_harness_find_element(f.h, "g3dobjectfuser");
    ck_assert(f.fuser != NULL);
    f.bus = gst_bus_new();
    gst_element_set_bus(f.h->element, f.bus);
    return f;
}

static void stop_fuser(Fuser *f) {
    gst_element_set_bus(f->h->element, NULL);
    gst_object_unref(f->bus);
    gst_object_unref(f->fuser);
    gst_harness_teardown(f->h);
    GDir *dir = g_dir_open(f->dir, 0, NULL);
    for (const gchar *name; dir && (name = g_dir_read_name(dir)) != NULL;) {
        gchar *path = g_build_filename(f->dir, name, NULL);
        g_unlink(path);
        g_free(path);
    }
    if (dir)
        g_dir_close(dir);
    g_rmdir(f->dir);
    g_free(f->calibration);
    g_free(f->dir);
}

/* Fills a batch stream with @buf (taking ownership) and a caps event for @caps_str. */
static void init_stream(GstAnalyticsBatchStream *stream, guint index, const gchar *caps_str, GstBuffer *buf) {
    stream->index = index;
    stream->n_objects = 1;
    stream->objects = g_new0(GstMiniObject *, 1);
    stream->objects[0] = GST_MINI_OBJECT_CAST(buf);
    GstCaps *caps = gst_caps_from_string(caps_str);
    stream->n_sticky_events = 1;
    stream->sticky_events = g_new0(GstEvent *, 1);
    stream->sticky_events[0] = gst_event_new_caps(caps);
    gst_caps_unref(caps);
}

/* A batch holding only a LiDAR stream: enough for the fuser to send its calibration. */
static void push_lidar_batch(Fuser *f) {
    GstBuffer *batch_buf = gst_buffer_new();
    GstAnalyticsBatchMeta *meta = gst_buffer_add_analytics_batch_meta(batch_buf);
    meta->n_streams = 1;
    meta->streams = g_new0(GstAnalyticsBatchStream, 1);
    init_stream(&meta->streams[0], 1, "application/x-lidar", gst_buffer_new());

    ck_assert_int_eq(gst_harness_push(f->h, batch_buf), GST_FLOW_OK);
    GstBuffer *out = gst_harness_pull(f->h);
    ck_assert(out != NULL);
    gst_buffer_unref(out);
}

/* Pushes a batch with camera 0 and the LiDAR, each with one detection, and returns how many
 * camera detections the fuser linked to a 3D detection. With the calibration from
 * write_calibration(cx = 600), the 2 m cube 10 m ahead projects to about (522, 122) 156 x 156 px;
 * the camera box covers its left half, so the two overlap with an IoU of about 0.5. */
static guint push_fusion_batch(Fuser *f) {
    GstBuffer *cam_buf = gst_buffer_new();
    GstAnalyticsRelationMeta *cam_rmeta = gst_buffer_add_analytics_relation_meta(cam_buf);
    GstAnalyticsODMtd od;
    ck_assert(gst_analytics_relation_meta_add_od_mtd(cam_rmeta, g_quark_from_static_string("car"), 522, 122, 78, 156,
                                                     0.9f, &od));

    GstBuffer *lidar_buf = gst_buffer_new();
    GstAnalyticsRelationMeta *lidar_rmeta = gst_buffer_add_analytics_relation_meta(lidar_buf);
    GstAnalytics3DODMtd od3d;
    ck_assert(gst_analytics_relation_meta_add_3d_od_mtd(lidar_rmeta, 10.f, 0.f, 0.f, 2.f, 2.f, 2.f, 0.f, 0.f, 0.f, 0,
                                                        0.9f, GST_ANALYTICS_3D_SENSOR_LIDAR, &od3d));

    GstBuffer *batch_buf = gst_buffer_new();
    GstAnalyticsBatchMeta *meta = gst_buffer_add_analytics_batch_meta(batch_buf);
    meta->n_streams = 2;
    meta->streams = g_new0(GstAnalyticsBatchStream, 2);
    init_stream(&meta->streams[0], 0, "video/x-raw,format=BGR,width=1242,height=375", cam_buf);
    init_stream(&meta->streams[1], 1, "application/x-lidar", lidar_buf);

    ck_assert_int_eq(gst_harness_push(f->h, batch_buf), GST_FLOW_OK);
    GstBuffer *out = gst_harness_pull(f->h);
    ck_assert(out != NULL);
    GstAnalyticsBatchMeta *out_meta = gst_buffer_get_analytics_batch_meta(out);
    ck_assert(out_meta != NULL && out_meta->n_streams == 2);
    GstAnalyticsRelationMeta *out_rmeta =
        gst_buffer_get_analytics_relation_meta(GST_BUFFER_CAST(out_meta->streams[0].objects[0]));
    ck_assert(out_rmeta != NULL);

    /* The fuser links a camera detection to a 3D one with an IS_PART_OF tracking mtd. */
    guint linked = 0;
    gpointer state = NULL;
    GstAnalyticsODMtd cam_od;
    while (gst_analytics_relation_meta_iterate(out_rmeta, &state, gst_analytics_od_mtd_get_mtd_type(), &cam_od)) {
        gpointer related_state = NULL;
        GstAnalyticsMtd link;
        if (gst_analytics_relation_meta_get_direct_related(out_rmeta, cam_od.id, GST_ANALYTICS_REL_TYPE_IS_PART_OF,
                                                           gst_analytics_tracking_mtd_get_mtd_type(), &related_state,
                                                           &link))
            ++linked;
    }
    gst_buffer_unref(out);
    return linked;
}

/* Principal point x of camera 0 in the next g3d/calibration event sent downstream, or -1 if the
 * fuser sent none since the last call. */
static gfloat pull_calibration_cx(Fuser *f) {
    gfloat cx = -1.f;
    GstEvent *ev;
    while ((ev = gst_harness_try_pull_event(f->h)) != NULL) {
        if (gst_event_has_name(ev, "g3d/calibration") && cx < 0.f) {
            const GValue *cam = gst_structure_get_value(gst_event_get_structure(ev), "camera-0");
            ck_assert(cam != NULL && GST_VALUE_HOLDS_STRUCTURE(cam));
            const GValue *p2 = gst_structure_get_value(gst_value_get_structure(cam), "p2");
            ck_assert(p2 != NULL && gst_value_array_get_size(p2) == 12);
            cx = g_value_get_float(gst_value_array_get_value(p2, 2));
        }
        gst_event_unref(ev);
    }
    return cx;
}

static void expect_calibration_cx(Fuser *f, gfloat expected) {
    const gfloat cx = pull_calibration_cx(f);
    ck_assert_msg(cx == expected, "expected a g3d/calibration event with cx=%.1f, got %.1f (-1 = none)", expected, cx);
}

static gchar *calibration_property(Fuser *f) {
    gchar *path = NULL;
    g_object_get(f->fuser, "calibration", &path, NULL);
    return path;
}

GST_START_TEST(test_calibration_event_sent_once) {
    Fuser f = start_fuser(600.f);
    push_lidar_batch(&f);
    expect_calibration_cx(&f, 600.f);
    push_lidar_batch(&f);
    ck_assert_msg(pull_calibration_cx(&f) < 0.f, "the calibration was sent again without a change");
    stop_fuser(&f);
}
GST_END_TEST;

GST_START_TEST(test_calibration_reload_while_running) {
    Fuser f = start_fuser(600.f);
    push_lidar_batch(&f);
    expect_calibration_cx(&f, 600.f);

    gchar *other = write_calibration(f.dir, "other.json", 640.f);
    g_object_set(f.fuser, "calibration", other, NULL);
    gchar *now = calibration_property(&f);
    ck_assert_str_eq(now, other);
    push_lidar_batch(&f);
    expect_calibration_cx(&f, 640.f);
    ck_assert(gst_bus_pop_filtered(f.bus, GST_MESSAGE_WARNING) == NULL);

    g_free(now);
    g_free(other);
    stop_fuser(&f);
}
GST_END_TEST;

/* Setting the same path again reloads the file, so an edited file is picked up. */
GST_START_TEST(test_calibration_reload_same_path) {
    Fuser f = start_fuser(600.f);
    push_lidar_batch(&f);
    expect_calibration_cx(&f, 600.f);

    g_free(write_calibration(f.dir, "calibration.json", 680.f));
    g_object_set(f.fuser, "calibration", f.calibration, NULL);
    push_lidar_batch(&f);
    expect_calibration_cx(&f, 680.f);

    stop_fuser(&f);
}
GST_END_TEST;

/* A file that does not load is rejected: a warning is posted, and the fuser keeps the current
 * calibration and path. */
GST_START_TEST(test_calibration_reload_failure_keeps_current) {
    Fuser f = start_fuser(600.f);
    push_lidar_batch(&f);
    expect_calibration_cx(&f, 600.f);

    gchar *missing = g_build_filename(f.dir, "missing.json", NULL);
    gchar *broken = g_build_filename(f.dir, "broken.json", NULL);
    ck_assert(g_file_set_contents(broken, "{\"cameras\": {\"0\": {\"p2\": [1, 2, 3]}}}", -1, NULL));
    const gchar *bad_paths[] = {missing, broken};
    for (const gchar *bad : bad_paths) {
        g_object_set(f.fuser, "calibration", bad, NULL);
        GstMessage *warning = gst_bus_pop_filtered(f.bus, GST_MESSAGE_WARNING);
        ck_assert_msg(warning != NULL, "no warning for %s", bad);
        gst_message_unref(warning);
        gchar *now = calibration_property(&f);
        ck_assert_str_eq(now, f.calibration);
        g_free(now);
        push_lidar_batch(&f);
        ck_assert_msg(pull_calibration_cx(&f) < 0.f, "a calibration was sent after %s failed to load", bad);
    }

    g_free(missing);
    g_free(broken);
    stop_fuser(&f);
}
GST_END_TEST;

/* Changing the association IoU threshold while running applies from the next buffer. */
GST_START_TEST(test_iou_threshold_change_while_running) {
    Fuser f = start_fuser(600.f, "assoc-iou-threshold=0.7");
    ck_assert_uint_eq(push_fusion_batch(&f), 0);

    g_object_set(f.fuser, "assoc-iou-threshold", 0.3f, NULL);
    gfloat now = 0.f;
    g_object_get(f.fuser, "assoc-iou-threshold", &now, NULL);
    ck_assert_msg(now == 0.3f, "assoc-iou-threshold reads back %f", now);
    ck_assert_uint_eq(push_fusion_batch(&f), 1);

    g_object_set(f.fuser, "assoc-iou-threshold", 0.7f, NULL);
    ck_assert_uint_eq(push_fusion_batch(&f), 0);

    stop_fuser(&f);
}
GST_END_TEST;

static Suite *g3dobjectfuser_suite(void) {
    Suite *s = suite_create("g3dobjectfuser");
    TCase *tc = tcase_create("calibration");
    tcase_add_test(tc, test_calibration_event_sent_once);
    tcase_add_test(tc, test_calibration_reload_while_running);
    tcase_add_test(tc, test_calibration_reload_same_path);
    tcase_add_test(tc, test_calibration_reload_failure_keeps_current);
    suite_add_tcase(s, tc);
    TCase *association = tcase_create("association");
    tcase_add_test(association, test_iou_threshold_change_while_running);
    suite_add_tcase(s, association);
    return s;
}

GST_CHECK_MAIN(g3dobjectfuser);
