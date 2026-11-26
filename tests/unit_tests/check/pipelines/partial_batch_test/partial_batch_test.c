/*******************************************************************************
 * Copyright (C) 2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

// gvaclassify batches ROIs across frames. A ROI waits in a partly filled batch until batch-size ROIs have come, and the
// frames after it wait too, as frames leave in order, each holding its buffer. Where ROIs are sparse, that must not
// last until batch-size ROIs have come, which may be never: the frames in the element must stay few.

#include <glib/gstdio.h>
#include <gst/check/gstcheck.h>
#include <gst/video/video.h>
#include <stdio.h>

#include "test_utils.h"

#define FRAMES 300
#define ROI_PERIOD 100 // a ROI on the first 2 frames of every ROI_PERIOD frames
#define BATCH_SIZE 4
// The partly filled batch starts after BATCH_SIZE frames. The bound leaves room for the frames that arrive while it
// runs, on a slow CPU too; the source is throttled to 200 fps. Without the limit, all frames stay until EOS.
#define MAX_FRAMES_IN_ELEMENT (8 * BATCH_SIZE)

typedef struct {
    GMutex mutex;
    int in;
    int out;
    int max_in_element;
    int classified_rois; // ROIs that left with a classification result
} FrameCounter;

static GstPadProbeReturn count_in(GstPad *pad, GstPadProbeInfo *info, gpointer user_data) {
    FrameCounter *counter = (FrameCounter *)user_data;
    g_mutex_lock(&counter->mutex);
    counter->in++;
    if (counter->in - counter->out > counter->max_in_element)
        counter->max_in_element = counter->in - counter->out;
    g_mutex_unlock(&counter->mutex);
    return GST_PAD_PROBE_OK;
}

static GstPadProbeReturn count_out(GstPad *pad, GstPadProbeInfo *info, gpointer user_data) {
    FrameCounter *counter = (FrameCounter *)user_data;
    int classified = 0;
    gpointer state = NULL;
    GstMeta *meta;
    while ((meta = gst_buffer_iterate_meta_filtered(GST_PAD_PROBE_INFO_BUFFER(info), &state,
                                                    GST_VIDEO_REGION_OF_INTEREST_META_API_TYPE))) {
        // gvaattachroi adds a "detection" param, the classification result is another one
        for (GList *l = ((GstVideoRegionOfInterestMeta *)meta)->params; l; l = l->next)
            if (!gst_structure_has_name((GstStructure *)l->data, "detection")) {
                classified++;
                break;
            }
    }
    g_mutex_lock(&counter->mutex);
    counter->out++;
    counter->classified_rois += classified;
    g_mutex_unlock(&counter->mutex);
    return GST_PAD_PROBE_OK;
}

static void add_probe(GstElement *element, const char *pad_name, GstPadProbeCallback callback, FrameCounter *counter) {
    GstPad *pad = gst_element_get_static_pad(element, pad_name);
    ck_assert(pad != NULL);
    gst_pad_add_probe(pad, GST_PAD_PROBE_TYPE_BUFFER, callback, counter, NULL);
    gst_object_unref(pad);
}

// gvaattachroi input, in-loop: a ROI on the first 2 frames of every ROI_PERIOD frames, none on the others. Two, so that
// one of them runs inference with inference-interval=2.
static gchar *write_roi_file(void) {
    gchar *path = NULL;
    GError *error = NULL;
    gint fd = g_file_open_tmp("partial_batch_test_XXXXXX.json", &path, &error);
    ck_assert_msg(fd >= 0, "cannot create the ROI file: %s", error ? error->message : "");
    g_close(fd, NULL);
#ifdef _WIN32
    // The path ends up in the gst_parse_launch() description, where '\' is an escape character
    g_strdelimit(path, "\\", '/');
#endif

    GString *json = g_string_new("[");
    for (int i = 0; i < 2; i++)
        g_string_append(json, "{\"objects\": [{\"x\": 80, \"y\": 60, \"w\": 160, \"h\": 120}]}, ");
    g_string_append(json, "{}");
    for (int i = 3; i < ROI_PERIOD; i++)
        g_string_append(json, ", {}");
    g_string_append(json, "]");
    ck_assert(g_file_set_contents(path, json->str, -1, NULL));
    g_string_free(json, TRUE);
    return path;
}

// properties: of gvaclassify. classified_rois: the number of ROIs that run inference.
static void check_frames_in_element(const char *properties, int classified_rois) {
    char model_path[MAX_STR_PATH_SIZE];
    ExitStatus status = get_model_path(model_path, MAX_STR_PATH_SIZE, "colorcls2", "FP32");
    ck_assert(status == EXIT_STATUS_SUCCESS);
    gchar *roi_file = write_roi_file();

    gchar *pipeline_str = g_strdup_printf(
        "videotestsrc num-buffers=%d ! video/x-raw,format=BGRx,width=320,height=240 ! identity sleep-time=5000 ! "
        "gvaattachroi file-path=%s mode=in-loop ! "
        "gvaclassify name=classify model=%s device=CPU pre-process-backend=opencv batch-size=%d %s ! "
        "fakesink sync=false",
        FRAMES, roi_file, model_path, BATCH_SIZE, properties);
    GstElement *pipeline = gst_parse_launch(pipeline_str, NULL);
    ck_assert(pipeline != NULL);

    FrameCounter counter = {0};
    g_mutex_init(&counter.mutex);
    GstElement *classify = gst_bin_get_by_name(GST_BIN(pipeline), "classify");
    add_probe(classify, "sink", count_in, &counter);
    add_probe(classify, "src", count_out, &counter);
    gst_object_unref(classify);

    GstBus *bus = gst_element_get_bus(pipeline);
    gst_element_set_state(pipeline, GST_STATE_PLAYING);
    GstMessage *msg = gst_bus_timed_pop_filtered(bus, 60 * GST_SECOND, GST_MESSAGE_ERROR | GST_MESSAGE_EOS);
    ck_assert_msg(msg != NULL, "no EOS after 60 s: the pipeline is stuck");
    if (GST_MESSAGE_TYPE(msg) == GST_MESSAGE_ERROR) {
        GError *error = NULL;
        gst_message_parse_error(msg, &error, NULL);
        ck_abort_msg("the pipeline failed: %s", error->message);
    }
    gst_message_unref(msg);
    gst_element_set_state(pipeline, GST_STATE_NULL);

    g_print("%s: %d frames in, %d out, at most %d in the element, %d ROIs classified\n", properties, counter.in,
            counter.out, counter.max_in_element, counter.classified_rois);
    ck_assert_int_eq(counter.in, FRAMES);
    ck_assert_int_eq(counter.out, FRAMES);
    ck_assert_int_eq(counter.classified_rois, classified_rois);
    ck_assert_int_le(counter.max_in_element, MAX_FRAMES_IN_ELEMENT);

    gst_object_unref(bus);
    gst_object_unref(pipeline);
    g_mutex_clear(&counter.mutex);
    g_unlink(roi_file);
    g_free(roi_file);
    g_free(pipeline_str);
}

GST_START_TEST(test_sparse_rois_do_not_hold_frames) {
    g_print("Starting test: test_sparse_rois_do_not_hold_frames\n");
    check_frames_in_element("scheduling-policy=throughput", 2 * FRAMES / ROI_PERIOD);
}
GST_END_TEST;

// The latency policy pauses while more than nireq * batch-size * inference-interval frames are in the element. It must
// not wait for frames that wait for it. Here it pauses before batch-size frames that run inference have arrived.
GST_START_TEST(test_sparse_rois_latency_policy) {
    g_print("Starting test: test_sparse_rois_latency_policy\n");
    // inference-interval=2: one of the 2 frames with a ROI runs inference
    check_frames_in_element("scheduling-policy=latency nireq=1 inference-interval=2", FRAMES / ROI_PERIOD);
}
GST_END_TEST;

static Suite *partial_batch_test_suite(void) {
    Suite *s = suite_create("partial_batch");
    TCase *test_case = tcase_create("general");

    suite_add_tcase(s, test_case);
    tcase_set_timeout(test_case, 180);
    tcase_add_test(test_case, test_sparse_rois_do_not_hold_frames);
    tcase_add_test(test_case, test_sparse_rois_latency_policy);

    return s;
}

GST_CHECK_MAIN(partial_batch_test);
