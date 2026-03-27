/*******************************************************************************
 * Copyright (C) 2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#include "test_common.h"
#include "test_utils.h"

/* ---------- element names under test ---------- */
constexpr char impl_name[] = "gvawatermarkimpl";
constexpr char bin_name[] = "gvawatermark";

/* ========================================================================= */
/*  Element instantiation tests                                              */
/* ========================================================================= */

GST_START_TEST(test_watermarkimpl_instantiation) {
    g_print("Starting test: test_watermarkimpl_instantiation\n");
    GstElement *element = gst_element_factory_make(impl_name, NULL);
    ck_assert_msg(element != NULL, "Failed to create element '%s'", impl_name);
    ck_assert(GST_IS_ELEMENT(element));
    gst_object_unref(element);
}
GST_END_TEST;

GST_START_TEST(test_watermark_bin_instantiation) {
    g_print("Starting test: test_watermark_bin_instantiation\n");
    GstElement *element = gst_element_factory_make(bin_name, NULL);
    ck_assert_msg(element != NULL, "Failed to create element '%s'", bin_name);
    ck_assert(GST_IS_ELEMENT(element));
    ck_assert(GST_IS_BIN(element));
    gst_object_unref(element);
}
GST_END_TEST;

/* ========================================================================= */
/*  Default property value tests                                             */
/* ========================================================================= */

GST_START_TEST(test_default_device_property) {
    g_print("Starting test: test_default_device_property\n");
    GstElement *element = gst_check_setup_element(impl_name);
    ck_assert(element != NULL);

    gchar *device = NULL;
    g_object_get(G_OBJECT(element), "device", &device, NULL);
    ck_assert_msg(device == NULL, "Expected default device to be NULL, got '%s'", device);
    g_free(device);

    gst_check_teardown_element(element);
}
GST_END_TEST;

GST_START_TEST(test_default_obb_property) {
    g_print("Starting test: test_default_obb_property\n");
    GstElement *element = gst_check_setup_element(impl_name);
    ck_assert(element != NULL);

    gboolean obb = TRUE;
    g_object_get(G_OBJECT(element), "obb", &obb, NULL);
    ck_assert_msg(obb == FALSE, "Expected default obb to be FALSE");

    gst_check_teardown_element(element);
}
GST_END_TEST;

GST_START_TEST(test_default_displ_avgfps_property) {
    g_print("Starting test: test_default_displ_avgfps_property\n");
    GstElement *element = gst_check_setup_element(impl_name);
    ck_assert(element != NULL);

    gboolean displ_avgfps = TRUE;
    g_object_get(G_OBJECT(element), "displ-avgfps", &displ_avgfps, NULL);
    ck_assert_msg(displ_avgfps == FALSE, "Expected default displ-avgfps to be FALSE");

    gst_check_teardown_element(element);
}
GST_END_TEST;

GST_START_TEST(test_default_displ_cfg_property) {
    g_print("Starting test: test_default_displ_cfg_property\n");
    GstElement *element = gst_check_setup_element(impl_name);
    ck_assert(element != NULL);

    gchar *displ_cfg = NULL;
    g_object_get(G_OBJECT(element), "displ-cfg", &displ_cfg, NULL);
    ck_assert_msg(displ_cfg == NULL, "Expected default displ-cfg to be NULL, got '%s'", displ_cfg);
    g_free(displ_cfg);

    gst_check_teardown_element(element);
}
GST_END_TEST;

/* ========================================================================= */
/*  Property set/get round-trip tests                                        */
/* ========================================================================= */

GST_START_TEST(test_set_get_device_cpu) {
    g_print("Starting test: test_set_get_device_cpu\n");
    GValue prop_value = G_VALUE_INIT;
    g_value_init(&prop_value, G_TYPE_STRING);
    g_value_set_string(&prop_value, "CPU");

    check_property_value_updated_correctly(impl_name, "device", prop_value);
    g_value_unset(&prop_value);
}
GST_END_TEST;

GST_START_TEST(test_set_get_device_gpu) {
    g_print("Starting test: test_set_get_device_gpu\n");
    GValue prop_value = G_VALUE_INIT;
    g_value_init(&prop_value, G_TYPE_STRING);
    g_value_set_string(&prop_value, "GPU");

    check_property_value_updated_correctly(impl_name, "device", prop_value);
    g_value_unset(&prop_value);
}
GST_END_TEST;

GST_START_TEST(test_set_get_obb_true) {
    g_print("Starting test: test_set_get_obb_true\n");
    GValue prop_value = G_VALUE_INIT;
    g_value_init(&prop_value, G_TYPE_BOOLEAN);
    g_value_set_boolean(&prop_value, TRUE);

    check_property_value_updated_correctly(impl_name, "obb", prop_value);
    g_value_unset(&prop_value);
}
GST_END_TEST;

GST_START_TEST(test_set_get_displ_avgfps_true) {
    g_print("Starting test: test_set_get_displ_avgfps_true\n");
    GValue prop_value = G_VALUE_INIT;
    g_value_init(&prop_value, G_TYPE_BOOLEAN);
    g_value_set_boolean(&prop_value, TRUE);

    check_property_value_updated_correctly(impl_name, "displ-avgfps", prop_value);
    g_value_unset(&prop_value);
}
GST_END_TEST;

GST_START_TEST(test_set_get_displ_cfg) {
    g_print("Starting test: test_set_get_displ_cfg\n");
    GValue prop_value = G_VALUE_INIT;
    g_value_init(&prop_value, G_TYPE_STRING);
    g_value_set_string(&prop_value, "show-labels=false,thickness=3");

    check_property_value_updated_correctly(impl_name, "displ-cfg", prop_value);
    g_value_unset(&prop_value);
}
GST_END_TEST;

GST_START_TEST(test_set_get_displ_cfg_full) {
    g_print("Starting test: test_set_get_displ_cfg_full\n");
    GValue prop_value = G_VALUE_INIT;
    g_value_init(&prop_value, G_TYPE_STRING);
    g_value_set_string(&prop_value, "show-labels=true,font-scale=1.0,thickness=5,color-idx=2,font-type=simplex");

    check_property_value_updated_correctly(impl_name, "displ-cfg", prop_value);
    g_value_unset(&prop_value);
}
GST_END_TEST;

/* ========================================================================= */
/*  State transition tests                                                   */
/* ========================================================================= */

GST_START_TEST(test_watermarkimpl_state_null_to_ready) {
    g_print("Starting test: test_watermarkimpl_state_null_to_ready\n");
    GstElement *element = gst_element_factory_make(impl_name, NULL);
    ck_assert(element != NULL);

    GstStateChangeReturn ret = gst_element_set_state(element, GST_STATE_READY);
    ck_assert_msg(ret != GST_STATE_CHANGE_FAILURE, "Failed to transition to READY state");

    ret = gst_element_set_state(element, GST_STATE_NULL);
    ck_assert_msg(ret != GST_STATE_CHANGE_FAILURE, "Failed to transition back to NULL state");

    gst_object_unref(element);
}
GST_END_TEST;

GST_START_TEST(test_watermark_bin_state_null_to_ready) {
    g_print("Starting test: test_watermark_bin_state_null_to_ready\n");
    GstElement *element = gst_element_factory_make(bin_name, NULL);
    ck_assert(element != NULL);

    GstStateChangeReturn ret = gst_element_set_state(element, GST_STATE_READY);
    ck_assert_msg(ret != GST_STATE_CHANGE_FAILURE, "Failed to transition bin to READY state");

    ret = gst_element_set_state(element, GST_STATE_NULL);
    ck_assert_msg(ret != GST_STATE_CHANGE_FAILURE, "Failed to transition bin back to NULL state");

    gst_object_unref(element);
}
GST_END_TEST;

/* ========================================================================= */
/*  Bin wrapper property forwarding tests                                    */
/* ========================================================================= */

GST_START_TEST(test_bin_set_get_device) {
    g_print("Starting test: test_bin_set_get_device\n");
    GstElement *element = gst_element_factory_make(bin_name, NULL);
    ck_assert(element != NULL);

    g_object_set(G_OBJECT(element), "device", "CPU", NULL);
    gchar *device = NULL;
    g_object_get(G_OBJECT(element), "device", &device, NULL);
    ck_assert_str_eq(device, "CPU");
    g_free(device);

    gst_object_unref(element);
}
GST_END_TEST;

GST_START_TEST(test_bin_set_get_obb) {
    g_print("Starting test: test_bin_set_get_obb\n");
    GstElement *element = gst_element_factory_make(bin_name, NULL);
    ck_assert(element != NULL);

    g_object_set(G_OBJECT(element), "obb", TRUE, NULL);
    gboolean obb = FALSE;
    g_object_get(G_OBJECT(element), "obb", &obb, NULL);
    ck_assert(obb == TRUE);

    gst_object_unref(element);
}
GST_END_TEST;

GST_START_TEST(test_bin_set_get_displ_avgfps) {
    g_print("Starting test: test_bin_set_get_displ_avgfps\n");
    GstElement *element = gst_element_factory_make(bin_name, NULL);
    ck_assert(element != NULL);

    g_object_set(G_OBJECT(element), "displ-avgfps", TRUE, NULL);
    gboolean displ_avgfps = FALSE;
    g_object_get(G_OBJECT(element), "displ-avgfps", &displ_avgfps, NULL);
    ck_assert(displ_avgfps == TRUE);

    gst_object_unref(element);
}
GST_END_TEST;

GST_START_TEST(test_bin_set_get_displ_cfg) {
    g_print("Starting test: test_bin_set_get_displ_cfg\n");
    GstElement *element = gst_element_factory_make(bin_name, NULL);
    ck_assert(element != NULL);

    g_object_set(G_OBJECT(element), "displ-cfg", "show-labels=false", NULL);
    gchar *displ_cfg = NULL;
    g_object_get(G_OBJECT(element), "displ-cfg", &displ_cfg, NULL);
    ck_assert_str_eq(displ_cfg, "show-labels=false");
    g_free(displ_cfg);

    gst_object_unref(element);
}
GST_END_TEST;

/* ========================================================================= */
/*  Suite setup                                                              */
/* ========================================================================= */

static Suite *watermark_properties_testing_suite(void) {
    Suite *s = suite_create("watermark_properties_testing");

    /* instantiation */
    TCase *tc_instantiation = tcase_create("instantiation");
    suite_add_tcase(s, tc_instantiation);
    tcase_add_test(tc_instantiation, test_watermarkimpl_instantiation);
    tcase_add_test(tc_instantiation, test_watermark_bin_instantiation);

    /* default property values */
    TCase *tc_defaults = tcase_create("default_properties");
    suite_add_tcase(s, tc_defaults);
    tcase_add_test(tc_defaults, test_default_device_property);
    tcase_add_test(tc_defaults, test_default_obb_property);
    tcase_add_test(tc_defaults, test_default_displ_avgfps_property);
    tcase_add_test(tc_defaults, test_default_displ_cfg_property);

    /* property set/get round-trip */
    TCase *tc_setget = tcase_create("property_set_get");
    suite_add_tcase(s, tc_setget);
    tcase_add_test(tc_setget, test_set_get_device_cpu);
    tcase_add_test(tc_setget, test_set_get_device_gpu);
    tcase_add_test(tc_setget, test_set_get_obb_true);
    tcase_add_test(tc_setget, test_set_get_displ_avgfps_true);
    tcase_add_test(tc_setget, test_set_get_displ_cfg);
    tcase_add_test(tc_setget, test_set_get_displ_cfg_full);

    /* state transitions */
    TCase *tc_states = tcase_create("state_transitions");
    suite_add_tcase(s, tc_states);
    tcase_add_test(tc_states, test_watermarkimpl_state_null_to_ready);
    tcase_add_test(tc_states, test_watermark_bin_state_null_to_ready);

    /* bin property forwarding */
    TCase *tc_bin = tcase_create("bin_property_forwarding");
    suite_add_tcase(s, tc_bin);
    tcase_add_test(tc_bin, test_bin_set_get_device);
    tcase_add_test(tc_bin, test_bin_set_get_obb);
    tcase_add_test(tc_bin, test_bin_set_get_displ_avgfps);
    tcase_add_test(tc_bin, test_bin_set_get_displ_cfg);

    return s;
}

GST_CHECK_MAIN(watermark_properties_testing);
