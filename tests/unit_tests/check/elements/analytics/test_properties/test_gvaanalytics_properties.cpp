/*******************************************************************************
 * Copyright (C) 2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#include "test_common.h"
#include "test_utils.h"

/* ---------- element name under test ---------- */
constexpr char elem_name[] = "gvaanalytics";

/* ========================================================================= */
/*  Element instantiation tests                                              */
/* ========================================================================= */

GST_START_TEST(test_analytics_instantiation) {
    g_print("Starting test: test_analytics_instantiation\n");
    GstElement *element = gst_element_factory_make(elem_name, NULL);
    ck_assert_msg(element != NULL, "Failed to create element '%s'", elem_name);
    ck_assert(GST_IS_ELEMENT(element));
    gst_object_unref(element);
}
GST_END_TEST;

/* ========================================================================= */
/*  Default property value tests                                             */
/* ========================================================================= */

GST_START_TEST(test_default_config_property) {
    g_print("Starting test: test_default_config_property\n");
    GstElement *element = gst_check_setup_element(elem_name);
    ck_assert(element != NULL);

    gchar *config = NULL;
    g_object_get(G_OBJECT(element), "config", &config, NULL);
    ck_assert_msg(config == NULL, "Expected default config to be NULL, got '%s'", config);
    g_free(config);

    gst_check_teardown_element(element);
}
GST_END_TEST;

GST_START_TEST(test_default_zones_property) {
    g_print("Starting test: test_default_zones_property\n");
    GstElement *element = gst_check_setup_element(elem_name);
    ck_assert(element != NULL);

    gchar *zones = NULL;
    g_object_get(G_OBJECT(element), "zones", &zones, NULL);
    ck_assert_msg(zones == NULL, "Expected default zones to be NULL, got '%s'", zones);
    g_free(zones);

    gst_check_teardown_element(element);
}
GST_END_TEST;

GST_START_TEST(test_default_tripwires_property) {
    g_print("Starting test: test_default_tripwires_property\n");
    GstElement *element = gst_check_setup_element(elem_name);
    ck_assert(element != NULL);

    gchar *tripwires = NULL;
    g_object_get(G_OBJECT(element), "tripwires", &tripwires, NULL);
    ck_assert_msg(tripwires == NULL, "Expected default tripwires to be NULL, got '%s'", tripwires);
    g_free(tripwires);

    gst_check_teardown_element(element);
}
GST_END_TEST;

GST_START_TEST(test_default_draw_zones_property) {
    g_print("Starting test: test_default_draw_zones_property\n");
    GstElement *element = gst_check_setup_element(elem_name);
    ck_assert(element != NULL);

    gboolean draw_zones = FALSE;
    g_object_get(G_OBJECT(element), "draw-zones", &draw_zones, NULL);
    ck_assert_msg(draw_zones == TRUE, "Expected default draw-zones to be TRUE");

    gst_check_teardown_element(element);
}
GST_END_TEST;

GST_START_TEST(test_default_draw_tripwires_property) {
    g_print("Starting test: test_default_draw_tripwires_property\n");
    GstElement *element = gst_check_setup_element(elem_name);
    ck_assert(element != NULL);

    gboolean draw_tripwires = FALSE;
    g_object_get(G_OBJECT(element), "draw-tripwires", &draw_tripwires, NULL);
    ck_assert_msg(draw_tripwires == TRUE, "Expected default draw-tripwires to be TRUE");

    gst_check_teardown_element(element);
}
GST_END_TEST;

/* ========================================================================= */
/*  Property set/get round-trip tests                                        */
/* ========================================================================= */

GST_START_TEST(test_set_get_config) {
    g_print("Starting test: test_set_get_config\n");
    GValue prop_value = G_VALUE_INIT;
    g_value_init(&prop_value, G_TYPE_STRING);
    g_value_set_string(&prop_value, "/tmp/analytics.json");

    check_property_value_updated_correctly(elem_name, "config", prop_value);
    g_value_unset(&prop_value);
}
GST_END_TEST;

GST_START_TEST(test_set_get_zones_polygon) {
    g_print("Starting test: test_set_get_zones_polygon\n");
    GValue prop_value = G_VALUE_INIT;
    g_value_init(&prop_value, G_TYPE_STRING);
    g_value_set_string(&prop_value, "[{\"id\":\"zone1\",\"type\":\"polygon\","
                                    "\"points\":[{\"x\":100,\"y\":100},{\"x\":500,\"y\":100},"
                                    "{\"x\":500,\"y\":400},{\"x\":100,\"y\":400}]}]");

    check_property_value_updated_correctly(elem_name, "zones", prop_value);
    g_value_unset(&prop_value);
}
GST_END_TEST;

GST_START_TEST(test_set_get_zones_circle) {
    g_print("Starting test: test_set_get_zones_circle\n");
    GValue prop_value = G_VALUE_INIT;
    g_value_init(&prop_value, G_TYPE_STRING);
    g_value_set_string(&prop_value, "[{\"id\":\"zone_circle\",\"type\":\"circle\","
                                    "\"center\":{\"x\":320,\"y\":240},\"radius\":100}]");

    check_property_value_updated_correctly(elem_name, "zones", prop_value);
    g_value_unset(&prop_value);
}
GST_END_TEST;

GST_START_TEST(test_set_get_tripwires) {
    g_print("Starting test: test_set_get_tripwires\n");
    GValue prop_value = G_VALUE_INIT;
    g_value_init(&prop_value, G_TYPE_STRING);
    g_value_set_string(&prop_value, "[{\"id\":\"wire1\","
                                    "\"points\":[{\"x\":320,\"y\":0},{\"x\":320,\"y\":480}]}]");

    check_property_value_updated_correctly(elem_name, "tripwires", prop_value);
    g_value_unset(&prop_value);
}
GST_END_TEST;

GST_START_TEST(test_set_get_draw_zones_false) {
    g_print("Starting test: test_set_get_draw_zones_false\n");
    GValue prop_value = G_VALUE_INIT;
    g_value_init(&prop_value, G_TYPE_BOOLEAN);
    g_value_set_boolean(&prop_value, FALSE);

    check_property_value_updated_correctly(elem_name, "draw-zones", prop_value);
    g_value_unset(&prop_value);
}
GST_END_TEST;

GST_START_TEST(test_set_get_draw_zones_true) {
    g_print("Starting test: test_set_get_draw_zones_true\n");
    GValue prop_value = G_VALUE_INIT;
    g_value_init(&prop_value, G_TYPE_BOOLEAN);
    g_value_set_boolean(&prop_value, TRUE);

    check_property_value_updated_correctly(elem_name, "draw-zones", prop_value);
    g_value_unset(&prop_value);
}
GST_END_TEST;

GST_START_TEST(test_set_get_draw_tripwires_false) {
    g_print("Starting test: test_set_get_draw_tripwires_false\n");
    GValue prop_value = G_VALUE_INIT;
    g_value_init(&prop_value, G_TYPE_BOOLEAN);
    g_value_set_boolean(&prop_value, FALSE);

    check_property_value_updated_correctly(elem_name, "draw-tripwires", prop_value);
    g_value_unset(&prop_value);
}
GST_END_TEST;

GST_START_TEST(test_set_get_draw_tripwires_true) {
    g_print("Starting test: test_set_get_draw_tripwires_true\n");
    GValue prop_value = G_VALUE_INIT;
    g_value_init(&prop_value, G_TYPE_BOOLEAN);
    g_value_set_boolean(&prop_value, TRUE);

    check_property_value_updated_correctly(elem_name, "draw-tripwires", prop_value);
    g_value_unset(&prop_value);
}
GST_END_TEST;

/* ========================================================================= */
/*  State transition tests                                                   */
/* ========================================================================= */

GST_START_TEST(test_analytics_state_null_to_ready) {
    g_print("Starting test: test_analytics_state_null_to_ready\n");
    GstElement *element = gst_element_factory_make(elem_name, NULL);
    ck_assert(element != NULL);

    GstStateChangeReturn ret = gst_element_set_state(element, GST_STATE_READY);
    ck_assert_msg(ret != GST_STATE_CHANGE_FAILURE, "Failed to transition to READY state");

    ret = gst_element_set_state(element, GST_STATE_NULL);
    ck_assert_msg(ret != GST_STATE_CHANGE_FAILURE, "Failed to transition back to NULL state");

    gst_object_unref(element);
}
GST_END_TEST;

GST_START_TEST(test_analytics_state_null_to_ready_with_zones) {
    g_print("Starting test: test_analytics_state_null_to_ready_with_zones\n");
    GstElement *element = gst_element_factory_make(elem_name, NULL);
    ck_assert(element != NULL);

    g_object_set(G_OBJECT(element), "zones",
                 "[{\"id\":\"zone1\",\"type\":\"polygon\","
                 "\"points\":[{\"x\":0,\"y\":0},{\"x\":100,\"y\":0},"
                 "{\"x\":100,\"y\":100},{\"x\":0,\"y\":100}]}]",
                 NULL);

    GstStateChangeReturn ret = gst_element_set_state(element, GST_STATE_READY);
    ck_assert_msg(ret != GST_STATE_CHANGE_FAILURE, "Failed to transition to READY state with zones");

    ret = gst_element_set_state(element, GST_STATE_NULL);
    ck_assert_msg(ret != GST_STATE_CHANGE_FAILURE, "Failed to transition back to NULL state");

    gst_object_unref(element);
}
GST_END_TEST;

GST_START_TEST(test_analytics_state_null_to_ready_with_tripwires) {
    g_print("Starting test: test_analytics_state_null_to_ready_with_tripwires\n");
    GstElement *element = gst_element_factory_make(elem_name, NULL);
    ck_assert(element != NULL);

    g_object_set(G_OBJECT(element), "tripwires",
                 "[{\"id\":\"wire1\","
                 "\"points\":[{\"x\":320,\"y\":0},{\"x\":320,\"y\":480}]}]",
                 NULL);

    GstStateChangeReturn ret = gst_element_set_state(element, GST_STATE_READY);
    ck_assert_msg(ret != GST_STATE_CHANGE_FAILURE, "Failed to transition to READY state with tripwires");

    ret = gst_element_set_state(element, GST_STATE_NULL);
    ck_assert_msg(ret != GST_STATE_CHANGE_FAILURE, "Failed to transition back to NULL state");

    gst_object_unref(element);
}
GST_END_TEST;

/* ========================================================================= */
/*  Suite setup                                                              */
/* ========================================================================= */

static Suite *analytics_properties_testing_suite(void) {
    Suite *s = suite_create("analytics_properties_testing");

    /* instantiation */
    TCase *tc_instantiation = tcase_create("instantiation");
    suite_add_tcase(s, tc_instantiation);
    tcase_add_test(tc_instantiation, test_analytics_instantiation);

    /* default property values */
    TCase *tc_defaults = tcase_create("default_properties");
    suite_add_tcase(s, tc_defaults);
    tcase_add_test(tc_defaults, test_default_config_property);
    tcase_add_test(tc_defaults, test_default_zones_property);
    tcase_add_test(tc_defaults, test_default_tripwires_property);
    tcase_add_test(tc_defaults, test_default_draw_zones_property);
    tcase_add_test(tc_defaults, test_default_draw_tripwires_property);

    /* property set/get round-trip */
    TCase *tc_setget = tcase_create("property_set_get");
    suite_add_tcase(s, tc_setget);
    tcase_add_test(tc_setget, test_set_get_config);
    tcase_add_test(tc_setget, test_set_get_zones_polygon);
    tcase_add_test(tc_setget, test_set_get_zones_circle);
    tcase_add_test(tc_setget, test_set_get_tripwires);
    tcase_add_test(tc_setget, test_set_get_draw_zones_false);
    tcase_add_test(tc_setget, test_set_get_draw_tripwires_false);

    /* state transitions */
    TCase *tc_states = tcase_create("state_transitions");
    suite_add_tcase(s, tc_states);
    tcase_add_test(tc_states, test_analytics_state_null_to_ready);
    tcase_add_test(tc_states, test_analytics_state_null_to_ready_with_zones);
    tcase_add_test(tc_states, test_analytics_state_null_to_ready_with_tripwires);

    return s;
}

GST_CHECK_MAIN(analytics_properties_testing);
