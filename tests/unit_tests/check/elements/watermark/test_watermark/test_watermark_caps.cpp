/*******************************************************************************
 * Copyright (C) 2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#include "test_common.h"
#include "test_utils.h"
#include <cstring>

/* ---------- element name under test ---------- */
constexpr char impl_name[] = "gvawatermarkimpl";

static Resolution test_resolution = {320, 240};

/* ========================================================================= */
/*  Data-driven caps negotiation                                             */
/*                                                                           */
/*  A single test function iterates over all supported system-memory formats */
/*  via tcase_add_loop_test, avoiding duplicated pad template declarations.  */
/* ========================================================================= */

struct FormatEntry {
    const char *name;        /* human-readable, for messages */
    const char *caps_string; /* GST_VIDEO_CAPS_MAKE result */
};

/* Constructed at file scope because GST_VIDEO_CAPS_MAKE is a macro that
   expands to a string literal — safe for static init. */
static const FormatEntry supported_formats[] = {
    {"BGR", GST_VIDEO_CAPS_MAKE("BGR")},   {"NV12", GST_VIDEO_CAPS_MAKE("NV12")}, {"BGRA", GST_VIDEO_CAPS_MAKE("BGRA")},
    {"RGBA", GST_VIDEO_CAPS_MAKE("RGBA")}, {"BGRx", GST_VIDEO_CAPS_MAKE("BGRx")}, {"I420", GST_VIDEO_CAPS_MAKE("I420")},
};
static const int NUM_FORMATS = sizeof(supported_formats) / sizeof(supported_formats[0]);

/*
 * Build a src/sink GstStaticPadTemplate pair matching the given caps string.
 * The caps_string must be a string literal or static storage (GstStaticPadTemplate
 * stores the pointer, not a copy).
 */
static GstStaticPadTemplate make_src_template(const char *caps_str) {
    GstStaticPadTemplate t = GST_STATIC_PAD_TEMPLATE("src", GST_PAD_SRC, GST_PAD_ALWAYS, GST_STATIC_CAPS(caps_str));
    return t;
}
static GstStaticPadTemplate make_sink_template(const char *caps_str) {
    GstStaticPadTemplate t = GST_STATIC_PAD_TEMPLATE("sink", GST_PAD_SINK, GST_PAD_ALWAYS, GST_STATIC_CAPS(caps_str));
    return t;
}

GST_START_TEST(test_caps_format_accepted) {
    const FormatEntry &fmt = supported_formats[__i__];
    g_print("Starting test: test_caps_format_accepted[%s]\n", fmt.name);

    GstStaticPadTemplate src = make_src_template(fmt.caps_string);
    GstStaticPadTemplate sink = make_sink_template(fmt.caps_string);

    run_test(impl_name, fmt.caps_string, test_resolution, &src, &sink, NULL, NULL, NULL, NULL);
}
GST_END_TEST;

/* ========================================================================= */
/*  GPU device with system memory should fail                                */
/* ========================================================================= */

GST_START_TEST(test_gpu_device_with_system_memory_fails) {
    g_print("Starting test: test_gpu_device_with_system_memory_fails\n");

    GstElement *pipeline = gst_pipeline_new("test-pipeline");
    GstElement *source = gst_element_factory_make("videotestsrc", "source");
    GstElement *element = gst_element_factory_make(impl_name, "watermark");
    GstElement *sink = gst_element_factory_make("fakesink", "sink");

    ck_assert(pipeline && source && element && sink);

    g_object_set(G_OBJECT(source), "num-buffers", 1, NULL);
    g_object_set(G_OBJECT(element), "device", "GPU", NULL);

    gst_bin_add_many(GST_BIN(pipeline), source, element, sink, NULL);
    gst_element_link_many(source, element, sink, NULL);

    GstBus *bus = gst_pipeline_get_bus(GST_PIPELINE(pipeline));
    gst_element_set_state(pipeline, GST_STATE_PLAYING);

    GstMessage *msg = gst_bus_poll(bus, (GstMessageType)(GST_MESSAGE_ERROR | GST_MESSAGE_EOS), 5 * GST_SECOND);
    ck_assert_msg(msg != NULL, "Expected an error message on the bus");
    ck_assert_msg(GST_MESSAGE_TYPE(msg) == GST_MESSAGE_ERROR, "Expected ERROR message, got %s",
                  GST_MESSAGE_TYPE_NAME(msg));

    GError *err = NULL;
    gst_message_parse_error(msg, &err, NULL);
    ck_assert(err != NULL);
    ck_assert_msg(strstr(err->message, "incompatible with System Memory") != NULL,
                  "Error message does not mention 'incompatible with System Memory'. Got: %s", err->message);

    g_error_free(err);
    gst_message_unref(msg);
    gst_object_unref(bus);
    gst_element_set_state(pipeline, GST_STATE_NULL);
    gst_object_unref(pipeline);
}
GST_END_TEST;

/* ========================================================================= */
/*  Buffer pass-through with no metadata                                     */
/*                                                                           */
/*  Push a buffer filled with 0xAB and no ROI metadata through the element.  */
/*  Verify the output buffer is byte-identical (in-place, no rendering).     */
/* ========================================================================= */

static void fill_buffer_with_pattern(GstBuffer *buffer, gpointer user_data) {
    (void)user_data;
    GstMapInfo info;
    ck_assert(gst_buffer_map(buffer, &info, GST_MAP_WRITE));
    memset(info.data, 0xAB, info.size);
    gst_buffer_unmap(buffer, &info);
}

static void verify_buffer_unchanged(GstBuffer *buffer, gpointer user_data) {
    (void)user_data;
    GstMapInfo info;
    ck_assert(gst_buffer_map(buffer, &info, GST_MAP_READ));

    /* Fast path: single memcmp against a reference pattern */
    guint8 *reference = (guint8 *)g_malloc(info.size);
    memset(reference, 0xAB, info.size);
    int cmp = memcmp(info.data, reference, info.size);
    if (cmp != 0) {
        /* Find first mismatch for a useful diagnostic */
        for (gsize i = 0; i < info.size; i++) {
            if (info.data[i] != 0xAB) {
                g_free(reference);
                gst_buffer_unmap(buffer, &info);
                ck_abort_msg("Buffer byte %zu was modified (expected 0xAB, got 0x%02X). "
                             "No metadata was attached so watermark should not modify pixels.",
                             i, info.data[i]);
            }
        }
    }
    g_free(reference);
    gst_buffer_unmap(buffer, &info);
}

/*
 * Passthrough tests use a loop over a subset of formats (BGR + NV12)
 * to verify in-place identity for both packed and planar layouts.
 */
static const FormatEntry passthrough_formats[] = {
    {"BGR", GST_VIDEO_CAPS_MAKE("BGR")},
    {"NV12", GST_VIDEO_CAPS_MAKE("NV12")},
};
static const int NUM_PASSTHROUGH_FORMATS = sizeof(passthrough_formats) / sizeof(passthrough_formats[0]);

GST_START_TEST(test_passthrough_no_metadata) {
    const FormatEntry &fmt = passthrough_formats[__i__];
    g_print("Starting test: test_passthrough_no_metadata[%s]\n", fmt.name);

    GstStaticPadTemplate src = make_src_template(fmt.caps_string);
    GstStaticPadTemplate sink = make_sink_template(fmt.caps_string);

    run_test(impl_name, fmt.caps_string, test_resolution, &src, &sink, fill_buffer_with_pattern,
             verify_buffer_unchanged, NULL, NULL);
}
GST_END_TEST;

/* ========================================================================= */
/*  Unsupported device name should fail                                      */
/* ========================================================================= */

GST_START_TEST(test_unsupported_device_name_fails) {
    g_print("Starting test: test_unsupported_device_name_fails\n");

    GstElement *pipeline = gst_pipeline_new("test-pipeline");
    GstElement *source = gst_element_factory_make("videotestsrc", "source");
    GstElement *element = gst_element_factory_make(impl_name, "watermark");
    GstElement *sink = gst_element_factory_make("fakesink", "sink");

    ck_assert(pipeline && source && element && sink);

    g_object_set(G_OBJECT(source), "num-buffers", 1, NULL);
    g_object_set(G_OBJECT(element), "device", "FPGA", NULL);

    gst_bin_add_many(GST_BIN(pipeline), source, element, sink, NULL);
    gst_element_link_many(source, element, sink, NULL);

    GstBus *bus = gst_pipeline_get_bus(GST_PIPELINE(pipeline));
    gst_element_set_state(pipeline, GST_STATE_PLAYING);

    GstMessage *msg = gst_bus_poll(bus, (GstMessageType)(GST_MESSAGE_ERROR | GST_MESSAGE_EOS), 5 * GST_SECOND);
    ck_assert_msg(msg != NULL, "Expected an error message on the bus");
    ck_assert_msg(GST_MESSAGE_TYPE(msg) == GST_MESSAGE_ERROR, "Expected ERROR message, got %s",
                  GST_MESSAGE_TYPE_NAME(msg));

    GError *err = NULL;
    gchar *dbg = NULL;
    gst_message_parse_error(msg, &err, &dbg);
    ck_assert(err != NULL);
    ck_assert_msg(strstr(err->message, "Unsupported") != NULL || (dbg && strstr(dbg, "not supported") != NULL),
                  "Error should mention unsupported device. Got: %s (debug: %s)", err->message, dbg ? dbg : "(null)");

    g_error_free(err);
    g_free(dbg);
    gst_message_unref(msg);
    gst_object_unref(bus);
    gst_element_set_state(pipeline, GST_STATE_NULL);
    gst_object_unref(pipeline);
}
GST_END_TEST;

/* ========================================================================= */
/*  Various resolutions                                                      */
/* ========================================================================= */

static const Resolution resolution_variants[] = {
    {64, 48},
    {1920, 1080},
};
static const int NUM_RESOLUTIONS = sizeof(resolution_variants) / sizeof(resolution_variants[0]);

GST_START_TEST(test_caps_resolution_variant) {
    const Resolution &res = resolution_variants[__i__];
    g_print("Starting test: test_caps_resolution_variant[%dx%d]\n", res.width, res.height);

    GstStaticPadTemplate src = make_src_template(GST_VIDEO_CAPS_MAKE("BGR"));
    GstStaticPadTemplate sink = make_sink_template(GST_VIDEO_CAPS_MAKE("BGR"));

    run_test(impl_name, GST_VIDEO_CAPS_MAKE("BGR"), res, &src, &sink, NULL, NULL, NULL, NULL);
}
GST_END_TEST;

/* ========================================================================= */
/*  Suite setup                                                              */
/* ========================================================================= */

static Suite *watermark_caps_testing_suite(void) {
    Suite *s = suite_create("watermark_caps_testing");

    /* Caps negotiation — each supported system memory format (loop test) */
    TCase *tc_caps = tcase_create("caps_negotiation");
    tcase_set_timeout(tc_caps, 30);
    suite_add_tcase(s, tc_caps);
    tcase_add_loop_test(tc_caps, test_caps_format_accepted, 0, NUM_FORMATS);

    /* Failure cases */
    TCase *tc_fail = tcase_create("caps_failure");
    tcase_set_timeout(tc_fail, 30);
    suite_add_tcase(s, tc_fail);
    tcase_add_test(tc_fail, test_gpu_device_with_system_memory_fails);
    tcase_add_test(tc_fail, test_unsupported_device_name_fails);

    /* Buffer pass-through with no metadata (loop test) */
    TCase *tc_passthrough = tcase_create("buffer_passthrough");
    tcase_set_timeout(tc_passthrough, 30);
    suite_add_tcase(s, tc_passthrough);
    tcase_add_loop_test(tc_passthrough, test_passthrough_no_metadata, 0, NUM_PASSTHROUGH_FORMATS);

    /* Resolution variants (loop test) */
    TCase *tc_resolution = tcase_create("resolution_variants");
    tcase_set_timeout(tc_resolution, 30);
    suite_add_tcase(s, tc_resolution);
    tcase_add_loop_test(tc_resolution, test_caps_resolution_variant, 0, NUM_RESOLUTIONS);

    return s;
}

GST_CHECK_MAIN(watermark_caps_testing);
