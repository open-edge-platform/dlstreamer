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

/* ========================================================================= */
/*  Properties that can be changed while the pipeline is running             */
/*                                                                           */
/*  These are inference-interval and, on gvadetect, threshold. Nothing else  */
/*  is declared changeable while running.                                    */
/* ========================================================================= */

GST_START_TEST(test_only_interval_and_threshold_are_changeable_while_running) {
    g_print("Starting test: test_only_interval_and_threshold_are_changeable_while_running\n");
    for (const char *element_name : {"gvainference", "gvadetect", "gvaclassify"}) {
        GstElement *element = gst_element_factory_make(element_name, NULL);
        ck_assert_msg(element != NULL, "Cannot create %s", element_name);

        guint n_props = 0;
        GParamSpec **props = g_object_class_list_properties(G_OBJECT_GET_CLASS(element), &n_props);
        for (guint i = 0; i < n_props; i++) {
            const GParamSpec *pspec = props[i];
            /* only the properties added by the gva elements, not those of GstObject/GstBaseTransform */
            if (pspec->owner_type == GST_TYPE_BASE_TRANSFORM ||
                !g_type_is_a(pspec->owner_type, GST_TYPE_BASE_TRANSFORM))
                continue;

            const bool expected = g_str_equal(pspec->name, "inference-interval") ||
                                  (g_str_equal(element_name, "gvadetect") && g_str_equal(pspec->name, "threshold"));
            const bool changeable = pspec->flags & GST_PARAM_MUTABLE_PLAYING;
            ck_assert_msg(changeable == expected, "%s: property '%s' should %sbe changeable while PLAYING",
                          element_name, pspec->name, expected ? "" : "not ");
        }
        g_free(props);
        gst_object_unref(element);
    }
}
GST_END_TEST;

/* ========================================================================= */
/*  Writing properties while the pipeline is running                         */
/*                                                                           */
/*  The state of the element is set directly, which is enough for the checks */
/*  of the properties, and does not need a model.                            */
/* ========================================================================= */

static void force_state(GstElement *element, GstState state) {
    GST_OBJECT_LOCK(element);
    GST_STATE(element) = state;
    GST_OBJECT_UNLOCK(element);
}

/* Status of the "set-runtime-property" signal, as in GvaPropertyStatus */
enum { GVA_PROPERTY_APPLIED = 0, GVA_PROPERTY_NOT_FOUND, GVA_PROPERTY_NOT_MUTABLE, GVA_PROPERTY_INVALID_VALUE };

static gint emit_set_runtime_property(GstElement *element, const char *name, const char *value) {
    GstStructure *result = NULL;
    g_signal_emit_by_name(element, "set-runtime-property", name, value, &result);
    ck_assert_msg(result != NULL, "no result for '%s'", name);
    gint status = -1;
    ck_assert(gst_structure_get_int(result, "status", &status));
    ck_assert(gst_structure_get_string(result, "code") != NULL);
    ck_assert(gst_structure_get_string(result, "message") != NULL);
    gst_structure_free(result);
    return status;
}

static gfloat get_float(GstElement *element, const char *name) {
    gfloat value = 0;
    g_object_get(element, name, &value, NULL);
    return value;
}

static guint get_uint(GstElement *element, const char *name) {
    guint value = 0;
    g_object_get(element, name, &value, NULL);
    return value;
}

static gchar *get_string(GstElement *element, const char *name) {
    gchar *value = NULL;
    g_object_get(element, name, &value, NULL);
    return value;
}

GST_START_TEST(test_write_of_mutable_properties_while_running) {
    g_print("Starting test: test_write_of_mutable_properties_while_running\n");
    GstElement *element = gst_element_factory_make("gvadetect", NULL);
    ck_assert(element != NULL);
    force_state(element, GST_STATE_PLAYING);

    g_object_set(element, "threshold", 0.3f, "inference-interval", 2u, NULL);
    ck_assert(get_float(element, "threshold") == 0.3f);
    ck_assert_uint_eq(get_uint(element, "inference-interval"), 2);

    force_state(element, GST_STATE_NULL);
    gst_object_unref(element);
}
GST_END_TEST;

GST_START_TEST(test_write_of_other_properties_while_running_is_refused) {
    g_print("Starting test: test_write_of_other_properties_while_running_is_refused\n");
    GstElement *element = gst_element_factory_make("gvadetect", NULL);
    ck_assert(element != NULL);
    g_object_set(element, "device", "CPU", "batch-size", 2u, NULL);
    force_state(element, GST_STATE_PLAYING);

    ASSERT_WARNING(g_object_set(element, "device", "GPU", NULL));
    ASSERT_WARNING(g_object_set(element, "batch-size", 8u, NULL));

    gchar *device = get_string(element, "device");
    ck_assert_str_eq(device, "CPU");
    g_free(device);
    ck_assert_uint_eq(get_uint(element, "batch-size"), 2);

    force_state(element, GST_STATE_NULL);
    gst_object_unref(element);
}
GST_END_TEST;

GST_START_TEST(test_write_of_properties_by_state) {
    g_print("Starting test: test_write_of_properties_by_state\n");
    GstElement *element = gst_element_factory_make("gvadetect", NULL);
    ck_assert(element != NULL);

    // Before the element starts, everything can be changed
    for (GstState state : {GST_STATE_NULL, GST_STATE_READY}) {
        force_state(element, state);
        g_object_set(element, "device", "GPU", NULL);
        g_object_set(element, "device", "CPU", NULL);
    }

    // Afterwards only the properties that are flagged changeable while running
    for (GstState state : {GST_STATE_PAUSED, GST_STATE_PLAYING}) {
        force_state(element, state);
        ASSERT_WARNING(g_object_set(element, "device", "GPU", NULL));
        g_object_set(element, "threshold", 0.4f, NULL);
        ck_assert(get_float(element, "threshold") == 0.4f);
    }

    // A transition to PLAYING that is still going on counts as PLAYING
    force_state(element, GST_STATE_READY);
    GST_OBJECT_LOCK(element);
    GST_STATE_PENDING(element) = GST_STATE_PLAYING;
    GST_OBJECT_UNLOCK(element);
    ASSERT_WARNING(g_object_set(element, "device", "GPU", NULL));

    GST_OBJECT_LOCK(element);
    GST_STATE_PENDING(element) = GST_STATE_VOID_PENDING;
    GST_OBJECT_UNLOCK(element);
    force_state(element, GST_STATE_NULL);
    gst_object_unref(element);
}
GST_END_TEST;

GST_START_TEST(test_write_while_running_on_other_elements) {
    g_print("Starting test: test_write_while_running_on_other_elements\n");
    GstElement *inference = gst_element_factory_make("gvainference", NULL);
    GstElement *classify = gst_element_factory_make("gvaclassify", NULL);
    ck_assert(inference != NULL && classify != NULL);
    force_state(inference, GST_STATE_PLAYING);
    force_state(classify, GST_STATE_PLAYING);

    g_object_set(inference, "inference-interval", 3u, NULL);
    ck_assert_uint_eq(get_uint(inference, "inference-interval"), 3);
    g_object_set(classify, "inference-interval", 4u, NULL);
    ck_assert_uint_eq(get_uint(classify, "inference-interval"), 4);
    ASSERT_WARNING(g_object_set(classify, "reclassify-interval", 4u, NULL));
    ck_assert_uint_eq(get_uint(classify, "reclassify-interval"), 1);

    force_state(inference, GST_STATE_NULL);
    force_state(classify, GST_STATE_NULL);
    gst_object_unref(inference);
    gst_object_unref(classify);
}
GST_END_TEST;

/* ========================================================================= */
/*  The "set-runtime-property" signal                                        */
/* ========================================================================= */

GST_START_TEST(test_signal_applies_mutable_properties) {
    g_print("Starting test: test_signal_applies_mutable_properties\n");
    GstElement *element = gst_element_factory_make("gvadetect", NULL);
    ck_assert(element != NULL);
    force_state(element, GST_STATE_PLAYING);

    ck_assert_int_eq(emit_set_runtime_property(element, "threshold", "0.8"), GVA_PROPERTY_APPLIED);
    ck_assert(get_float(element, "threshold") == 0.8f);
    ck_assert_int_eq(emit_set_runtime_property(element, "inference-interval", "5"), GVA_PROPERTY_APPLIED);
    ck_assert_uint_eq(get_uint(element, "inference-interval"), 5);

    force_state(element, GST_STATE_NULL);
    gst_object_unref(element);
}
GST_END_TEST;

GST_START_TEST(test_signal_refuses_invalid_value) {
    g_print("Starting test: test_signal_refuses_invalid_value\n");
    GstElement *element = gst_element_factory_make("gvadetect", NULL);
    ck_assert(element != NULL);
    g_object_set(element, "threshold", 0.8f, "inference-interval", 5u, NULL);
    force_state(element, GST_STATE_PLAYING);

    for (const char *value : {"1.5", "-0.1", "abc", "nan", ""})
        ck_assert_msg(emit_set_runtime_property(element, "threshold", value) == GVA_PROPERTY_INVALID_VALUE,
                      "threshold '%s' should be refused", value);
    for (const char *value : {"0", "-1", "1.5", "abc", "", "4294967296"})
        ck_assert_msg(emit_set_runtime_property(element, "inference-interval", value) == GVA_PROPERTY_INVALID_VALUE,
                      "inference-interval '%s' should be refused", value);
    ck_assert_int_eq(emit_set_runtime_property(element, "threshold", NULL), GVA_PROPERTY_INVALID_VALUE);

    // Nothing has changed
    ck_assert(get_float(element, "threshold") == 0.8f);
    ck_assert_uint_eq(get_uint(element, "inference-interval"), 5);

    force_state(element, GST_STATE_NULL);
    gst_object_unref(element);
}
GST_END_TEST;

GST_START_TEST(test_signal_refuses_other_properties) {
    g_print("Starting test: test_signal_refuses_other_properties\n");
    GstElement *element = gst_element_factory_make("gvadetect", NULL);
    ck_assert(element != NULL);
    g_object_set(element, "device", "CPU", "batch-size", 2u, NULL);
    force_state(element, GST_STATE_PLAYING);

    ck_assert_int_eq(emit_set_runtime_property(element, "nonexistent", "1"), GVA_PROPERTY_NOT_FOUND);
    ck_assert_int_eq(emit_set_runtime_property(element, NULL, "1"), GVA_PROPERTY_NOT_FOUND);
    ck_assert_int_eq(emit_set_runtime_property(element, "device", "GPU"), GVA_PROPERTY_NOT_MUTABLE);
    ck_assert_int_eq(emit_set_runtime_property(element, "batch-size", "8"), GVA_PROPERTY_NOT_MUTABLE);
    ck_assert_int_eq(emit_set_runtime_property(element, "model", "/some/model.xml"), GVA_PROPERTY_NOT_MUTABLE);
    // The state is checked before the value
    ck_assert_int_eq(emit_set_runtime_property(element, "batch-size", "abc"), GVA_PROPERTY_NOT_MUTABLE);

    gchar *device = get_string(element, "device");
    ck_assert_str_eq(device, "CPU");
    g_free(device);
    ck_assert_uint_eq(get_uint(element, "batch-size"), 2);

    force_state(element, GST_STATE_NULL);
    gst_object_unref(element);
}
GST_END_TEST;

GST_START_TEST(test_signal_by_state) {
    g_print("Starting test: test_signal_by_state\n");
    GstElement *element = gst_element_factory_make("gvadetect", NULL);
    ck_assert(element != NULL);

    // Before the element starts, everything can be changed
    for (GstState state : {GST_STATE_NULL, GST_STATE_READY}) {
        force_state(element, state);
        ck_assert_int_eq(emit_set_runtime_property(element, "device", "GPU"), GVA_PROPERTY_APPLIED);
        ck_assert_int_eq(emit_set_runtime_property(element, "device", "CPU"), GVA_PROPERTY_APPLIED);
    }

    for (GstState state : {GST_STATE_PAUSED, GST_STATE_PLAYING}) {
        force_state(element, state);
        ck_assert_int_eq(emit_set_runtime_property(element, "device", "GPU"), GVA_PROPERTY_NOT_MUTABLE);
        ck_assert_int_eq(emit_set_runtime_property(element, "threshold", "0.4"), GVA_PROPERTY_APPLIED);
    }

    force_state(element, GST_STATE_NULL);
    gst_object_unref(element);
}
GST_END_TEST;

GST_START_TEST(test_signal_on_other_elements) {
    g_print("Starting test: test_signal_on_other_elements\n");
    GstElement *inference = gst_element_factory_make("gvainference", NULL);
    GstElement *classify = gst_element_factory_make("gvaclassify", NULL);
    ck_assert(inference != NULL && classify != NULL);
    force_state(inference, GST_STATE_PLAYING);
    force_state(classify, GST_STATE_PLAYING);

    ck_assert_int_eq(emit_set_runtime_property(inference, "inference-interval", "3"), GVA_PROPERTY_APPLIED);
    ck_assert_int_eq(emit_set_runtime_property(inference, "threshold", "0.5"), GVA_PROPERTY_NOT_FOUND);
    ck_assert_int_eq(emit_set_runtime_property(classify, "inference-interval", "4"), GVA_PROPERTY_APPLIED);
    ck_assert_int_eq(emit_set_runtime_property(classify, "reclassify-interval", "4"), GVA_PROPERTY_NOT_MUTABLE);
    ck_assert_uint_eq(get_uint(classify, "reclassify-interval"), 1);

    force_state(inference, GST_STATE_NULL);
    force_state(classify, GST_STATE_NULL);
    gst_object_unref(inference);
    gst_object_unref(classify);
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
    tcase_add_test(tc_chain, test_only_interval_and_threshold_are_changeable_while_running);
    tcase_add_test(tc_chain, test_write_of_mutable_properties_while_running);
    tcase_add_test(tc_chain, test_write_of_other_properties_while_running_is_refused);
    tcase_add_test(tc_chain, test_write_of_properties_by_state);
    tcase_add_test(tc_chain, test_write_while_running_on_other_elements);
    tcase_add_test(tc_chain, test_signal_applies_mutable_properties);
    tcase_add_test(tc_chain, test_signal_refuses_invalid_value);
    tcase_add_test(tc_chain, test_signal_refuses_other_properties);
    tcase_add_test(tc_chain, test_signal_by_state);
    tcase_add_test(tc_chain, test_signal_on_other_elements);

    return s;
}

GST_CHECK_MAIN(inference_properties_testing);
