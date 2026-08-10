/*******************************************************************************
 * Copyright (C) 2020-2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#include <gst/check/gstcheck.h>
#include <gst/video/gstvideometa.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pipeline_test_common.h"
#include "test_utils.h"

const char *video_src = "People_On_The_Street.mp4";
const char *detect_model = "centerface";
const char *class_model = "dima806_fairface_gender_image_detection";
const char *fp_format = "FP32";
const unsigned buf_num = 50;

/* error messages */
const char *opening_error = "METAAGGREGATE: Unable to open json file %s\n";
const char *comparing_error = "METAAGGREGATE: Json files are not equal [%s, %s]\n";
const char *remove_eror = "METAAGGREGATE: Unable to remove json file %s\n";

/**
 * Moves files pointer to the new line 'amount' times
 */
void new_line(FILE *fptr, int amount) {
    char ch = '\0';
    while (ch != EOF && amount > 0) {
        ch = fgetc(fptr);
        if (ch == '\n')
            --amount;
    }
}

/**
 * Compares to strings till or newline character
 */
int compare_frames(char frame1[], char frame2[]) {
    while (*frame1 == *frame2 && *frame1 != '\n') {
        ++frame1;
        ++frame2;
    }
    return *frame1 == *frame2;
}

/**
 * Reset region_id.
 */
void reset_region_id(const char *file_path) {
    char *command = "sed -i 's/\"region_id\":[0-9]*,/\"region_id\":0,/g' %s";
    char buf[256];
    sprintf(buf, command, file_path);
    int ret = system(buf);
    if (ret != 0)
        printf("Unable to reset region_id: %s", strerror(errno));
}

/**
 * Compares two files considering the given frames drop rate.
 * For example, with droprate1 = 2 method will take only every second line from file1.
 * Asserts fail if files are not equal.
 */
int compare_files(const char *file1, int droprate1, const char *file2, int droprate2) {
    FILE *fptr1 = fopen(file1, "r");
    ck_assert_msg(fptr1 != NULL, opening_error, file1);
    FILE *fptr2 = fopen(file2, "r");
    ck_assert_msg(fptr2 != NULL, opening_error, file2);

    // JSON lines with add_tensor_data can be several KB long; buffer must hold a full line.
    int bufferLength = 65536;
    char buffer1[bufferLength];
    char buffer2[bufferLength];

    int result = 1;
    while (fgets(buffer1, bufferLength, fptr1) != NULL && fgets(buffer2, bufferLength, fptr2) != NULL) {
        if (!compare_frames(buffer1, buffer2)) {
            result = 0;
            break;
        }
        new_line(fptr1, droprate1 - 1);
        new_line(fptr2, droprate2 - 1);
    }

    ck_assert(!ferror(fptr1) && !ferror(fptr2));
    /* Due to possibly different droprate && condition is set */
    if (fgetc(fptr1) != EOF && fgetc(fptr2) != EOF)
        result = 0;

    fclose(fptr1);
    fclose(fptr2);
    return result;
}

GST_START_TEST(test_metaaggregate_drop_frames) {
    gchar command_first[8 * MAX_STR_PATH_SIZE];
    gchar command_second[8 * MAX_STR_PATH_SIZE];

    char model_path[MAX_STR_PATH_SIZE];
    char video_file_path[MAX_STR_PATH_SIZE];

    ExitStatus status = get_video_file_path(video_file_path, MAX_STR_PATH_SIZE, video_src);
    ck_assert(status == EXIT_STATUS_SUCCESS);
    status = get_model_path(model_path, MAX_STR_PATH_SIZE, detect_model, fp_format);
    ck_assert(status == EXIT_STATUS_SUCCESS);

    /* metaaggregate */
    const char *path_first = "./metaaggregate.json";
    snprintf(command_first, sizeof(command_first),
             "filesrc location=%s ! identity eos-after=%d ! decodebin ! videoconvert ! tee name=t t. ! queue ! "
             "gvametaaggregate name=a ! gvametaconvert format=json add_tensor_data=true add-empty-results=true ! "
             "gvametapublish file-path=%s method=file file-format=json-lines ! videoconvert ! fakesink sync=false t. ! "
             "queue ! gvadetect model=%s device=CPU ! a.",
             video_file_path, buf_num, path_first, model_path);
    check_run_pipeline(command_first, GST_SECOND);

    /* metaaggregate drop every second frame */
    const char *path_second = "./metaaggregate_drop_frames.json";
    snprintf(command_second, sizeof(command_second),
             "filesrc location=%s ! identity eos-after=%d ! decodebin ! videoconvert ! tee name=t t. ! queue ! "
             "gvadrop pass-frames=1 drop-frames=1 ! gvametaaggregate name=a ! gvametaconvert format=json "
             "add_tensor_data=true add-empty-results=true ! gvametapublish file-path=%s method=file "
             "file-format=json-lines ! videoconvert ! fakesink sync=false t. ! queue ! "
             "gvadetect model=%s device=CPU ! a.",
             video_file_path, buf_num, path_second, model_path);
    check_run_pipeline(command_second, GST_SECOND);

    // Need to reset the region_id field because it differs from launch to launch.
    reset_region_id(path_first);
    reset_region_id(path_second);

    /* compare first and second meta json files */
    int equal = compare_files(path_first, 2, path_second, 1);

    /* remove files */
    if (remove(path_first) != 0)
        printf(remove_eror, path_first);
    if (remove(path_second) != 0)
        printf(remove_eror, path_second);

    ck_assert_msg(equal, comparing_error, path_first, path_second);
}

GST_END_TEST;

GST_START_TEST(test_metaaggregate_drop_meta) {
    gchar command_first[8 * MAX_STR_PATH_SIZE];
    gchar command_second[8 * MAX_STR_PATH_SIZE];

    char model_path[MAX_STR_PATH_SIZE];
    char video_file_path[MAX_STR_PATH_SIZE];

    ExitStatus status = get_video_file_path(video_file_path, MAX_STR_PATH_SIZE, video_src);
    ck_assert(status == EXIT_STATUS_SUCCESS);
    status = get_model_path(model_path, MAX_STR_PATH_SIZE, detect_model, fp_format);
    ck_assert(status == EXIT_STATUS_SUCCESS);

    /* metaaggregate drop every second frame */
    const char *path_first = "./metaaggregate_drop_t1.json";
    snprintf(command_first, sizeof(command_first),
             "filesrc location=%s ! identity eos-after=%d ! decodebin ! videoconvert ! tee name=t t. ! queue ! "
             "gvadrop pass-frames=1 drop-frames=1 ! gvametaaggregate name=a ! gvametaconvert format=json "
             "add_tensor_data=true ! gvametapublish file-path=%s method=file file-format=json-lines ! videoconvert ! "
             "fakesink sync=false t. ! queue ! gvadetect model=%s device=CPU ! a.",
             video_file_path, buf_num, path_first, model_path);
    check_run_pipeline(command_first, GST_SECOND);

    /* metaaggregate drop meta */
    const char *path_second = "./metaaggregate_drop_t2.json";
    snprintf(command_second, sizeof(command_second),
             "filesrc location=%s ! identity eos-after=%d ! decodebin ! videoconvert ! tee name=t t. ! queue ! "
             "gvametaaggregate name=a ! gvametaconvert format=json add_tensor_data=true ! gvametapublish file-path=%s "
             "method=file file-format=json-lines ! videoconvert ! fakesink sync=false t. ! queue ! "
             "gvadrop pass-frames=1 drop-frames=1 ! gvadetect model=%s device=CPU ! queue ! a.",
             video_file_path, buf_num, path_second, model_path);
    check_run_pipeline(command_second, GST_SECOND);

    // Need to reset the region_id field because it differs from launch to launch.
    reset_region_id(path_first);
    reset_region_id(path_second);

    /* compare first and second meta json files */
    int equal = compare_files(path_first, 1, path_second, 1);

    /* remove files */
    if (remove(path_first) != 0)
        printf(remove_eror, path_first);
    if (remove(path_second) != 0)
        printf(remove_eror, path_second);

    ck_assert_msg(equal, comparing_error, path_first, path_second);
}

GST_END_TEST;

typedef struct {
    gboolean test_passed;
    guint x;
    guint y;
    guint w;
    guint h;
    guint expected_roi_count;
} TestData;

// Testing callbacks
static void check_roi_scale(GstElement *fakesink, GstBuffer *buf, GstPad *pad, TestData *test_data) {
    if (buf == NULL) {
        test_data->test_passed &= FALSE;
        return;
    }

    guint count = 0;
    GstVideoRegionOfInterestMeta *meta = NULL;
    gpointer state = NULL;
    while ((meta = GST_VIDEO_REGION_OF_INTEREST_META_ITERATE(buf, &state))) {
        test_data->test_passed &= meta->x == test_data->x;
        test_data->test_passed &= meta->y == test_data->y;
        test_data->test_passed &= meta->w == test_data->w;
        test_data->test_passed &= meta->h == test_data->h;
        count += 1;
    }
    test_data->test_passed &= count == test_data->expected_roi_count;
}

static void check_roi_crop(GstElement *fakesink, GstBuffer *buf, GstPad *pad, TestData *test_data) {
    if (buf == NULL) {
        test_data->test_passed &= FALSE;
        return;
    }

    GstVideoInfo *vinfo = gst_video_info_new();
    fail_unless(gst_video_info_from_caps(vinfo, gst_pad_get_current_caps(pad)));

    GstVideoRegionOfInterestMeta *meta = NULL;
    gpointer state = NULL;
    guint count = 0;
    while ((meta = GST_VIDEO_REGION_OF_INTEREST_META_ITERATE(buf, &state))) {
        guint x1 = meta->x, x2 = meta->x + meta->w, y1 = meta->y, y2 = meta->y + meta->h;
        test_data->test_passed &= (x1 >= 0 && x1 <= vinfo->width);
        test_data->test_passed &= (x2 >= 0 && x2 <= vinfo->width);
        test_data->test_passed &= (y1 >= 0 && y1 <= vinfo->height);
        test_data->test_passed &= (y2 >= 0 && y2 <= vinfo->height);
        count += 1;
    }
    test_data->test_passed &= count == test_data->expected_roi_count;
    gst_video_info_free(vinfo);
}

static void count_buffers(GstElement *fakesink, GstBuffer *buffer, GstPad *pad, gpointer udata) {
    *((gint *)udata) += 1;
}

static GstPadProbeReturn break_buffer_duration(GstPad *pad, GstPadProbeInfo *info, gpointer user_data) {
    GstBuffer *buffer;
    buffer = GST_PAD_PROBE_INFO_BUFFER(info);
    if (!gst_buffer_is_writable(buffer))
        buffer = gst_buffer_make_writable(buffer);
    if (buffer == NULL)
        return GST_PAD_PROBE_OK;
    GST_BUFFER_DURATION(buffer) = GST_CLOCK_TIME_NONE;
    GST_PAD_PROBE_INFO_DATA(info) = buffer;
    return GST_PAD_PROBE_OK;
}

static GstPadProbeReturn break_buffer_timestamp(GstPad *pad, GstPadProbeInfo *info, gpointer user_data) {
    GstBuffer *buffer;
    buffer = GST_PAD_PROBE_INFO_BUFFER(info);
    if (!gst_buffer_is_writable(buffer))
        buffer = gst_buffer_make_writable(buffer);
    if (buffer == NULL)
        return GST_PAD_PROBE_OK;
    GST_BUFFER_TIMESTAMP(buffer) /= 2;
    GST_PAD_PROBE_INFO_DATA(info) = buffer;
    return GST_PAD_PROBE_OK;
}

static GstPadProbeReturn break_buffer_timestamp_2(GstPad *pad, GstPadProbeInfo *info, gpointer user_data) {
    GstBuffer *buffer;
    buffer = GST_PAD_PROBE_INFO_BUFFER(info);
    if (!gst_buffer_is_writable(buffer))
        buffer = gst_buffer_make_writable(buffer);
    if (buffer == NULL)
        return GST_PAD_PROBE_OK;
    GST_BUFFER_TIMESTAMP(buffer) = GST_CLOCK_TIME_NONE;
    GST_PAD_PROBE_INFO_DATA(info) = buffer;
    return GST_PAD_PROBE_OK;
}

static GstPadProbeReturn do_nothing(GstPad *pad, GstPadProbeInfo *info, gpointer user_data) {
    return GST_PAD_PROBE_OK;
}

// The function constructs the following pipeline:
// videotestsrc1 -> capsfilter1 -> gvaattachroi1 \
//                                                > gvametaaggregate -> fakesink
// videotestsrc2 -> capsfilter2 -> gvaattachroi2 /
// When fakesink receives a buffer, it calls `check_roi_scale` callback to check that meta was scaled properly
// and the number of ROI's is equals to expected.
static void test_metaaggregate_roi_scale_template(TestData test_data, GCallback check_results_callback,
                                                  const gchar *caps_string_1, const gchar *roi_string_1,
                                                  const gchar *caps_string_2, const gchar *roi_string_2) {
    GstBus *bus;
    GstMessage *msg;
    GstElement *pipeline, *src, *src1, *caps, *caps1, *roi, *roi1, *agg, *sink;

    pipeline = gst_pipeline_new("pipeline");

    src = gst_element_factory_make("videotestsrc", NULL);
    g_object_set(src, "num-buffers", 3, NULL);

    caps = gst_element_factory_make("capsfilter", NULL);
    g_object_set(caps, "caps", gst_caps_from_string(caps_string_1), NULL);

    roi = gst_element_factory_make("gvaattachroi", NULL);
    g_object_set(roi, "roi", roi_string_1, NULL);

    src1 = gst_element_factory_make("videotestsrc", NULL);
    g_object_set(src1, "num-buffers", 3, NULL);

    caps1 = gst_element_factory_make("capsfilter", NULL);
    g_object_set(caps1, "caps", gst_caps_from_string(caps_string_2), NULL);

    roi1 = gst_element_factory_make("gvaattachroi", NULL);
    g_object_set(roi1, "roi", roi_string_2, NULL);

    agg = gst_element_factory_make("gvametaaggregate", NULL);

    sink = gst_check_setup_element("fakesink");
    g_object_set(sink, "signal-handoffs", TRUE, NULL);
    g_signal_connect(sink, "handoff", (GCallback)check_results_callback, &test_data);

    fail_unless(gst_bin_add(GST_BIN(pipeline), src));
    fail_unless(gst_bin_add(GST_BIN(pipeline), src1));
    fail_unless(gst_bin_add(GST_BIN(pipeline), caps));
    fail_unless(gst_bin_add(GST_BIN(pipeline), caps1));
    fail_unless(gst_bin_add(GST_BIN(pipeline), roi));
    fail_unless(gst_bin_add(GST_BIN(pipeline), roi1));
    fail_unless(gst_bin_add(GST_BIN(pipeline), agg));
    fail_unless(gst_bin_add(GST_BIN(pipeline), sink));
    fail_unless(gst_element_link(src, caps));
    fail_unless(gst_element_link(caps, roi));
    fail_unless(gst_element_link(src1, caps1));
    fail_unless(gst_element_link(caps1, roi1));
    fail_unless(gst_element_link(roi, agg));
    fail_unless(gst_element_link(roi1, agg));
    fail_unless(gst_element_link(agg, sink));

    bus = gst_element_get_bus(pipeline);
    fail_if(bus == NULL);
    gst_element_set_state(pipeline, GST_STATE_PLAYING);

    msg = gst_bus_poll(bus, GST_MESSAGE_EOS | GST_MESSAGE_ERROR, -1);
    fail_if(GST_MESSAGE_TYPE(msg) != GST_MESSAGE_EOS);
    gst_message_unref(msg);

    fail_unless(test_data.test_passed);

    gst_element_set_state(pipeline, GST_STATE_NULL);
    gst_object_unref(bus);
    gst_object_unref(pipeline);
}

// Just construct a simple pipeline, pass callbacks to fakesrc and fakesink to configure the pipeline
// Pipeline: fakesrc -> gvametaaggregate -> fakesink
static void test_metaaggregate_buffer_template(GstPadProbeCallback fakesrc_callback, gpointer fakesrc_callback_args,
                                               GCallback fakesink_callback, gpointer fakesink_callback_args) {
    GstBus *bus;
    GstMessage *msg;
    GstElement *pipeline, *src, *agg, *sink;
    GstPad *pad;

    pipeline = gst_pipeline_new("pipeline");

    src = gst_element_factory_make("videotestsrc", NULL);
    g_object_set(src, "num-buffers", 10, NULL);

    agg = gst_element_factory_make("gvametaaggregate", NULL);

    sink = gst_element_factory_make("fakesink", NULL);
    g_object_set(sink, "signal-handoffs", TRUE, NULL);
    g_signal_connect(sink, "handoff", (GCallback)fakesink_callback, fakesink_callback_args);

    fail_unless(gst_bin_add(GST_BIN(pipeline), src));
    fail_unless(gst_bin_add(GST_BIN(pipeline), agg));
    fail_unless(gst_bin_add(GST_BIN(pipeline), sink));
    fail_unless(gst_element_link(src, agg));
    fail_unless(gst_element_link(agg, sink));

    pad = gst_element_get_static_pad(src, "src");
    gst_pad_add_probe(pad, GST_PAD_PROBE_TYPE_BUFFER, (GstPadProbeCallback)fakesrc_callback, fakesrc_callback_args,
                      NULL);
    gst_object_unref(pad);

    bus = gst_element_get_bus(pipeline);
    fail_if(bus == NULL);
    gst_element_set_state(pipeline, GST_STATE_PLAYING);

    msg = gst_bus_poll(bus, GST_MESSAGE_EOS | GST_MESSAGE_ERROR, -1);
    fail_if(GST_MESSAGE_TYPE(msg) != GST_MESSAGE_EOS);
    gst_message_unref(msg);

    gst_element_set_state(pipeline, GST_STATE_NULL);
    gst_object_unref(bus);
    gst_object_unref(pipeline);
}

GST_START_TEST(test_metaaggregate_roi_scale) {
    TestData test_case_1 = {.test_passed = TRUE, .x = 300, .y = 300, .w = 100, .h = 100, .expected_roi_count = 2};
    test_metaaggregate_roi_scale_template(test_case_1, G_CALLBACK(check_roi_scale), "video/x-raw,width=640,height=480",
                                          "300,300,400,400", "video/x-raw,width=320,height=240", "150,150,200,200");

    TestData test_case_2 = {.test_passed = TRUE, .x = 150, .y = 150, .w = 50, .h = 50, .expected_roi_count = 2};
    test_metaaggregate_roi_scale_template(test_case_2, G_CALLBACK(check_roi_scale), "video/x-raw,width=320,height=240",
                                          "150,150,200,200", "video/x-raw,width=640,height=480", "300,300,400,400");

    test_metaaggregate_roi_scale_template(test_case_2, G_CALLBACK(check_roi_crop), "video/x-raw,width=320,height=240",
                                          "150,150,200,200", "video/x-raw,width=640,height=480", "0,0,400,400");

    test_metaaggregate_roi_scale_template(test_case_2, G_CALLBACK(check_roi_crop), "video/x-raw,width=320,height=240",
                                          "150,150,200,200", "video/x-raw,width=640,height=480", "0,0,650,400");

    test_metaaggregate_roi_scale_template(test_case_2, G_CALLBACK(check_roi_crop), "video/x-raw,width=320,height=240",
                                          "150,150,200,200", "video/x-raw,width=640,height=480", "0,0,400,650");
}
GST_END_TEST;

GST_START_TEST(test_metaaggregate_buffer) {
    gint buffer_count = 0;
    test_metaaggregate_buffer_template(do_nothing, NULL, G_CALLBACK(count_buffers), &buffer_count);
    fail_if(buffer_count == 0);

    buffer_count = 0;
    test_metaaggregate_buffer_template(break_buffer_duration, NULL, G_CALLBACK(count_buffers), &buffer_count);
    fail_if(buffer_count == 0);

    buffer_count = 0;
    test_metaaggregate_buffer_template(break_buffer_timestamp, NULL, G_CALLBACK(count_buffers), &buffer_count);
    fail_if(buffer_count == 0);

    buffer_count = 0;
    test_metaaggregate_buffer_template(break_buffer_timestamp_2, NULL, G_CALLBACK(count_buffers), &buffer_count);
    fail_if(buffer_count != 0);
}
GST_END_TEST;

static Suite *metaaggregate_test_suite(void) {
    Suite *s = suite_create("metaaggregate_test");
    TCase *tc_chain = tcase_create("general");

    suite_add_tcase(s, tc_chain);
    tcase_add_test(tc_chain, test_metaaggregate_drop_frames);
    tcase_add_test(tc_chain, test_metaaggregate_drop_meta);
    tcase_add_test(tc_chain, test_metaaggregate_roi_scale);
    tcase_add_test(tc_chain, test_metaaggregate_buffer);

    return s;
}

GST_CHECK_MAIN(metaaggregate_test);
