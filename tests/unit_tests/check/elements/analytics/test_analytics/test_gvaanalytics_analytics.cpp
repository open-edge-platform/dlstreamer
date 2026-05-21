/*******************************************************************************
 * Copyright (C) 2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

/**
 * @file test_gvaanalytics_analytics.cpp
 * @brief Behavioral / buffer-level tests for the gvaanalytics GStreamer element.
 *
 * These tests verify:
 *   - Caps negotiation: the element accepts any video format (GST_STATIC_CAPS_ANY)
 *   - Buffer passthrough: pixel data is NOT modified (metadata-only element)
 *   - Zone violation detection: OD center inside a polygon zone → GstAnalyticsZoneMtd added
 *   - Zone miss: OD center outside zone → no GstAnalyticsZoneMtd added
 *   - Tripwire crossing detection: two-frame sequence crossing a line → GstAnalyticsTripwireMtd added
 *   - draw-zones=false suppresses WatermarkDrawMeta
 */

#include "test_common.h"
#include "test_utils.h"
#include <cstring>

#include <gst/analytics/analytics-meta-prelude.h>
#include <gst/analytics/gstanalyticsmeta.h>
#include <gst/analytics/gstanalyticsobjectdetectionmtd.h>
#include <gst/analytics/gstanalyticsobjecttrackingmtd.h>
#include <gst/gst.h>
#include <gst/video/gstvideometa.h>

#include "dlstreamer/gst/metadata/gva_tripwire_meta.h"
#include "dlstreamer/gst/metadata/gva_zone_meta.h"
#include "dlstreamer/gst/metadata/watermark_draw_meta.h"

/* ---------- element name under test ---------- */
constexpr char elem_name[] = "gvaanalytics";

#define ANALYTICS_BGR_CAPS GST_VIDEO_CAPS_MAKE("BGR")

static Resolution test_resolution = {320, 240};

/* A simple square zone covering the centre of a 320x240 frame:
   (100,80)-(220,160) — top-left to bottom-right. */
static const char *zone_polygon_json = "[{\"id\":\"zone_center\",\"type\":\"polygon\","
                                       "\"points\":[{\"x\":100,\"y\":80},{\"x\":220,\"y\":80},"
                                       "{\"x\":220,\"y\":160},{\"x\":100,\"y\":160}]}]";

/* A vertical tripwire at x=160, spanning the full height */
static const char *tripwire_vertical_json = "[{\"id\":\"wire_center\","
                                            "\"points\":[{\"x\":160,\"y\":0},{\"x\":160,\"y\":240}]}]";

/* Pad templates using a concrete video format (required for caps negotiation in test harness) */
static GstStaticPadTemplate src_any =
    GST_STATIC_PAD_TEMPLATE("src", GST_PAD_SRC, GST_PAD_ALWAYS, GST_STATIC_CAPS(ANALYTICS_BGR_CAPS));
static GstStaticPadTemplate sink_any =
    GST_STATIC_PAD_TEMPLATE("sink", GST_PAD_SINK, GST_PAD_ALWAYS, GST_STATIC_CAPS(ANALYTICS_BGR_CAPS));

/* ========================================================================= */
/*  Helpers                                                                  */
/* ========================================================================= */

/**
 * Attach an OD bounding box to @buffer's analytics relation metadata.
 * Returns the created od_mtd.id so the caller can relate tracking to it.
 */
static guint attach_od(GstBuffer *buffer, gint x, gint y, gint w, gint h) {
    GstAnalyticsRelationMeta *rmeta = gst_buffer_add_analytics_relation_meta(buffer);
    ck_assert_msg(rmeta != NULL, "Failed to add analytics relation meta");

    GstAnalyticsODMtd od_mtd;
    gboolean ok =
        gst_analytics_relation_meta_add_od_mtd(rmeta, g_quark_from_string("object"), x, y, w, h, 0.9f, &od_mtd);
    ck_assert_msg(ok, "Failed to add OD metadata");
    return od_mtd.id;
}

/**
 * Attach a tracking metadata entry related to od_id.
 */
static void attach_tracking(GstBuffer *buffer, guint od_id, guint64 tracking_id) {
    GstAnalyticsRelationMeta *rmeta =
        (GstAnalyticsRelationMeta *)gst_buffer_get_meta(buffer, gst_analytics_relation_meta_api_get_type());
    ck_assert_msg(rmeta != NULL, "Buffer has no analytics relation meta (call attach_od first)");

    GstAnalyticsTrackingMtd trk_mtd;
    gboolean ok = gst_analytics_relation_meta_add_tracking_mtd(rmeta, tracking_id, GST_CLOCK_TIME_NONE, &trk_mtd);
    ck_assert_msg(ok, "Failed to add tracking metadata");

    /* Relate OD → tracking */
    ok = gst_analytics_relation_meta_set_relation(rmeta, GST_ANALYTICS_REL_TYPE_RELATE_TO, od_id, trk_mtd.id);
    ck_assert_msg(ok, "Failed to set OD→tracking relation");
}

/* ========================================================================= */
/*  Caps negotiation                                                         */
/* ========================================================================= */

struct FormatEntry {
    const char *name;
    const char *caps_string;
};

static const FormatEntry supported_formats[] = {
    {"BGR", GST_VIDEO_CAPS_MAKE("BGR")},   {"NV12", GST_VIDEO_CAPS_MAKE("NV12")}, {"BGRA", GST_VIDEO_CAPS_MAKE("BGRA")},
    {"RGBA", GST_VIDEO_CAPS_MAKE("RGBA")}, {"I420", GST_VIDEO_CAPS_MAKE("I420")},
};
static const int NUM_FORMATS = (int)(sizeof(supported_formats) / sizeof(supported_formats[0]));

static GstStaticPadTemplate make_src(const char *caps_str) {
    GstStaticPadTemplate t = GST_STATIC_PAD_TEMPLATE("src", GST_PAD_SRC, GST_PAD_ALWAYS, GST_STATIC_CAPS(caps_str));
    return t;
}
static GstStaticPadTemplate make_sink(const char *caps_str) {
    GstStaticPadTemplate t = GST_STATIC_PAD_TEMPLATE("sink", GST_PAD_SINK, GST_PAD_ALWAYS, GST_STATIC_CAPS(caps_str));
    return t;
}

/**
 * gvaanalytics uses GST_STATIC_CAPS_ANY, so every video format must pass
 * caps negotiation without error.
 */
GST_START_TEST(test_caps_format_accepted) {
    const FormatEntry &fmt = supported_formats[__i__];
    g_print("Starting test: test_caps_format_accepted[%s]\n", fmt.name);

    GstStaticPadTemplate src = make_src(fmt.caps_string);
    GstStaticPadTemplate sink = make_sink(fmt.caps_string);

    run_test(elem_name, fmt.caps_string, test_resolution, &src, &sink, NULL, NULL, NULL, NULL);
}
GST_END_TEST;

/* ========================================================================= */
/*  Buffer passthrough (pixel data unchanged)                                */
/* ========================================================================= */

static void fill_pattern(GstBuffer *buf, gpointer /*user_data*/) {
    GstMapInfo info;
    ck_assert(gst_buffer_map(buf, &info, GST_MAP_WRITE));
    memset(info.data, 0xAB, info.size);
    gst_buffer_unmap(buf, &info);
}

static void verify_unchanged(GstBuffer *buf, gpointer /*user_data*/) {
    GstMapInfo info;
    ck_assert(gst_buffer_map(buf, &info, GST_MAP_READ));
    for (gsize i = 0; i < info.size; i++) {
        ck_assert_msg(info.data[i] == 0xAB, "Pixel data was modified at offset %zu (expected 0xAB, got 0x%02X)", i,
                      info.data[i]);
    }
    gst_buffer_unmap(buf, &info);
}

/**
 * With no OD metadata the element must not touch pixel data.
 */
GST_START_TEST(test_buffer_passthrough_no_metadata) {
    g_print("Starting test: test_buffer_passthrough_no_metadata\n");
    run_test(elem_name, ANALYTICS_BGR_CAPS, test_resolution, &src_any, &sink_any, fill_pattern, verify_unchanged, NULL,
             NULL);
}
GST_END_TEST;

/* ========================================================================= */
/*  Zone violation detection                                                 */
/* ========================================================================= */

/**
 * OD center (160, 120) is inside the square zone (100-220, 80-160).
 * After transform_ip we expect at least one GstAnalyticsZoneMtd in the buffer.
 */

static void setup_od_inside_zone(GstBuffer *buf, gpointer /*user_data*/) {
    /* bbox (110,90,100,60): center = (160, 120) → inside zone */
    attach_od(buf, 110, 90, 100, 60);
}

static void check_zone_meta_present(GstBuffer *buf, gpointer /*user_data*/) {
    GstAnalyticsRelationMeta *rmeta =
        (GstAnalyticsRelationMeta *)gst_buffer_get_meta(buf, gst_analytics_relation_meta_api_get_type());
    ck_assert_msg(rmeta != NULL, "No analytics relation meta on output buffer");

    GstAnalyticsZoneMtd zone_mtd;
    gpointer state = NULL;
    gboolean found = gst_analytics_relation_meta_iterate(rmeta, &state, gst_analytics_zone_mtd_get_mtd_type(),
                                                         (GstAnalyticsMtd *)&zone_mtd);
    ck_assert_msg(found, "Expected GstAnalyticsZoneMtd to be present but found none");

    gchar *zone_id = NULL;
    gst_analytics_zone_mtd_get_info(&zone_mtd, &zone_id);
    ck_assert_msg(zone_id != NULL && strcmp(zone_id, "zone_center") == 0,
                  "Zone ID mismatch: expected 'zone_center', got '%s'", zone_id ? zone_id : "(null)");
    g_free(zone_id);
}

GST_START_TEST(test_zone_violation_detected) {
    g_print("Starting test: test_zone_violation_detected\n");
    run_test(elem_name, ANALYTICS_BGR_CAPS, test_resolution, &src_any, &sink_any, setup_od_inside_zone,
             check_zone_meta_present, NULL, "zones", zone_polygon_json, NULL);
}
GST_END_TEST;

/* ========================================================================= */
/*  Zone miss (OD outside zone → no ZoneMtd)                                */
/* ========================================================================= */

/**
 * OD center (10, 10) is clearly outside the square zone.
 * No GstAnalyticsZoneMtd should appear.
 */

static void setup_od_outside_zone(GstBuffer *buf, gpointer /*user_data*/) {
    /* bbox (0,0,20,20): center = (10, 10) → outside zone */
    attach_od(buf, 0, 0, 20, 20);
}

static void check_zone_meta_absent(GstBuffer *buf, gpointer /*user_data*/) {
    GstAnalyticsRelationMeta *rmeta =
        (GstAnalyticsRelationMeta *)gst_buffer_get_meta(buf, gst_analytics_relation_meta_api_get_type());
    /* There may still be an rmeta (from the OD we attached), but zone type should not be present */
    if (rmeta == NULL)
        return; /* no meta at all → certainly no zone */

    GstAnalyticsZoneMtd zone_mtd;
    gpointer state = NULL;
    gboolean found = gst_analytics_relation_meta_iterate(rmeta, &state, gst_analytics_zone_mtd_get_mtd_type(),
                                                         (GstAnalyticsMtd *)&zone_mtd);
    ck_assert_msg(!found, "Unexpected GstAnalyticsZoneMtd on buffer whose OD center is outside all zones");
}

GST_START_TEST(test_zone_no_violation_outside) {
    g_print("Starting test: test_zone_no_violation_outside\n");
    run_test(elem_name, ANALYTICS_BGR_CAPS, test_resolution, &src_any, &sink_any, setup_od_outside_zone,
             check_zone_meta_absent, NULL, "zones", zone_polygon_json, NULL);
}
GST_END_TEST;

/* ========================================================================= */
/*  draw-zones=false suppresses WatermarkDrawMeta                            */
/* ========================================================================= */

static void check_no_draw_meta(GstBuffer *buf, gpointer /*user_data*/) {
    const GstMetaInfo *draw_info = watermark_draw_meta_get_info();
    GstMeta *meta = NULL;
    gpointer state = NULL;
    while ((meta = gst_buffer_iterate_meta(buf, &state)) != NULL) {
        ck_assert_msg(meta->info != draw_info, "WatermarkDrawMeta found on buffer but draw-zones was set to FALSE");
    }
}

GST_START_TEST(test_draw_zones_false_no_draw_meta) {
    g_print("Starting test: test_draw_zones_false_no_draw_meta\n");
    run_test(elem_name, ANALYTICS_BGR_CAPS, test_resolution, &src_any, &sink_any, setup_od_inside_zone,
             check_no_draw_meta, NULL, "zones", zone_polygon_json, "draw-zones", FALSE, NULL);
}
GST_END_TEST;

/* ========================================================================= */
/*  draw-zones=true attaches WatermarkDrawMeta                               */
/* ========================================================================= */

static void check_draw_meta_present(GstBuffer *buf, gpointer /*user_data*/) {
    const GstMetaInfo *draw_info = watermark_draw_meta_get_info();
    GstMeta *meta = NULL;
    gpointer state = NULL;
    gboolean found = FALSE;
    while ((meta = gst_buffer_iterate_meta(buf, &state)) != NULL) {
        if (meta->info == draw_info) {
            found = TRUE;
            break;
        }
    }
    ck_assert_msg(found, "Expected WatermarkDrawMeta on buffer when draw-zones=true and a zone is configured");
}

GST_START_TEST(test_draw_zones_true_has_draw_meta) {
    g_print("Starting test: test_draw_zones_true_has_draw_meta\n");
    run_test(elem_name, ANALYTICS_BGR_CAPS, test_resolution, &src_any, &sink_any, fill_pattern, check_draw_meta_present,
             NULL, "zones", zone_polygon_json, "draw-zones", TRUE, NULL);
}
GST_END_TEST;

/* ========================================================================= */
/*  Tripwire crossing detection (two-frame simulation)                       */
/*                                                                           */
/*  gvaanalytics stores tracking state across calls to transform_ip.         */
/*  We cannot easily push two independent buffers through run_test, so we    */
/*  drive the element manually using GstCheck harness helpers.               */
/* ========================================================================= */

/**
 * Push @buffer through a freshly created gvaanalytics element configured with
 * a vertical tripwire at x=160.  The element is kept alive between calls so
 * tracking state persists.  Returns the output buffer (caller must unref).
 */
static GstBuffer *push_buffer_through(GstElement *element, GstPad *src_pad, GstPad *sink_pad, GstBuffer *buffer) {
    /* clear collected buffers list */
    g_list_free_full(buffers, (GDestroyNotify)gst_buffer_unref);
    buffers = NULL;

    fail_unless(gst_pad_push(src_pad, gst_buffer_ref(buffer)) == GST_FLOW_OK);

    ck_assert_msg(buffers != NULL, "No output buffer collected");
    GstBuffer *out = (GstBuffer *)buffers->data;
    /* transfer ownership: remove from list without unreffing */
    buffers = g_list_delete_link(buffers, buffers);
    return out;
}

GST_START_TEST(test_tripwire_crossing_detected) {
    g_print("Starting test: test_tripwire_crossing_detected\n");

    /* ---- element setup ---- */
    GstElement *element = gst_check_setup_element(elem_name);
    ck_assert(element != NULL);
    g_object_set(G_OBJECT(element), "tripwires", tripwire_vertical_json, NULL);
    g_object_set(G_OBJECT(element), "draw-tripwires", FALSE, NULL);

    GstStaticPadTemplate src_t =
        GST_STATIC_PAD_TEMPLATE("src", GST_PAD_SRC, GST_PAD_ALWAYS, GST_STATIC_CAPS(ANALYTICS_BGR_CAPS));
    GstStaticPadTemplate sink_t =
        GST_STATIC_PAD_TEMPLATE("sink", GST_PAD_SINK, GST_PAD_ALWAYS, GST_STATIC_CAPS(ANALYTICS_BGR_CAPS));

    GstPad *src_pad = gst_check_setup_src_pad(element, &src_t);
    GstPad *sink_pad = gst_check_setup_sink_pad(element, &sink_t);

    gst_pad_set_active(src_pad, TRUE);
    gst_pad_set_active(sink_pad, TRUE);
    ck_assert(gst_element_set_state(element, GST_STATE_PLAYING) != GST_STATE_CHANGE_FAILURE);

    GstCaps *caps =
        gst_caps_new_simple("video/x-raw", "format", G_TYPE_STRING, "BGR", "width", G_TYPE_INT, test_resolution.width,
                            "height", G_TYPE_INT, test_resolution.height, "framerate", GST_TYPE_FRACTION, 25, 1, NULL);
    gst_check_setup_events(src_pad, element, caps, GST_FORMAT_TIME);
    gst_caps_unref(caps);

    /* ---- frame 1: object at x=100 (left of tripwire at x=160) ---- */
    /* bbox (50, 90, 100, 60) → center (100, 120) */
    const gsize buf_size = (gsize)(test_resolution.width * test_resolution.height * 3);
    GstBuffer *buf1 = gst_buffer_new_allocate(NULL, buf_size, NULL);
    guint od1_id = attach_od(buf1, 50, 90, 100, 60);
    attach_tracking(buf1, od1_id, 42ULL);

    GstBuffer *out1 = push_buffer_through(element, src_pad, sink_pad, buf1);
    gst_buffer_unref(buf1);
    gst_buffer_unref(out1);

    /* ---- frame 2: same object moved to x=200 (right of tripwire) ---- */
    /* bbox (150, 90, 100, 60) → center (200, 120) — crosses x=160 */
    GstBuffer *buf2 = gst_buffer_new_allocate(NULL, buf_size, NULL);
    guint od2_id = attach_od(buf2, 150, 90, 100, 60);
    attach_tracking(buf2, od2_id, 42ULL);

    GstBuffer *out2 = push_buffer_through(element, src_pad, sink_pad, buf2);
    gst_buffer_unref(buf2);

    /* ---- verify tripwire metadata is on output of frame 2 ---- */
    GstAnalyticsRelationMeta *rmeta =
        (GstAnalyticsRelationMeta *)gst_buffer_get_meta(out2, gst_analytics_relation_meta_api_get_type());
    ck_assert_msg(rmeta != NULL, "No analytics relation meta on frame-2 output buffer");

    GstAnalyticsTripwireMtd tw_mtd;
    gpointer state = NULL;
    gboolean found = gst_analytics_relation_meta_iterate(rmeta, &state, gst_analytics_tripwire_mtd_get_mtd_type(),
                                                         (GstAnalyticsMtd *)&tw_mtd);
    ck_assert_msg(found, "Expected GstAnalyticsTripwireMtd on frame-2 output but found none");

    gchar *tw_id = NULL;
    gint direction = 0;
    gst_analytics_tripwire_mtd_get_info(&tw_mtd, &tw_id, &direction);
    ck_assert_msg(tw_id != NULL && strcmp(tw_id, "wire_center") == 0,
                  "Tripwire ID mismatch: expected 'wire_center', got '%s'", tw_id ? tw_id : "(null)");
    ck_assert_msg(direction != 0, "Expected non-zero direction for tripwire crossing");
    g_free(tw_id);

    gst_buffer_unref(out2);

    /* ---- teardown ---- */
    gst_element_set_state(element, GST_STATE_NULL);
    gst_check_teardown_src_pad(element);
    gst_check_teardown_sink_pad(element);
    gst_check_teardown_element(element);
}
GST_END_TEST;

/* ========================================================================= */
/*  No tripwire crossing when object stays on same side                      */
/* ========================================================================= */

GST_START_TEST(test_tripwire_no_crossing_same_side) {
    g_print("Starting test: test_tripwire_no_crossing_same_side\n");

    GstElement *element = gst_check_setup_element(elem_name);
    ck_assert(element != NULL);
    g_object_set(G_OBJECT(element), "tripwires", tripwire_vertical_json, NULL);
    g_object_set(G_OBJECT(element), "draw-tripwires", FALSE, NULL);

    GstStaticPadTemplate src_t =
        GST_STATIC_PAD_TEMPLATE("src", GST_PAD_SRC, GST_PAD_ALWAYS, GST_STATIC_CAPS(ANALYTICS_BGR_CAPS));
    GstStaticPadTemplate sink_t =
        GST_STATIC_PAD_TEMPLATE("sink", GST_PAD_SINK, GST_PAD_ALWAYS, GST_STATIC_CAPS(ANALYTICS_BGR_CAPS));

    GstPad *src_pad = gst_check_setup_src_pad(element, &src_t);
    GstPad *sink_pad = gst_check_setup_sink_pad(element, &sink_t);

    gst_pad_set_active(src_pad, TRUE);
    gst_pad_set_active(sink_pad, TRUE);
    ck_assert(gst_element_set_state(element, GST_STATE_PLAYING) != GST_STATE_CHANGE_FAILURE);

    GstCaps *caps =
        gst_caps_new_simple("video/x-raw", "format", G_TYPE_STRING, "BGR", "width", G_TYPE_INT, test_resolution.width,
                            "height", G_TYPE_INT, test_resolution.height, "framerate", GST_TYPE_FRACTION, 25, 1, NULL);
    gst_check_setup_events(src_pad, element, caps, GST_FORMAT_TIME);
    gst_caps_unref(caps);

    const gsize buf_size = (gsize)(test_resolution.width * test_resolution.height * 3);

    /* Frame 1: center (50, 120) — left of tripwire */
    GstBuffer *buf1 = gst_buffer_new_allocate(NULL, buf_size, NULL);
    guint od1_id = attach_od(buf1, 0, 90, 100, 60);
    attach_tracking(buf1, od1_id, 99ULL);
    GstBuffer *out1 = push_buffer_through(element, src_pad, sink_pad, buf1);
    gst_buffer_unref(buf1);
    gst_buffer_unref(out1);

    /* Frame 2: center (100, 120) — still left of tripwire */
    GstBuffer *buf2 = gst_buffer_new_allocate(NULL, buf_size, NULL);
    guint od2_id = attach_od(buf2, 50, 90, 100, 60);
    attach_tracking(buf2, od2_id, 99ULL);
    GstBuffer *out2 = push_buffer_through(element, src_pad, sink_pad, buf2);
    gst_buffer_unref(buf2);

    /* No tripwire meta expected */
    GstAnalyticsRelationMeta *rmeta =
        (GstAnalyticsRelationMeta *)gst_buffer_get_meta(out2, gst_analytics_relation_meta_api_get_type());
    if (rmeta) {
        GstAnalyticsTripwireMtd tw_mtd;
        gpointer state = NULL;
        gboolean found = gst_analytics_relation_meta_iterate(rmeta, &state, gst_analytics_tripwire_mtd_get_mtd_type(),
                                                             (GstAnalyticsMtd *)&tw_mtd);
        ck_assert_msg(!found, "Unexpected GstAnalyticsTripwireMtd: object never crossed the tripwire");
    }

    gst_buffer_unref(out2);
    gst_element_set_state(element, GST_STATE_NULL);
    gst_check_teardown_src_pad(element);
    gst_check_teardown_sink_pad(element);
    gst_check_teardown_element(element);
}
GST_END_TEST;

/* ========================================================================= */
/*  Suite assembly                                                            */
/* ========================================================================= */

static Suite *gvaanalytics_analytics_suite(void) {
    Suite *s = suite_create("gvaanalytics_analytics");

    /* --- caps negotiation --- */
    TCase *tc_caps = tcase_create("caps_negotiation");
    tcase_add_loop_test(tc_caps, test_caps_format_accepted, 0, NUM_FORMATS);
    suite_add_tcase(s, tc_caps);

    /* --- buffer passthrough --- */
    TCase *tc_passthrough = tcase_create("buffer_passthrough");
    tcase_add_test(tc_passthrough, test_buffer_passthrough_no_metadata);
    suite_add_tcase(s, tc_passthrough);

    /* --- zone detection --- */
    TCase *tc_zone = tcase_create("zone_detection");
    tcase_add_test(tc_zone, test_zone_violation_detected);
    tcase_add_test(tc_zone, test_zone_no_violation_outside);
    suite_add_tcase(s, tc_zone);

    /* --- draw metadata --- */
    TCase *tc_draw = tcase_create("draw_metadata");
    tcase_add_test(tc_draw, test_draw_zones_false_no_draw_meta);
    tcase_add_test(tc_draw, test_draw_zones_true_has_draw_meta);
    suite_add_tcase(s, tc_draw);

    /* --- tripwire detection --- */
    TCase *tc_tripwire = tcase_create("tripwire_detection");
    tcase_add_test(tc_tripwire, test_tripwire_crossing_detected);
    tcase_add_test(tc_tripwire, test_tripwire_no_crossing_same_side);
    suite_add_tcase(s, tc_tripwire);

    return s;
}

GST_CHECK_MAIN(gvaanalytics_analytics);
