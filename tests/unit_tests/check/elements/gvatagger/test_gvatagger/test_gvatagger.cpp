/*******************************************************************************
 * Copyright (C) 2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

/**
 * @file test_gvatagger.cpp
 * @brief Behavioral / buffer-level tests for the gvatagger GStreamer element.
 *
 * These tests verify:
 *   - Element instantiation and default/round-trip property values
 *   - Warm-up fail-safe tagging (motion=TRUE for every AU until the baseline is seeded)
 *   - Steady-state motion threshold (vcl_bytes > MAX(k * ema, ema + margin))
 *   - I-frames and AUs without VCL data are always tagged motion=FALSE
 *   - FLUSH_STOP resets the warm-up/EMA state
 */

#include "gvatagger.h"
#include "test_common.h"
#include "test_utils.h"

#include <cstring>

/* ---------- element name under test ---------- */
constexpr char elem_name[] = "gvatagger";

/* H.264 NAL unit types relevant to VCL/IDR detection (see ITU-T H.264 Table 7-1). */
constexpr guint8 NAL_TYPE_SLICE = 1;     /* coded slice of a non-IDR picture (VCL) */
constexpr guint8 NAL_TYPE_SLICE_IDR = 5; /* coded slice of an IDR picture (VCL) */
constexpr guint8 NAL_TYPE_SEI = 6;       /* supplemental enhancement info (non-VCL) */

#define H264_BYTE_STREAM_CAPS "video/x-h264, stream-format=(string)byte-stream, alignment=(string)au"

static GstStaticPadTemplate sink_template =
    GST_STATIC_PAD_TEMPLATE("sink", GST_PAD_SINK, GST_PAD_ALWAYS, GST_STATIC_CAPS(H264_BYTE_STREAM_CAPS));
static GstStaticPadTemplate src_template =
    GST_STATIC_PAD_TEMPLATE("src", GST_PAD_SRC, GST_PAD_ALWAYS, GST_STATIC_CAPS(H264_BYTE_STREAM_CAPS));

/* ========================================================================= */
/*  Helpers                                                                  */
/* ========================================================================= */

struct TaggerFixture {
    GstElement *element;
    GstPad *srcpad;  /* fake pad feeding the element's sink pad */
    GstPad *sinkpad; /* fake pad receiving the element's src pad */
};

static void negotiate(TaggerFixture &f) {
    GstCaps *caps = gst_caps_from_string(H264_BYTE_STREAM_CAPS);
    gst_check_setup_events(f.srcpad, f.element, caps, GST_FORMAT_TIME);
    gst_caps_unref(caps);
}

static TaggerFixture setup_tagger() {
    TaggerFixture f;
    f.element = gst_check_setup_element(elem_name);
    ck_assert_msg(f.element != NULL, "Failed to create element '%s'", elem_name);

    f.srcpad = gst_check_setup_src_pad(f.element, &src_template);
    f.sinkpad = gst_check_setup_sink_pad(f.element, &sink_template);
    gst_pad_set_active(f.srcpad, TRUE);
    gst_pad_set_active(f.sinkpad, TRUE);

    ck_assert(gst_element_set_state(f.element, GST_STATE_PLAYING) != GST_STATE_CHANGE_FAILURE);
    negotiate(f);
    return f;
}

static void teardown_tagger(TaggerFixture &f) {
    gst_element_set_state(f.element, GST_STATE_NULL);
    gst_check_drop_buffers();
    gst_check_teardown_src_pad(f.element);
    gst_check_teardown_sink_pad(f.element);
    gst_check_teardown_element(f.element);
}

/* Builds a single-NAL H.264 byte-stream access unit: start code + NAL header + filler payload. */
static GstBuffer *make_h264_au(guint8 nal_type, gsize payload_size) {
    const guint8 start_code[4] = {0x00, 0x00, 0x00, 0x01};
    const guint8 header = (guint8)(0x60 | (nal_type & 0x1F));
    const gsize total = sizeof(start_code) + 1 + payload_size;

    GstBuffer *buf = gst_buffer_new_and_alloc(total);
    GstMapInfo map;
    gst_buffer_map(buf, &map, GST_MAP_WRITE);
    memcpy(map.data, start_code, sizeof(start_code));
    map.data[sizeof(start_code)] = header;
    if (payload_size)
        memset(map.data + sizeof(start_code) + 1, 0xAB, payload_size);
    gst_buffer_unmap(buf, &map);

    GST_BUFFER_PTS(buf) = 0;
    return buf;
}

/* Pushes one AU and returns the (single) output buffer produced in response, without unreffing it. */
static GstBuffer *push_au_and_get_output(TaggerFixture &f, guint8 nal_type, gsize payload_size) {
    GstBuffer *inbuf = make_h264_au(nal_type, payload_size);
    ck_assert(gst_pad_push(f.srcpad, inbuf) == GST_FLOW_OK);
    ck_assert_msg(g_list_length(buffers) == 1, "Expected exactly one output buffer, got %u", g_list_length(buffers));

    GstBuffer *outbuf = GST_BUFFER(buffers->data);
    buffers = g_list_remove(buffers, outbuf);
    return outbuf;
}

static void get_tags(GstBuffer *buf, gboolean *motion, gboolean *keyframe) {
    GstCustomMeta *meta = gst_buffer_get_custom_meta(buf, GST_GVA_TAGGER_META_NAME);
    ck_assert_msg(meta != NULL, "Buffer has no %s", GST_GVA_TAGGER_META_NAME);
    GstStructure *s = gst_custom_meta_get_structure(meta);
    ck_assert(gst_structure_get_boolean(s, "motion", motion));
    ck_assert(gst_structure_get_boolean(s, "keyframe", keyframe));
}

/* Pushes enough identical quiet non-IDR AUs to complete warm-up and seed the EMA baseline. */
static void run_warmup(TaggerFixture &f, gsize quiet_payload_size) {
    for (guint i = 0; i < GST_GVA_TAGGER_WARMUP_COUNT; i++) {
        GstBuffer *outbuf = push_au_and_get_output(f, NAL_TYPE_SLICE, quiet_payload_size);
        gst_buffer_unref(outbuf);
    }
}

/* ========================================================================= */
/*  Instantiation                                                            */
/* ========================================================================= */

GST_START_TEST(test_gvatagger_instantiation) {
    GstElement *element = gst_element_factory_make(elem_name, NULL);
    ck_assert_msg(element != NULL, "Failed to create element '%s'", elem_name);
    ck_assert(GST_IS_ELEMENT(element));
    gst_object_unref(element);
}
GST_END_TEST;

/* ========================================================================= */
/*  Default property values                                                 */
/* ========================================================================= */

GST_START_TEST(test_default_k_property) {
    GstElement *element = gst_check_setup_element(elem_name);
    gdouble k = 0.0;
    g_object_get(G_OBJECT(element), "k", &k, NULL);
    ck_assert_msg(k == 1.5, "Expected default k to be 1.5, got %f", k);
    gst_check_teardown_element(element);
}
GST_END_TEST;

GST_START_TEST(test_default_alpha_property) {
    GstElement *element = gst_check_setup_element(elem_name);
    gdouble alpha = 0.0;
    g_object_get(G_OBJECT(element), "alpha", &alpha, NULL);
    ck_assert_msg(alpha == 0.03, "Expected default alpha to be 0.03, got %f", alpha);
    gst_check_teardown_element(element);
}
GST_END_TEST;

GST_START_TEST(test_default_margin_property) {
    GstElement *element = gst_check_setup_element(elem_name);
    guint64 margin = 0;
    g_object_get(G_OBJECT(element), "margin", &margin, NULL);
    ck_assert_msg(margin == 8, "Expected default margin to be 8, got %" G_GUINT64_FORMAT, margin);
    gst_check_teardown_element(element);
}
GST_END_TEST;

GST_START_TEST(test_default_min_baseline_property) {
    GstElement *element = gst_check_setup_element(elem_name);
    guint64 min_baseline = 0;
    g_object_get(G_OBJECT(element), "min-baseline", &min_baseline, NULL);
    ck_assert_msg(min_baseline == 10, "Expected default min-baseline to be 10, got %" G_GUINT64_FORMAT, min_baseline);
    gst_check_teardown_element(element);
}
GST_END_TEST;

/* ========================================================================= */
/*  Property set/get round-trip                                             */
/* ========================================================================= */

GST_START_TEST(test_set_get_k) {
    GstElement *element = gst_check_setup_element(elem_name);
    g_object_set(G_OBJECT(element), "k", 2.5, NULL);
    gdouble k = 0.0;
    g_object_get(G_OBJECT(element), "k", &k, NULL);
    ck_assert(k == 2.5);
    gst_check_teardown_element(element);
}
GST_END_TEST;

GST_START_TEST(test_set_get_alpha) {
    GstElement *element = gst_check_setup_element(elem_name);
    g_object_set(G_OBJECT(element), "alpha", 0.1, NULL);
    gdouble alpha = 0.0;
    g_object_get(G_OBJECT(element), "alpha", &alpha, NULL);
    ck_assert(alpha == 0.1);
    gst_check_teardown_element(element);
}
GST_END_TEST;

GST_START_TEST(test_set_get_margin) {
    GstElement *element = gst_check_setup_element(elem_name);
    g_object_set(G_OBJECT(element), "margin", (guint64)20, NULL);
    guint64 margin = 0;
    g_object_get(G_OBJECT(element), "margin", &margin, NULL);
    ck_assert(margin == 20);
    gst_check_teardown_element(element);
}
GST_END_TEST;

GST_START_TEST(test_set_get_min_baseline) {
    GstElement *element = gst_check_setup_element(elem_name);
    g_object_set(G_OBJECT(element), "min-baseline", (guint64)50, NULL);
    guint64 min_baseline = 0;
    g_object_get(G_OBJECT(element), "min-baseline", &min_baseline, NULL);
    ck_assert(min_baseline == 50);
    gst_check_teardown_element(element);
}
GST_END_TEST;

/* ========================================================================= */
/*  Warm-up phase behavior                                                   */
/* ========================================================================= */

GST_START_TEST(test_warmup_nonidr_tagged_motion_true) {
    TaggerFixture f = setup_tagger();

    GstBuffer *outbuf = push_au_and_get_output(f, NAL_TYPE_SLICE, 50);
    gboolean motion = FALSE, keyframe = TRUE;
    get_tags(outbuf, &motion, &keyframe);
    ck_assert_msg(motion == TRUE, "Expected motion=TRUE during warm-up (fail-safe)");
    ck_assert_msg(keyframe == FALSE, "Expected keyframe=FALSE for a non-IDR slice");
    gst_buffer_unref(outbuf);

    teardown_tagger(f);
}
GST_END_TEST;

GST_START_TEST(test_warmup_idr_tagged_motion_true_and_keyframe_true) {
    TaggerFixture f = setup_tagger();

    GstBuffer *outbuf = push_au_and_get_output(f, NAL_TYPE_SLICE_IDR, 500);
    gboolean motion = FALSE, keyframe = FALSE;
    get_tags(outbuf, &motion, &keyframe);
    ck_assert_msg(motion == TRUE, "Expected motion=TRUE during warm-up (fail-safe), even for an IDR");
    ck_assert_msg(keyframe == TRUE, "Expected keyframe=TRUE for an IDR slice");
    gst_buffer_unref(outbuf);

    teardown_tagger(f);
}
GST_END_TEST;

GST_START_TEST(test_warmup_idr_does_not_count_toward_warmup) {
    TaggerFixture f = setup_tagger();

    /* IDRs are skipped by the warm-up counter, so GST_GVA_TAGGER_WARMUP_COUNT - 1 quiet
     * non-IDR AUs plus any number of interleaved IDRs must still leave warm-up incomplete. */
    for (guint i = 0; i < GST_GVA_TAGGER_WARMUP_COUNT - 1; i++) {
        GstBuffer *outbuf = push_au_and_get_output(f, NAL_TYPE_SLICE, 50);
        gst_buffer_unref(outbuf);
    }
    for (guint i = 0; i < 5; i++) {
        GstBuffer *outbuf = push_au_and_get_output(f, NAL_TYPE_SLICE_IDR, 500);
        gst_buffer_unref(outbuf);
    }

    GstBuffer *outbuf = push_au_and_get_output(f, NAL_TYPE_SLICE, 50);
    gboolean motion = FALSE, keyframe = TRUE;
    get_tags(outbuf, &motion, &keyframe);
    ck_assert_msg(motion == TRUE, "Expected warm-up to still be in progress (IDRs don't count)");
    gst_buffer_unref(outbuf);

    teardown_tagger(f);
}
GST_END_TEST;

/* ========================================================================= */
/*  Steady-state motion threshold                                           */
/* ========================================================================= */

GST_START_TEST(test_steady_state_quiet_frame_no_motion) {
    TaggerFixture f = setup_tagger();
    run_warmup(f, 50);

    /* Baseline ~= 50 bytes; threshold = MAX(1.5 * 50, 50 + 8) = 75. A same-size AU stays quiet. */
    GstBuffer *outbuf = push_au_and_get_output(f, NAL_TYPE_SLICE, 50);
    gboolean motion = TRUE, keyframe = TRUE;
    get_tags(outbuf, &motion, &keyframe);
    ck_assert_msg(motion == FALSE, "Expected motion=FALSE for a quiet frame at baseline size");
    ck_assert_msg(keyframe == FALSE, "Expected keyframe=FALSE for a non-IDR slice");
    gst_buffer_unref(outbuf);

    teardown_tagger(f);
}
GST_END_TEST;

GST_START_TEST(test_steady_state_large_frame_motion_true) {
    TaggerFixture f = setup_tagger();
    run_warmup(f, 50);

    /* Baseline ~= 50 bytes; threshold = 75. A much larger AU crosses it. */
    GstBuffer *outbuf = push_au_and_get_output(f, NAL_TYPE_SLICE, 500);
    gboolean motion = FALSE, keyframe = TRUE;
    get_tags(outbuf, &motion, &keyframe);
    ck_assert_msg(motion == TRUE, "Expected motion=TRUE for an AU far above baseline");
    ck_assert_msg(keyframe == FALSE, "Expected keyframe=FALSE for a non-IDR slice");
    gst_buffer_unref(outbuf);

    teardown_tagger(f);
}
GST_END_TEST;

GST_START_TEST(test_steady_state_idr_always_no_motion) {
    TaggerFixture f = setup_tagger();
    run_warmup(f, 50);

    /* IDRs are always tagged motion=FALSE in steady state, regardless of size. */
    GstBuffer *outbuf = push_au_and_get_output(f, NAL_TYPE_SLICE_IDR, 5000);
    gboolean motion = TRUE, keyframe = FALSE;
    get_tags(outbuf, &motion, &keyframe);
    ck_assert_msg(motion == FALSE, "Expected motion=FALSE for an IDR regardless of size");
    ck_assert_msg(keyframe == TRUE, "Expected keyframe=TRUE for an IDR slice");
    gst_buffer_unref(outbuf);

    teardown_tagger(f);
}
GST_END_TEST;

GST_START_TEST(test_steady_state_no_vcl_no_motion) {
    TaggerFixture f = setup_tagger();
    run_warmup(f, 50);

    /* An AU with no VCL NAL units (e.g. SEI only) has no picture data: always motion=FALSE. */
    GstBuffer *outbuf = push_au_and_get_output(f, NAL_TYPE_SEI, 5000);
    gboolean motion = TRUE, keyframe = TRUE;
    get_tags(outbuf, &motion, &keyframe);
    ck_assert_msg(motion == FALSE, "Expected motion=FALSE for an AU with no VCL data");
    ck_assert_msg(keyframe == FALSE, "Expected keyframe=FALSE for an AU with no VCL data");
    gst_buffer_unref(outbuf);

    teardown_tagger(f);
}
GST_END_TEST;

GST_START_TEST(test_steady_state_quiet_frame_updates_baseline) {
    TaggerFixture f = setup_tagger();
    run_warmup(f, 50);

    /* A slightly larger-but-still-quiet frame raises the baseline via EMA, so a
     * previously-triggering size no longer crosses the (now higher) threshold. */
    for (guint i = 0; i < 200; i++) {
        GstBuffer *outbuf = push_au_and_get_output(f, NAL_TYPE_SLICE, 65);
        gst_buffer_unref(outbuf);
    }

    GstBuffer *outbuf = push_au_and_get_output(f, NAL_TYPE_SLICE, 65);
    gboolean motion = TRUE, keyframe = TRUE;
    get_tags(outbuf, &motion, &keyframe);
    ck_assert_msg(motion == FALSE, "Expected baseline to have tracked up to the new quiet size");
    gst_buffer_unref(outbuf);

    teardown_tagger(f);
}
GST_END_TEST;

/* ========================================================================= */
/*  FLUSH_STOP resets warm-up/EMA state                                      */
/* ========================================================================= */

GST_START_TEST(test_flush_stop_resets_warmup) {
    TaggerFixture f = setup_tagger();
    run_warmup(f, 50);

    /* Confirm warm-up had completed: a quiet AU at baseline size is tagged motion=FALSE. */
    GstBuffer *outbuf = push_au_and_get_output(f, NAL_TYPE_SLICE, 50);
    gboolean motion = TRUE, keyframe = TRUE;
    get_tags(outbuf, &motion, &keyframe);
    ck_assert(motion == FALSE);
    gst_buffer_unref(outbuf);

    ck_assert(gst_pad_push_event(f.srcpad, gst_event_new_flush_start()));
    ck_assert(gst_pad_push_event(f.srcpad, gst_event_new_flush_stop(TRUE)));
    negotiate(f);

    /* After the reset, warm-up fail-safe tagging applies again. */
    outbuf = push_au_and_get_output(f, NAL_TYPE_SLICE, 50);
    get_tags(outbuf, &motion, &keyframe);
    ck_assert_msg(motion == TRUE, "Expected FLUSH_STOP to reset warm-up state (fail-safe motion=TRUE)");
    gst_buffer_unref(outbuf);

    teardown_tagger(f);
}
GST_END_TEST;

/* ========================================================================= */
/*  Suite setup                                                              */
/* ========================================================================= */

static Suite *gvatagger_testing_suite(void) {
    Suite *s = suite_create("gvatagger_testing");

    TCase *tc_instantiation = tcase_create("instantiation");
    suite_add_tcase(s, tc_instantiation);
    tcase_add_test(tc_instantiation, test_gvatagger_instantiation);

    TCase *tc_defaults = tcase_create("default_properties");
    suite_add_tcase(s, tc_defaults);
    tcase_add_test(tc_defaults, test_default_k_property);
    tcase_add_test(tc_defaults, test_default_alpha_property);
    tcase_add_test(tc_defaults, test_default_margin_property);
    tcase_add_test(tc_defaults, test_default_min_baseline_property);

    TCase *tc_setget = tcase_create("property_set_get");
    suite_add_tcase(s, tc_setget);
    tcase_add_test(tc_setget, test_set_get_k);
    tcase_add_test(tc_setget, test_set_get_alpha);
    tcase_add_test(tc_setget, test_set_get_margin);
    tcase_add_test(tc_setget, test_set_get_min_baseline);

    TCase *tc_warmup = tcase_create("warmup");
    suite_add_tcase(s, tc_warmup);
    tcase_add_test(tc_warmup, test_warmup_nonidr_tagged_motion_true);
    tcase_add_test(tc_warmup, test_warmup_idr_tagged_motion_true_and_keyframe_true);
    tcase_add_test(tc_warmup, test_warmup_idr_does_not_count_toward_warmup);

    TCase *tc_steady = tcase_create("steady_state");
    suite_add_tcase(s, tc_steady);
    tcase_add_test(tc_steady, test_steady_state_quiet_frame_no_motion);
    tcase_add_test(tc_steady, test_steady_state_large_frame_motion_true);
    tcase_add_test(tc_steady, test_steady_state_idr_always_no_motion);
    tcase_add_test(tc_steady, test_steady_state_no_vcl_no_motion);
    tcase_add_test(tc_steady, test_steady_state_quiet_frame_updates_baseline);

    TCase *tc_flush = tcase_create("flush_stop");
    suite_add_tcase(s, tc_flush);
    tcase_add_test(tc_flush, test_flush_stop_resets_warmup);

    return s;
}

GST_CHECK_MAIN(gvatagger_testing);
