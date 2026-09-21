/*******************************************************************************
 * Copyright (C) 2018-2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#include "test_common.h"
#include "test_utils.h"

constexpr char plugin_name[] = "gvadetect";

static GstStaticPadTemplate srctemplate =
    GST_STATIC_PAD_TEMPLATE("src", GST_PAD_SRC, GST_PAD_ALWAYS, GST_STATIC_CAPS(VIDEO_CAPS_TEMPLATE_STRING));

static GstStaticPadTemplate sinktemplate =
    GST_STATIC_PAD_TEMPLATE("sink", GST_PAD_SINK, GST_PAD_ALWAYS, GST_STATIC_CAPS(VIDEO_CAPS_TEMPLATE_STRING));

GST_START_TEST(test_model_property_invalid_path) {
    g_print("Starting test: test_model_property_invalid_path\n");
    std::string prop_value = "/a/non/existent/file.xml";
    std::string expected_msg = "Error loading xmlfile: " + prop_value;

    check_multiple_property_init_fail_if_invalid_value(plugin_name, &srctemplate, &sinktemplate, expected_msg.c_str(),
                                                       "model", prop_value.c_str(), NULL);
}

GST_END_TEST;

GST_START_TEST(test_model_proc_property_invalid_path) {
    g_print("Starting test: test_model_proc_property_invalid_path\n");
    std::string prop_value = "/a/non/existent/file.json";
    std::string expected_msg = "Error loading json file: " + prop_value;

    char model_path[MAX_STR_PATH_SIZE];
    ExitStatus status = get_model_path(model_path, MAX_STR_PATH_SIZE, "centerface", "FP32");
    ck_assert(status == EXIT_STATUS_SUCCESS);

    check_multiple_property_init_fail_if_invalid_value(plugin_name, &srctemplate, &sinktemplate, expected_msg.c_str(),
                                                       "model", model_path, "model-proc", prop_value.c_str(), NULL);
}

GST_END_TEST;

GST_START_TEST(test_batch_size_property_less_zero) {
    g_print("Starting test: test_batch_size_property_less_zero\n");
    GValue prop_value = G_VALUE_INIT;
    g_value_init(&prop_value, G_TYPE_INT);
    g_value_set_int(&prop_value, -1);

    check_property_default_if_invalid_value(plugin_name, "batch-size", prop_value);
}

GST_END_TEST;

GST_START_TEST(test_nireq_property_less_zero) {
    g_print("Starting test: test_nireq_property_less_zero\n");
    GValue prop_value = G_VALUE_INIT;
    g_value_init(&prop_value, G_TYPE_INT);
    g_value_set_int(&prop_value, -1);

    check_property_default_if_invalid_value(plugin_name, "nireq", prop_value);
}

GST_END_TEST;

GST_START_TEST(test_qos_property_str_trash) {
    g_print("Starting test: test_qos_property_str_trash\n");
    GValue prop_value = G_VALUE_INIT;
    g_value_init(&prop_value, G_TYPE_STRING);
    g_value_set_string(&prop_value, "true");

    check_property_default_if_invalid_value(plugin_name, "qos", prop_value);
}

GST_END_TEST;

static Suite *inference_properties_testing_suite(void) {
    Suite *s = suite_create("inference_properties_testing");
    TCase *tc_chain = tcase_create("general");

    suite_add_tcase(s, tc_chain);
    // tcase_add_test(tc_chain, test_model_property_invalid_path);
    // tcase_add_test(tc_chain, test_model_proc_property_invalid_path);
    tcase_add_test(tc_chain, test_batch_size_property_less_zero);
    tcase_add_test(tc_chain, test_nireq_property_less_zero);
    tcase_add_test(tc_chain, test_qos_property_str_trash);

    return s;
}

GST_CHECK_MAIN(inference_properties_testing);
