/*******************************************************************************
 * Copyright (C) 2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#include "test_common.h"
#include "test_utils.h"
#include <cstring>
#include <dlstreamer/gst/videoanalytics/tensor.h>
#include <glib/gstdio.h>
#include <gst/analytics/analytics-meta-prelude.h>
#include <gst/analytics/gstanalyticsmeta.h>
#include <gst/analytics/gstanalyticsobjectdetectionmtd.h>
#include <gst/analytics/gstanalyticsobjecttrackingmtd.h>
#include <gst/video/gstvideometa.h>

/* ---------- element name under test ---------- */
constexpr char impl_name[] = "gvawatermarkimpl";

#define WATERMARK_BGR_CAPS GST_VIDEO_CAPS_MAKE("BGR")

static GstStaticPadTemplate srctemplate =
    GST_STATIC_PAD_TEMPLATE("src", GST_PAD_SRC, GST_PAD_ALWAYS, GST_STATIC_CAPS(WATERMARK_BGR_CAPS));
static GstStaticPadTemplate sinktemplate =
    GST_STATIC_PAD_TEMPLATE("sink", GST_PAD_SINK, GST_PAD_ALWAYS, GST_STATIC_CAPS(WATERMARK_BGR_CAPS));

static Resolution test_resolution = {320, 240};

/* ========================================================================= */
/*  Helpers: attach ROI metadata and count modified pixels                   */
/* ========================================================================= */

static void add_roi_to_buffer(GstBuffer *buffer, const char *label, guint x, guint y, guint w, guint h) {
    GstAnalyticsRelationMeta *relation_meta = gst_buffer_add_analytics_relation_meta(buffer);
    GstAnalyticsODMtd od_mtd;
    gst_analytics_relation_meta_add_od_mtd(relation_meta, g_quark_from_string(label), (gint)x, (gint)y, (gint)w,
                                           (gint)h, 0.95f, &od_mtd);
    GstVideoRegionOfInterestMeta *roi_meta = gst_buffer_add_video_region_of_interest_meta(buffer, label, x, y, w, h);
    roi_meta->id = od_mtd.id;
}

/* Context passed through user_data for behavioral tests */
struct RenderStats {
    const char *label; /* ROI label (NULL → "person") */
    gsize modified;    /* filled by check callback */
};

static void setup_buffer_with_roi(GstBuffer *buffer, gpointer user_data) {
    GstMapInfo info;
    ck_assert(gst_buffer_map(buffer, &info, GST_MAP_WRITE));
    memset(info.data, 0x00, info.size);
    gst_buffer_unmap(buffer, &info);

    const char *label = "person";
    if (user_data) {
        RenderStats *stats = (RenderStats *)user_data;
        if (stats->label)
            label = stats->label;
    }
    add_roi_to_buffer(buffer, label, 50, 50, 100, 100);
}

/* Count how many bytes in the buffer differ from 0x00 (black).
   Uses branch-free increment to avoid branch mispredictions on large buffers. */
static gsize count_modified_bytes(GstBuffer *buffer) {
    GstMapInfo info;
    ck_assert(gst_buffer_map(buffer, &info, GST_MAP_READ));
    gsize count = 0;
    for (gsize i = 0; i < info.size; i++) {
        count += (info.data[i] != 0x00);
    }
    gst_buffer_unmap(buffer, &info);
    return count;
}

static void record_modified_bytes(GstBuffer *buffer, gpointer user_data) {
    RenderStats *stats = (RenderStats *)user_data;
    stats->modified = count_modified_bytes(buffer);
}

/* ========================================================================= */
/*  Helpers: non-square segmentation mask rendering (regression coverage for */
/*  commit c0bf460a / PR #1047, which swapped cv::Size(width,height) args).  */
/*                                                                           */
/*  A square mask cannot reveal a transposed width/height read, so these     */
/*  tests use a non-square ROI and an oblong mask (5 wide x 50 tall) whose    */
/*  "on" region is a full-width band in the bottom 3 mask-rows. A width/     */
/*  height swap turns a 5x50 buffer into a 50x5 reinterpretation - since the  */
/*  row length changes so drastically (5 vs 50), the same flat bytes decode   */
/*  into a band confined to the right ~30% of the width instead of the full  */
/*  width, which is easy to tell apart with a couple of point samples.       */
/* ========================================================================= */

constexpr guint SEG_ROI_X = 10, SEG_ROI_Y = 10, SEG_ROI_W = 30, SEG_ROI_H = 60;
constexpr guint SEG_MASK_W = 5, SEG_MASK_H = 50; // deliberately oblong, decoupled from ROI size
constexpr guint SEG_MASK_ON_ROWS = 3;            // bottom-most rows of the mask are "on"

/* BGR is packed 3 bytes/pixel; SEG_FRAME_W chosen so width*3 needs no row padding. */
constexpr guint SEG_FRAME_W = 100, SEG_FRAME_H = 100;
static Resolution seg_test_resolution = {(gint)SEG_FRAME_W, (gint)SEG_FRAME_H};

static bool pixel_is_colored(GstBuffer *buffer, guint x, guint y) {
    GstMapInfo info;
    ck_assert(gst_buffer_map(buffer, &info, GST_MAP_READ));
    gsize stride = SEG_FRAME_W * 3;
    gsize offset = y * stride + x * 3;
    ck_assert(offset + 2 < info.size);
    bool colored = info.data[offset] != 0 || info.data[offset + 1] != 0 || info.data[offset + 2] != 0;
    gst_buffer_unmap(buffer, &info);
    return colored;
}

struct SegPixelStats {
    bool bottom_left_colored; // ON only if width/height were read correctly (full-width band)
    bool top_center_colored;  // must stay OFF - confirms the mask isn't just filling the whole box
};

static void record_seg_pixel_stats(GstBuffer *buffer, gpointer user_data) {
    SegPixelStats *stats = (SegPixelStats *)user_data;
    /* bottom-left corner of the ROI, well inside the "on" band's full-width extent */
    stats->bottom_left_colored = pixel_is_colored(buffer, SEG_ROI_X + 3, SEG_ROI_Y + SEG_ROI_H - 3);
    /* top-center of the ROI: never "on" in either the correct or swapped interpretation */
    stats->top_center_colored = pixel_is_colored(buffer, SEG_ROI_X + SEG_ROI_W / 2, SEG_ROI_Y + 3);
}

static void add_roi_only(GstBuffer *buffer, guint x, guint y, guint w, guint h) {
    GstAnalyticsRelationMeta *relation_meta = gst_buffer_add_analytics_relation_meta(buffer);
    GstAnalyticsODMtd od_mtd;
    gst_analytics_relation_meta_add_od_mtd(relation_meta, g_quark_from_string("object"), (gint)x, (gint)y, (gint)w,
                                           (gint)h, 0.95f, &od_mtd);
    GstVideoRegionOfInterestMeta *roi_meta = gst_buffer_add_video_region_of_interest_meta(buffer, "object", x, y, w, h);
    roi_meta->id = od_mtd.id;

    return;
}

/* bottom SEG_MASK_ON_ROWS rows of the mask (all columns) are "on", rest is "off" */
static void setup_buffer_with_instance_mask(GstBuffer *buffer, gpointer /*user_data*/) {
    GstMapInfo info;
    ck_assert(gst_buffer_map(buffer, &info, GST_MAP_WRITE));
    memset(info.data, 0x00, info.size);
    gst_buffer_unmap(buffer, &info);

    add_roi_only(buffer, SEG_ROI_X, SEG_ROI_Y, SEG_ROI_W, SEG_ROI_H);

    GstVideoRegionOfInterestMeta *roi_meta = gst_buffer_get_video_region_of_interest_meta_id(buffer, 0);
    ck_assert(roi_meta != nullptr);

    std::vector<float> mask_data(SEG_MASK_W * SEG_MASK_H);
    for (guint r = 0; r < SEG_MASK_H; r++)
        for (guint c = 0; c < SEG_MASK_W; c++)
            mask_data[r * SEG_MASK_W + c] = (r >= SEG_MASK_H - SEG_MASK_ON_ROWS) ? 1.0f : 0.0f;

    GstStructure *s = gst_structure_new_empty("instance_mask");
    GVA::Tensor tensor(s);
    tensor.set_format(GVA::TENSOR_FORMAT_INSTANCE_SEGMENTATION);
    tensor.set_precision(GVA::Tensor::Precision::FP32);
    tensor.set_dims({SEG_MASK_W, SEG_MASK_H}); // [W, H], per the fixed convention
    tensor.set_data(mask_data.data(), mask_data.size() * sizeof(float));

    gst_video_region_of_interest_meta_add_param(roi_meta, s);
}

static void setup_buffer_with_semantic_mask(GstBuffer *buffer, gpointer /*user_data*/) {
    GstMapInfo info;
    ck_assert(gst_buffer_map(buffer, &info, GST_MAP_WRITE));
    memset(info.data, 0x00, info.size);
    gst_buffer_unmap(buffer, &info);

    add_roi_only(buffer, SEG_ROI_X, SEG_ROI_Y, SEG_ROI_W, SEG_ROI_H);

    GstVideoRegionOfInterestMeta *roi_meta = gst_buffer_get_video_region_of_interest_meta_id(buffer, 0);
    ck_assert(roi_meta != nullptr);

    std::vector<int64_t> mask_data(SEG_MASK_W * SEG_MASK_H);
    for (guint r = 0; r < SEG_MASK_H; r++)
        for (guint c = 0; c < SEG_MASK_W; c++)
            mask_data[r * SEG_MASK_W + c] = (r >= SEG_MASK_H - SEG_MASK_ON_ROWS) ? 1 : 0; // class 1 != background

    GstStructure *s = gst_structure_new_empty("semantic_mask");
    GVA::Tensor tensor(s);
    tensor.set_format(GVA::TENSOR_FORMAT_SEMANTIC_SEGMENTATION);
    tensor.set_precision(GVA::Tensor::Precision::I64);
    tensor.set_dims({1u, SEG_MASK_H, SEG_MASK_W}); // [1, H, W], per the fixed convention
    tensor.set_data(mask_data.data(), mask_data.size() * sizeof(int64_t));

    gst_video_region_of_interest_meta_add_param(roi_meta, s);
}

/* ========================================================================= */
/*  Data-driven valid config acceptance tests                                */
/*                                                                           */
/*  Each config string is pushed through the element via run_test. If the    */
/*  config is valid, no crash or error occurs.                               */
/* ========================================================================= */

static const char *valid_config_strings[] = {
    "show-labels=false",
    "font-scale=1.0",
    "thickness=5",
    "color-idx=0",
    "font-type=simplex",
    "draw-txt-bg=false",
    "show-roi=person:car",
    "hide-roi=background",
    "show-labels=true,font-scale=0.8,thickness=3,color-idx=1,font-type=complex,draw-txt-bg=true",
    "text-x=10,text-y=50",
};
static const int NUM_VALID_CONFIGS = sizeof(valid_config_strings) / sizeof(valid_config_strings[0]);

GST_START_TEST(test_config_accepted) {
    const char *cfg = valid_config_strings[__i__];
    g_print("Starting test: test_config_accepted[%s]\n", cfg);
    run_test(impl_name, WATERMARK_BGR_CAPS, test_resolution, &srctemplate, &sinktemplate, NULL, NULL, NULL, "displ-cfg",
             cfg, NULL);
}
GST_END_TEST;

/* ========================================================================= */
/*  Test: show-labels affects rendering output                               */
/*                                                                           */
/*  With ROI metadata attached:                                              */
/*  - show-labels=true: bounding box + text label drawn (more modified px)   */
/*  - show-labels=false: only bounding box drawn (fewer modified px)         */
/* ========================================================================= */

GST_START_TEST(test_show_labels_true_renders_more_than_false) {
    g_print("Starting test: test_show_labels_true_renders_more_than_false\n");

    RenderStats with_labels = {NULL, 0};
    RenderStats without_labels = {NULL, 0};

    /* Run with labels ON (default) */
    run_test(impl_name, WATERMARK_BGR_CAPS, test_resolution, &srctemplate, &sinktemplate, setup_buffer_with_roi,
             record_modified_bytes, &with_labels, NULL);

    /* Run with labels OFF */
    run_test(impl_name, WATERMARK_BGR_CAPS, test_resolution, &srctemplate, &sinktemplate, setup_buffer_with_roi,
             record_modified_bytes, &without_labels, "displ-cfg", "show-labels=false", NULL);

    g_print("  Modified bytes with labels: %zu, without labels: %zu\n", with_labels.modified, without_labels.modified);

    ck_assert_msg(with_labels.modified > 0, "Expected some pixels modified when ROI is present (labels on)");
    ck_assert_msg(without_labels.modified > 0,
                  "Expected some pixels modified when ROI is present (labels off, but bbox still drawn)");
    ck_assert_msg(with_labels.modified > without_labels.modified,
                  "show-labels=true should render more pixels than show-labels=false. "
                  "With: %zu, Without: %zu",
                  with_labels.modified, without_labels.modified);
}
GST_END_TEST;

/* ========================================================================= */
/*  Test: ROI filtering via show-roi / hide-roi                              */
/* ========================================================================= */

GST_START_TEST(test_show_roi_filters_non_matching) {
    g_print("Starting test: test_show_roi_filters_non_matching\n");

    RenderStats unfiltered = {"person", 0};
    RenderStats filtered = {"person", 0};

    /* No filter: ROI "person" is rendered */
    run_test(impl_name, WATERMARK_BGR_CAPS, test_resolution, &srctemplate, &sinktemplate, setup_buffer_with_roi,
             record_modified_bytes, &unfiltered, NULL);

    /* show-roi=car: only "car" should be shown, "person" should be filtered out */
    run_test(impl_name, WATERMARK_BGR_CAPS, test_resolution, &srctemplate, &sinktemplate, setup_buffer_with_roi,
             record_modified_bytes, &filtered, "displ-cfg", "show-roi=car", NULL);

    g_print("  Unfiltered: %zu bytes, Filtered (show-roi=car): %zu bytes\n", unfiltered.modified, filtered.modified);

    ck_assert_msg(unfiltered.modified > 0, "Expected pixels modified when 'person' ROI rendered without filter");
    ck_assert_msg(filtered.modified == 0,
                  "Expected zero pixels modified when 'person' ROI is filtered out by show-roi=car. Got: %zu",
                  filtered.modified);
}
GST_END_TEST;

GST_START_TEST(test_hide_roi_hides_matching) {
    g_print("Starting test: test_hide_roi_hides_matching\n");

    RenderStats unfiltered = {"person", 0};
    RenderStats filtered = {"person", 0};

    /* No filter: ROI "person" is rendered */
    run_test(impl_name, WATERMARK_BGR_CAPS, test_resolution, &srctemplate, &sinktemplate, setup_buffer_with_roi,
             record_modified_bytes, &unfiltered, NULL);

    /* hide-roi=person: "person" should be hidden */
    run_test(impl_name, WATERMARK_BGR_CAPS, test_resolution, &srctemplate, &sinktemplate, setup_buffer_with_roi,
             record_modified_bytes, &filtered, "displ-cfg", "hide-roi=person", NULL);

    g_print("  Unfiltered: %zu bytes, Filtered (hide-roi=person): %zu bytes\n", unfiltered.modified, filtered.modified);

    ck_assert_msg(unfiltered.modified > 0, "Expected pixels modified when 'person' ROI rendered without filter");
    ck_assert_msg(filtered.modified == 0,
                  "Expected zero pixels modified when 'person' ROI is hidden by hide-roi=person. Got: %zu",
                  filtered.modified);
}
GST_END_TEST;

GST_START_TEST(test_show_roi_allows_matching) {
    g_print("Starting test: test_show_roi_allows_matching\n");

    RenderStats stats = {"person", 0};

    /* show-roi=person: "person" should still be rendered */
    run_test(impl_name, WATERMARK_BGR_CAPS, test_resolution, &srctemplate, &sinktemplate, setup_buffer_with_roi,
             record_modified_bytes, &stats, "displ-cfg", "show-roi=person", NULL);

    ck_assert_msg(stats.modified > 0, "Expected pixels modified when 'person' ROI matches show-roi=person filter");
}
GST_END_TEST;

/* ========================================================================= */
/*  Test: thickness affects rendering                                        */
/* ========================================================================= */

GST_START_TEST(test_thickness_affects_pixel_count) {
    g_print("Starting test: test_thickness_affects_pixel_count\n");

    RenderStats thin = {NULL, 0};
    RenderStats thick = {NULL, 0};

    /* Thin bounding box (thickness=1) */
    run_test(impl_name, WATERMARK_BGR_CAPS, test_resolution, &srctemplate, &sinktemplate, setup_buffer_with_roi,
             record_modified_bytes, &thin, "displ-cfg", "thickness=1,show-labels=false", NULL);

    /* Thick bounding box (thickness=9) */
    run_test(impl_name, WATERMARK_BGR_CAPS, test_resolution, &srctemplate, &sinktemplate, setup_buffer_with_roi,
             record_modified_bytes, &thick, "displ-cfg", "thickness=9,show-labels=false", NULL);

    g_print("  Thin (1): %zu bytes, Thick (9): %zu bytes\n", thin.modified, thick.modified);

    ck_assert_msg(thick.modified > thin.modified,
                  "thickness=9 should produce more modified pixels than thickness=1. "
                  "Thin: %zu, Thick: %zu",
                  thin.modified, thick.modified);
}
GST_END_TEST;

/* ========================================================================= */
/*  Test: show-roi + hide-roi conflict (show-roi takes precedence)           */
/* ========================================================================= */

GST_START_TEST(test_show_roi_hide_roi_conflict) {
    g_print("Starting test: test_show_roi_hide_roi_conflict\n");

    RenderStats stats = {"person", 0};

    /* Both show-roi=person and hide-roi=person set.
       Source code: when show-roi is non-empty, hide-roi is ignored.
       So "person" should still be rendered. */
    run_test(impl_name, WATERMARK_BGR_CAPS, test_resolution, &srctemplate, &sinktemplate, setup_buffer_with_roi,
             record_modified_bytes, &stats, "displ-cfg", "show-roi=person,hide-roi=person", NULL);

    g_print("  Modified bytes (show-roi=person,hide-roi=person): %zu\n", stats.modified);

    ck_assert_msg(stats.modified > 0,
                  "When both show-roi and hide-roi match, show-roi should take precedence. "
                  "Expected rendering, got %zu modified bytes",
                  stats.modified);
}
GST_END_TEST;

/* ========================================================================= */
/*  Test: invalid config value causes pipeline error                         */
/* ========================================================================= */

/* Runs a pipeline with the given displ-cfg and expects the element to fail to initialize */
static void expect_config_error(const gchar *cfg) {
    GstElement *pipeline = gst_pipeline_new("test-pipeline");
    GstElement *source = gst_element_factory_make("videotestsrc", "source");
    GstElement *element = gst_element_factory_make(impl_name, "watermark");
    GstElement *sink = gst_element_factory_make("fakesink", "sink");

    ck_assert(pipeline && source && element && sink);

    g_object_set(G_OBJECT(source), "num-buffers", 1, NULL);
    g_object_set(G_OBJECT(element), "displ-cfg", cfg, NULL);

    gst_bin_add_many(GST_BIN(pipeline), source, element, sink, NULL);
    gst_element_link_many(source, element, sink, NULL);

    GstBus *bus = gst_pipeline_get_bus(GST_PIPELINE(pipeline));
    gst_element_set_state(pipeline, GST_STATE_PLAYING);

    GstMessage *msg = gst_bus_poll(bus, (GstMessageType)(GST_MESSAGE_ERROR | GST_MESSAGE_EOS), 5 * GST_SECOND);
    ck_assert_msg(msg != NULL, "Expected an error message on the bus for displ-cfg '%s'", cfg);
    ck_assert_msg(GST_MESSAGE_TYPE(msg) == GST_MESSAGE_ERROR, "Expected ERROR message for displ-cfg '%s', got %s", cfg,
                  GST_MESSAGE_TYPE_NAME(msg));

    GError *err = NULL;
    gst_message_parse_error(msg, &err, NULL);
    ck_assert(err != NULL);
    /* set_caps catches the Impl constructor exception and posts "Could not initialize" */
    ck_assert_msg(strstr(err->message, "Could not initialize") != NULL,
                  "Expected 'Could not initialize' error. Got: %s", err->message);

    g_error_free(err);
    gst_message_unref(msg);
    gst_object_unref(bus);
    gst_element_set_state(pipeline, GST_STATE_NULL);
    gst_object_unref(pipeline);
}

GST_START_TEST(test_invalid_config_value_fails) {
    g_print("Starting test: test_invalid_config_value_fails\n");
    expect_config_error("thickness=abc");
}
GST_END_TEST;

/* ========================================================================= */
/*  Test: color-table sets the colors of the listed labels, and label text   */
/*  on a background is black or white, whichever contrasts                   */
/* ========================================================================= */

/* Writes content to a new temporary file and returns its path, to be freed with g_free() */
static gchar *write_temp_file(const gchar *content) {
    GError *error = NULL;
    gchar *path = NULL;
    gint fd = g_file_open_tmp("gvawatermark-colors-XXXXXX.txt", &path, &error);
    ck_assert_msg(fd != -1, "Could not create a temporary file: %s", error ? error->message : "unknown error");
    g_close(fd, NULL);
    ck_assert_msg(g_file_set_contents(path, content, -1, &error), "Could not write %s", path);
    return path;
}

/* Gray frame, so pure black and pure white pixels can only come from the label text. Unlike
   setup_buffer_with_roi it leaves user_data to the check callback. */
static void setup_gray_buffer_with_roi(GstBuffer *buffer, gpointer /*user_data*/) {
    GstMapInfo info;
    ck_assert(gst_buffer_map(buffer, &info, GST_MAP_WRITE));
    memset(info.data, 0x80, info.size);
    gst_buffer_unmap(buffer, &info);

    add_roi_to_buffer(buffer, "person", 50, 50, 100, 100);
}

constexpr guint64 TRACKED_OBJECT_ID = 5; /* built-in tracking color: entry 5, magenta */

/* Same as setup_gray_buffer_with_roi, with the ROI tracked as object TRACKED_OBJECT_ID */
static void setup_gray_buffer_with_tracked_roi(GstBuffer *buffer, gpointer user_data) {
    setup_gray_buffer_with_roi(buffer, user_data);

    GstAnalyticsRelationMeta *relation_meta = gst_buffer_get_analytics_relation_meta(buffer);
    ck_assert(relation_meta != nullptr);
    GstAnalyticsODMtd od_mtd;
    ck_assert(gst_analytics_relation_meta_get_od_mtd(relation_meta, 0, &od_mtd));
    GstAnalyticsTrackingMtd trk_mtd;
    ck_assert(
        gst_analytics_relation_meta_add_tracking_mtd(relation_meta, TRACKED_OBJECT_ID, GST_CLOCK_TIME_NONE, &trk_mtd));
    ck_assert(gst_analytics_relation_meta_set_relation(relation_meta, GST_ANALYTICS_REL_TYPE_RELATE_TO, od_mtd.id,
                                                       trk_mtd.id));
}

struct PixelStats {
    guint x, y;    /* pixel to sample */
    guint8 bgr[3]; /* filled by check callback */
};

static void record_pixel(GstBuffer *buffer, gpointer user_data) {
    PixelStats *stats = (PixelStats *)user_data;
    GstMapInfo info;
    ck_assert(gst_buffer_map(buffer, &info, GST_MAP_READ));
    gsize offset = (stats->y * test_resolution.width + stats->x) * 3;
    ck_assert(offset + 2 < info.size);
    memcpy(stats->bgr, info.data + offset, 3);
    gst_buffer_unmap(buffer, &info);
}

/* Draws the "person" ROI box with the given displ-cfg (labels off) and checks its color */
static void expect_box_color(const gchar *cfg, guint8 b, guint8 g, guint8 r,
                             void (*setup)(GstBuffer *, gpointer) = setup_gray_buffer_with_roi) {
    gchar *full_cfg = g_strdup_printf("%s,show-labels=false", cfg);

    /* Left edge of the 100x100 box at 50,50 */
    PixelStats stats = {50, 100, {0, 0, 0}};
    run_test(impl_name, WATERMARK_BGR_CAPS, test_resolution, &srctemplate, &sinktemplate, setup, record_pixel, &stats,
             "displ-cfg", full_cfg, NULL);

    ck_assert_msg(stats.bgr[0] == b && stats.bgr[1] == g && stats.bgr[2] == r,
                  "displ-cfg '%s': expected the box in BGR %d,%d,%d, got %d,%d,%d", full_cfg, b, g, r, stats.bgr[0],
                  stats.bgr[1], stats.bgr[2]);
    g_free(full_cfg);
}

GST_START_TEST(test_color_table_sets_roi_color) {
    g_print("Starting test: test_color_table_sets_roi_color\n");

    /* Labels match like the built-in colors (case is ignored); comments and both separators are accepted */
    gchar *listed = write_temp_file("# test colors\n\nPerson: 10, 20, 30  # comment\ncar: 40 50 60\n");
    gchar *not_listed = write_temp_file("car: 40,50,60\n");
    gchar *listed_cfg = g_strdup_printf("color-table=%s", listed);
    gchar *not_listed_cfg = g_strdup_printf("color-table=%s", not_listed);

    expect_box_color(listed_cfg, 30, 20, 10);
    /* A label that is not in the table keeps its built-in color: person is red */
    expect_box_color(not_listed_cfg, 0, 0, 255);

    g_unlink(listed);
    g_unlink(not_listed);
    g_free(listed_cfg);
    g_free(not_listed_cfg);
    g_free(listed);
    g_free(not_listed);
}
GST_END_TEST;

GST_START_TEST(test_color_table_overrides_tracking_color) {
    g_print("Starting test: test_color_table_overrides_tracking_color\n");

    /* Without a table entry, a tracked object is colored by its tracking id */
    expect_box_color("thickness=2", 255, 0, 255, setup_gray_buffer_with_tracked_roi);

    gchar *path = write_temp_file("person: 10,20,30\n");
    gchar *cfg = g_strdup_printf("color-table=%s", path);

    expect_box_color(cfg, 30, 20, 10, setup_gray_buffer_with_tracked_roi);

    g_unlink(path);
    g_free(cfg);
    g_free(path);
}
GST_END_TEST;

GST_START_TEST(test_color_idx_overrides_color_table) {
    g_print("Starting test: test_color_idx_overrides_color_table\n");

    /* Built-in table, beyond the red/green/blue entries: 3 is yellow */
    expect_box_color("color-idx=3", 0, 255, 255);

    gchar *path = write_temp_file("person: 10,20,30\n");
    gchar *in_range = g_strdup_printf("color-table=%s,color-idx=2", path);
    gchar *out_of_range = g_strdup_printf("color-table=%s,color-idx=20", path);

    /* color-idx picks the built-in table entry for every object, 2 is blue */
    expect_box_color(in_range, 255, 0, 0);
    /* Only a warning: the ROI keeps its color from the color table */
    expect_box_color(out_of_range, 30, 20, 10);

    g_unlink(path);
    g_free(in_range);
    g_free(out_of_range);
    g_free(path);
}
GST_END_TEST;

struct TextColorStats {
    gsize black; /* pixels at 0,0,0 */
    gsize white; /* pixels at 255,255,255 */
};

static void count_black_and_white_pixels(GstBuffer *buffer, gpointer user_data) {
    TextColorStats *stats = (TextColorStats *)user_data;
    GstMapInfo info;
    ck_assert(gst_buffer_map(buffer, &info, GST_MAP_READ));
    for (gsize i = 0; i + 2 < info.size; i += 3) {
        const guint8 *p = info.data + i;
        stats->black += (p[0] == 0 && p[1] == 0 && p[2] == 0);
        stats->white += (p[0] == 255 && p[1] == 255 && p[2] == 255);
    }
    gst_buffer_unmap(buffer, &info);
}

GST_START_TEST(test_label_text_contrasts_with_background) {
    g_print("Starting test: test_label_text_contrasts_with_background\n");

    gchar *yellow = write_temp_file("person: 255,255,0\n");
    gchar *blue = write_temp_file("person: 0,0,255\n");
    gchar *yellow_cfg = g_strdup_printf("color-table=%s", yellow);
    gchar *blue_cfg = g_strdup_printf("color-table=%s", blue);

    TextColorStats on_yellow = {0, 0};
    TextColorStats on_blue = {0, 0};
    run_test(impl_name, WATERMARK_BGR_CAPS, test_resolution, &srctemplate, &sinktemplate, setup_gray_buffer_with_roi,
             count_black_and_white_pixels, &on_yellow, "displ-cfg", yellow_cfg, NULL);
    run_test(impl_name, WATERMARK_BGR_CAPS, test_resolution, &srctemplate, &sinktemplate, setup_gray_buffer_with_roi,
             count_black_and_white_pixels, &on_blue, "displ-cfg", blue_cfg, NULL);

    g_print("  On yellow: %zu black, %zu white pixels; on blue: %zu black, %zu white pixels\n", on_yellow.black,
            on_yellow.white, on_blue.black, on_blue.white);

    ck_assert_msg(on_yellow.black > 0 && on_yellow.white == 0, "Expected black label text on a yellow background");
    ck_assert_msg(on_blue.white > 0 && on_blue.black == 0, "Expected white label text on a blue background");

    g_unlink(yellow);
    g_unlink(blue);
    g_free(yellow_cfg);
    g_free(blue_cfg);
    g_free(yellow);
    g_free(blue);
}
GST_END_TEST;

GST_START_TEST(test_invalid_color_table_fails) {
    g_print("Starting test: test_invalid_color_table_fails\n");

    gchar *out_of_range = write_temp_file("person: 255,0,0\ncar: 0,256,0\n");
    gchar *missing_component = write_temp_file("person: 255,0\n");
    gchar *missing_label = write_temp_file("255,0,0\n");
    gchar *empty_label = write_temp_file(" _-: 255,0,0\n");
    gchar *duplicate_label = write_temp_file("traffic light: 255,0,0\ntraffic_light: 0,255,0\n");
    gchar *no_colors = write_temp_file("# no colors\n\n");
    const gchar *paths[] = {out_of_range,
                            missing_component,
                            missing_label,
                            empty_label,
                            duplicate_label,
                            no_colors,
                            "gvawatermark-no-such-file.txt"};

    for (const gchar *path : paths) {
        gchar *cfg = g_strdup_printf("color-table=%s", path);
        expect_config_error(cfg);
        g_free(cfg);
    }

    for (gchar *path : {out_of_range, missing_component, missing_label, empty_label, duplicate_label, no_colors}) {
        g_unlink(path);
        g_free(path);
    }
}
GST_END_TEST;

/* ========================================================================= */
/*  Test: multiple ROIs produce more pixels than a single ROI                */
/* ========================================================================= */

static void setup_buffer_with_two_rois(GstBuffer *buffer, gpointer user_data) {
    GstMapInfo info;
    ck_assert(gst_buffer_map(buffer, &info, GST_MAP_WRITE));
    memset(info.data, 0x00, info.size);
    gst_buffer_unmap(buffer, &info);

    (void)user_data;
    add_roi_to_buffer(buffer, "person", 10, 10, 80, 80);
    add_roi_to_buffer(buffer, "car", 200, 100, 100, 80);
}

GST_START_TEST(test_multiple_rois_more_pixels) {
    g_print("Starting test: test_multiple_rois_more_pixels\n");

    RenderStats single = {NULL, 0};
    RenderStats multi = {NULL, 0};

    /* Single ROI */
    run_test(impl_name, WATERMARK_BGR_CAPS, test_resolution, &srctemplate, &sinktemplate, setup_buffer_with_roi,
             record_modified_bytes, &single, "displ-cfg", "show-labels=false", NULL);

    /* Two ROIs */
    run_test(impl_name, WATERMARK_BGR_CAPS, test_resolution, &srctemplate, &sinktemplate, setup_buffer_with_two_rois,
             record_modified_bytes, &multi, "displ-cfg", "show-labels=false", NULL);

    g_print("  Single ROI: %zu bytes, Two ROIs: %zu bytes\n", single.modified, multi.modified);

    ck_assert_msg(single.modified > 0, "Expected pixels from single ROI");
    ck_assert_msg(multi.modified > single.modified,
                  "Two ROIs should produce more modified pixels than one. "
                  "Single: %zu, Multi: %zu",
                  single.modified, multi.modified);
}
GST_END_TEST;

/* ========================================================================= */
/*  Test: show-roi is applied regardless of show-labels                      */
/*                                                                           */
/*  show-labels only toggles the per-ROI text. The ROI filter decides        */
/*  whether anything is drawn for an ROI, so with show-labels=false the      */
/*  filtered-out ROI must not render its bbox either.                        */
/* ========================================================================= */

GST_START_TEST(test_show_labels_false_ignores_show_roi) {
    g_print("Starting test: test_show_labels_false_ignores_show_roi\n");

    RenderStats with_filter = {"person", 0};
    RenderStats without_filter = {"person", 0};

    /* show-labels=true,show-roi=car → "person" ROI filtered out (0 pixels) */
    run_test(impl_name, WATERMARK_BGR_CAPS, test_resolution, &srctemplate, &sinktemplate, setup_buffer_with_roi,
             record_modified_bytes, &with_filter, "displ-cfg", "show-labels=true,show-roi=car", NULL);

    /* show-labels=false,show-roi=car → "person" ROI also filtered out */
    run_test(impl_name, WATERMARK_BGR_CAPS, test_resolution, &srctemplate, &sinktemplate, setup_buffer_with_roi,
             record_modified_bytes, &without_filter, "displ-cfg", "show-labels=false,show-roi=car", NULL);

    g_print("  labels=true,show-roi=car: %zu bytes; labels=false,show-roi=car: %zu bytes\n", with_filter.modified,
            without_filter.modified);

    ck_assert_msg(with_filter.modified == 0,
                  "With show-labels=true,show-roi=car, 'person' should be filtered. Got: %zu", with_filter.modified);
    ck_assert_msg(without_filter.modified == 0,
                  "With show-labels=false,show-roi=car, 'person' should be filtered. Got: %zu",
                  without_filter.modified);
}
GST_END_TEST;

/* ========================================================================= */
/*  Test: non-square instance/semantic segmentation masks render at the      */
/*  correct rows/columns (regression coverage for commit c0bf460a / #1047)   */
/* ========================================================================= */

GST_START_TEST(test_instance_segmentation_mask_non_square_correct_position) {
    g_print("Starting test: test_instance_segmentation_mask_non_square_correct_position\n");

    SegPixelStats stats = {false, false};
    run_test(impl_name, WATERMARK_BGR_CAPS, seg_test_resolution, &srctemplate, &sinktemplate,
             setup_buffer_with_instance_mask, record_seg_pixel_stats, &stats, NULL);

    /* only colored if width/height were read correctly (full-width band); swapped dims confine
       the "on" region to the right ~30% of the width instead */
    ck_assert_msg(stats.bottom_left_colored,
                  "Expected bottom-left of ROI to be colored - mask width/height were likely swapped");
    ck_assert_msg(!stats.top_center_colored, "Expected top-center of ROI to remain unmodified");
}
GST_END_TEST;

GST_START_TEST(test_semantic_segmentation_mask_non_square_correct_position) {
    g_print("Starting test: test_semantic_segmentation_mask_non_square_correct_position\n");

    SegPixelStats stats = {false, false};
    run_test(impl_name, WATERMARK_BGR_CAPS, seg_test_resolution, &srctemplate, &sinktemplate,
             setup_buffer_with_semantic_mask, record_seg_pixel_stats, &stats, NULL);

    ck_assert_msg(stats.bottom_left_colored,
                  "Expected bottom-left of ROI to be colored - mask width/height were likely swapped");
    ck_assert_msg(!stats.top_center_colored, "Expected top-center of ROI to remain unmodified");
}
GST_END_TEST;

/* ========================================================================= */
/*  Suite setup                                                              */
/* ========================================================================= */

static Suite *watermark_config_testing_suite(void) {
    Suite *s = suite_create("watermark_config_testing");

    /* Valid config strings accepted (loop test) */
    TCase *tc_valid = tcase_create("valid_configs");
    tcase_set_timeout(tc_valid, 30);
    suite_add_tcase(s, tc_valid);
    tcase_add_loop_test(tc_valid, test_config_accepted, 0, NUM_VALID_CONFIGS);

    /* Behavioral: show-labels affects rendering */
    TCase *tc_labels = tcase_create("show_labels_behavior");
    tcase_set_timeout(tc_labels, 30);
    suite_add_tcase(s, tc_labels);
    tcase_add_test(tc_labels, test_show_labels_true_renders_more_than_false);

    /* Behavioral: ROI filtering */
    TCase *tc_filter = tcase_create("roi_filtering");
    tcase_set_timeout(tc_filter, 30);
    suite_add_tcase(s, tc_filter);
    tcase_add_test(tc_filter, test_show_roi_filters_non_matching);
    tcase_add_test(tc_filter, test_hide_roi_hides_matching);
    tcase_add_test(tc_filter, test_show_roi_allows_matching);
    tcase_add_test(tc_filter, test_show_roi_hide_roi_conflict);
    tcase_add_test(tc_filter, test_show_labels_false_ignores_show_roi);

    /* Error cases */
    TCase *tc_errors = tcase_create("config_errors");
    tcase_set_timeout(tc_errors, 30);
    suite_add_tcase(s, tc_errors);
    tcase_add_test(tc_errors, test_invalid_config_value_fails);
    tcase_add_test(tc_errors, test_invalid_color_table_fails);

    /* Behavioral: color table and label text contrast */
    TCase *tc_colors = tcase_create("color_table");
    tcase_set_timeout(tc_colors, 30);
    suite_add_tcase(s, tc_colors);
    tcase_add_test(tc_colors, test_color_table_sets_roi_color);
    tcase_add_test(tc_colors, test_color_table_overrides_tracking_color);
    tcase_add_test(tc_colors, test_color_idx_overrides_color_table);
    tcase_add_test(tc_colors, test_label_text_contrasts_with_background);

    /* Behavioral: multiple ROIs */
    TCase *tc_multi = tcase_create("multiple_rois");
    tcase_set_timeout(tc_multi, 30);
    suite_add_tcase(s, tc_multi);
    tcase_add_test(tc_multi, test_multiple_rois_more_pixels);

    /* Behavioral: thickness affects rendering */
    TCase *tc_thickness = tcase_create("thickness_behavior");
    tcase_set_timeout(tc_thickness, 30);
    suite_add_tcase(s, tc_thickness);
    tcase_add_test(tc_thickness, test_thickness_affects_pixel_count);

    /* Regression: non-square segmentation mask dims order (commit c0bf460a / #1047) */
    TCase *tc_seg_dims = tcase_create("segmentation_mask_dims");
    tcase_set_timeout(tc_seg_dims, 30);
    suite_add_tcase(s, tc_seg_dims);
    tcase_add_test(tc_seg_dims, test_instance_segmentation_mask_non_square_correct_position);
    tcase_add_test(tc_seg_dims, test_semantic_segmentation_mask_non_square_correct_position);

    return s;
}

GST_CHECK_MAIN(watermark_config_testing);
