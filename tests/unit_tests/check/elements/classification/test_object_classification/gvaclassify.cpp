/*******************************************************************************
 * Copyright (C) 2018-2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#include "gva_utils.h"
#include "region_of_interest.h"
#include "tensor.h"
#include "test_common.h"
#include "test_utils.h"
#include "video_frame.h"

#include <gst/video/video.h>
#include <unordered_map>

static GstStaticPadTemplate srctemplate =
    GST_STATIC_PAD_TEMPLATE("src", GST_PAD_SRC, GST_PAD_ALWAYS, GST_STATIC_CAPS(VIDEO_CAPS_TEMPLATE_STRING));

static GstStaticPadTemplate sinktemplate =
    GST_STATIC_PAD_TEMPLATE("sink", GST_PAD_SINK, GST_PAD_ALWAYS, GST_STATIC_CAPS(VIDEO_CAPS_TEMPLATE_STRING));

GST_START_TEST(test_model_proc_property_json_does_not_match_schema) {
    g_print("Starting test: test_model_proc_property_json_does_not_match_schema\n");

    std::string prop_value = "classification_test_files/invalid_model_schema.json";

    char model_path[MAX_STR_PATH_SIZE];
    ExitStatus status =
        get_model_path(model_path, MAX_STR_PATH_SIZE, "dima806_fairface_gender_image_detection", "FP32");
    ck_assert(status == EXIT_STATUS_SUCCESS);
    check_bus_for_error("gvaclassify", &srctemplate, &sinktemplate, "", GST_LIBRARY_ERROR, GST_LIBRARY_ERROR_INIT,
                        "model", model_path, "model-proc", prop_value.c_str(), NULL);
}

GST_END_TEST;

static Suite *classification_suite(void) {
    Suite *s = suite_create("classification");
    TCase *tc_chain = tcase_create("general");

    suite_add_tcase(s, tc_chain);
    tcase_add_test(tc_chain, test_model_proc_property_json_does_not_match_schema);

    return s;
}

GST_CHECK_MAIN(classification);
