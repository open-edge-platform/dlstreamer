/*******************************************************************************
 * Copyright (C) 2018-2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#include <gst/audio/audio.h>
#include <gst/video/video-format.h>
#include <gst/video/video.h>
#include <opencv2/imgcodecs.hpp>
#include <unistd.h>

#include "test_common.h"

cv::Mat get_image(const std::string &imagePath, cv::ColorConversionCodes color_convert_code) {
    cv::Mat origin;
    try {
        origin = cv::imread(imagePath, cv::IMREAD_COLOR);
        if (color_convert_code != cv::COLOR_COLORCVT_MAX) {
            cv::Mat converted;
            cv::cvtColor(origin, converted, color_convert_code);
            return converted;
        }
    } catch (cv::Exception &e) {
        g_print("OpenCV exception caught: %s\n", e.what());
    }
    return origin;
}

void get_audio_data(guint8 audio_data[], int size, std::string file_path) {
    FILE *ifile;
    ifile = fopen(file_path.c_str(), "rb");
    size_t res = fread(audio_data, 1, size, ifile);
    fclose(ifile);
}
void cleanup_plugin(GstElement *plugin) {
    GST_DEBUG_OBJECT(plugin, "cleanup_element");

    gst_check_teardown_src_pad(plugin);
    gst_check_teardown_sink_pad(plugin);
    gst_check_teardown_element(plugin);
}

GstPad *mysrcpad, *mysinkpad;

static GstElement *setup_plugin(const gchar *name, GstStaticPadTemplate *srctemplate,
                                GstStaticPadTemplate *sinktemplate) {
    GstElement *element;

    GST_DEBUG("setup_element");

    element = gst_check_setup_element(name);
    mysrcpad = gst_check_setup_src_pad(element, srctemplate);
    gst_pad_set_active(mysrcpad, TRUE);
    mysinkpad = gst_check_setup_sink_pad(element, sinktemplate);
    gst_pad_set_active(mysinkpad, TRUE);

    return element;
}

void launch_plugin(GstElement *plugin) {
    ck_assert(gst_element_set_state(plugin, GST_STATE_PLAYING));
    while (gst_element_get_state(plugin, NULL, NULL, GST_CLOCK_TIME_NONE) != GST_STATE_CHANGE_SUCCESS)
        continue;
}

void completion_plugin(GstElement *plugin) {
    ck_assert(gst_element_set_state(plugin, GST_STATE_NULL) == GST_STATE_CHANGE_SUCCESS);
    while (gst_element_get_state(plugin, NULL, NULL, GST_CLOCK_TIME_NONE) != GST_STATE_CHANGE_SUCCESS)
        continue;
}

static void check_incorrect_plugin_caps(const gchar *name, GstStaticPadTemplate *srctemplate,
                                        GstStaticPadTemplate *sinktemplate, const gchar *prop, va_list varargs) {
    GstElement *plugin = setup_plugin(name, srctemplate, sinktemplate);
    g_object_set_valist(G_OBJECT(plugin), prop, varargs);
    ck_assert_msg(gst_element_set_state(plugin, GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE,
                  "gst_element_set_state did not return GST_STATE_CHANGE_FAILURE");
}
static void check_plugin_caps(const gchar *name, GstCaps *caps, gint size, GstStaticPadTemplate *srctemplate,
                              GstStaticPadTemplate *sinktemplate, SetupInBuffCb setup_inbuf,
                              CheckOutBuffCb check_outbuf, gpointer user_data, const gchar *prop, va_list varargs,
                              GetExpectedSizeIncreaseCb get_size_increase_cb = NULL) {
    GstElement *plugin = setup_plugin(name, srctemplate, sinktemplate);
    g_object_set_valist(G_OBJECT(plugin), prop, varargs);

    launch_plugin(plugin);

    gst_check_setup_events(mysrcpad, plugin, caps, GST_FORMAT_TIME);

    GstBuffer *inbuffer = gst_buffer_new_and_alloc(size);
    if (setup_inbuf) {
        setup_inbuf(inbuffer, user_data);
    } // Call callback after setup_inbuf to get the expected size increase
    gsize expected_size_increase = (get_size_increase_cb != NULL) ? get_size_increase_cb(user_data) : 0;
    GST_BUFFER_TIMESTAMP(inbuffer) = 0;
    ASSERT_BUFFER_REFCOUNT(inbuffer, "inbuffer", 1);
    ck_assert(gst_pad_push(mysrcpad, inbuffer) == GST_FLOW_OK);

    while (g_list_length(buffers) < 1) {
        usleep(1000);
    }

    GstBuffer *outbuffer = GST_BUFFER(buffers->data);
    ck_assert(outbuffer != NULL);

    gsize expected_output_size = size + expected_size_increase;
    ck_assert_msg(gst_buffer_get_size(outbuffer) == expected_output_size,
                  "Expected buffer size %zu (base %d + increase %zu), got %zu", expected_output_size, size,
                  expected_size_increase, gst_buffer_get_size(outbuffer));
    if (check_outbuf) {
        check_outbuf(outbuffer, user_data);
    }
    buffers = g_list_remove(buffers, outbuffer);

    ASSERT_BUFFER_REFCOUNT(outbuffer, "outbuffer", 1);
    gst_buffer_unref(outbuffer);
    outbuffer = NULL;

    completion_plugin(plugin);
    cleanup_plugin(plugin);
    gst_check_drop_buffers();
    buffers = NULL;
}

void run_test(const gchar *elem_name, const gchar *caps_string, Resolution resolution,
              GstStaticPadTemplate *srctemplate, GstStaticPadTemplate *sinktemplate, SetupInBuffCb setup_inbuf,
              CheckOutBuffCb check_outbuf, gpointer user_data, const gchar *prop, ...) {
    GstCaps *templ = gst_caps_from_string(caps_string);
    GstCaps *allcaps = gst_caps_normalize(templ);
    gint n = gst_caps_get_size(allcaps);

    for (gint i = 0; i < n; i++) {
        GstStructure *s = gst_caps_get_structure(allcaps, i);
        GstCaps *caps = gst_caps_new_empty();

        gst_caps_append_structure(caps, gst_structure_copy(s));

        GstVideoInfo info;

        caps = gst_caps_make_writable(caps);
        gst_caps_set_simple(caps, "width", G_TYPE_INT, resolution.width, "height", G_TYPE_INT, resolution.height,
                            "framerate", GST_TYPE_FRACTION, 25, 1, NULL);

        gst_video_info_from_caps(&info, caps);
        gint size = GST_VIDEO_INFO_SIZE(&info);
        va_list varargs;
        va_start(varargs, prop);
        check_plugin_caps(elem_name, caps, size, srctemplate, sinktemplate, setup_inbuf, check_outbuf, user_data, prop,
                          varargs, NULL);
        va_end(varargs);

        gst_caps_unref(caps);
    }

    gst_caps_unref(allcaps);
}

void run_test_with_size_increase(const gchar *elem_name, const gchar *caps_string, Resolution resolution,
                                 GstStaticPadTemplate *srctemplate, GstStaticPadTemplate *sinktemplate,
                                 SetupInBuffCb setup_inbuf, CheckOutBuffCb check_outbuf, gpointer user_data,
                                 GetExpectedSizeIncreaseCb get_size_increase_cb, const gchar *prop, ...) {
    GstCaps *templ = gst_caps_from_string(caps_string);
    GstCaps *allcaps = gst_caps_normalize(templ);
    gint n = gst_caps_get_size(allcaps);

    for (gint i = 0; i < n; i++) {
        GstStructure *s = gst_caps_get_structure(allcaps, i);
        GstCaps *caps = gst_caps_new_empty();

        gst_caps_append_structure(caps, gst_structure_copy(s));

        GstVideoInfo info;

        caps = gst_caps_make_writable(caps);
        gst_caps_set_simple(caps, "width", G_TYPE_INT, resolution.width, "height", G_TYPE_INT, resolution.height,
                            "framerate", GST_TYPE_FRACTION, 25, 1, NULL);

        gst_video_info_from_caps(&info, caps);
        gint size = GST_VIDEO_INFO_SIZE(&info);
        va_list varargs;
        va_start(varargs, prop);
        check_plugin_caps(elem_name, caps, size, srctemplate, sinktemplate, setup_inbuf, check_outbuf, user_data, prop,
                          varargs, get_size_increase_cb);
        va_end(varargs);

        gst_caps_unref(caps);
    }

    gst_caps_unref(allcaps);
}

void run_test_fail(const gchar *elem_name, const gchar *caps_string, Resolution resolution,
                   GstStaticPadTemplate *srctemplate, GstStaticPadTemplate *sinktemplate, SetupInBuffCb setup_inbuf,
                   gpointer user_data, const gchar *prop, ...) {
    GstCaps *templ = gst_caps_from_string(caps_string);
    GstCaps *allcaps = gst_caps_normalize(templ);
    gint n = gst_caps_get_size(allcaps);

    for (gint i = 0; i < n; i++) {
        GstStructure *s = gst_caps_get_structure(allcaps, i);
        GstCaps *caps = gst_caps_new_empty();

        gst_caps_append_structure(caps, gst_structure_copy(s));

        GstVideoInfo info;

        caps = gst_caps_make_writable(caps);
        gst_caps_set_simple(caps, "width", G_TYPE_INT, resolution.width, "height", G_TYPE_INT, resolution.height,
                            "framerate", GST_TYPE_FRACTION, 25, 1, NULL);

        gst_video_info_from_caps(&info, caps);
        gint size = GST_VIDEO_INFO_SIZE(&info);
        va_list varargs;
        va_start(varargs, prop);

        GstElement *plugin = setup_plugin(elem_name, srctemplate, sinktemplate);
        g_object_set_valist(G_OBJECT(plugin), prop, varargs);

        launch_plugin(plugin);

        gst_check_setup_events(mysrcpad, plugin, caps, GST_FORMAT_TIME);

        GstBuffer *inbuffer = gst_buffer_new_and_alloc(size);
        if (setup_inbuf) {
            setup_inbuf(inbuffer, user_data);
        }
        GST_BUFFER_TIMESTAMP(inbuffer) = 0;
        ASSERT_BUFFER_REFCOUNT(inbuffer, "inbuffer", 1);
        ck_assert(gst_pad_push(mysrcpad, inbuffer) == GST_FLOW_ERROR);

        va_end(varargs);

        completion_plugin(plugin);
        cleanup_plugin(plugin);
        gst_check_drop_buffers();
        buffers = NULL;
        gst_caps_unref(caps);
    }

    gst_caps_unref(allcaps);
}

void run_audio_test_fail(const gchar *elem_name, const gchar *caps_string, GstStaticPadTemplate *srctemplate,
                         GstStaticPadTemplate *sinktemplate, const gchar *prop, ...) {
    GstCaps *templ = gst_caps_from_string(caps_string);
    GstCaps *allcaps = gst_caps_normalize(templ);
    gint n = gst_caps_get_size(allcaps);
    for (gint i = 0; i < n; i++) {
        GstStructure *s = gst_caps_get_structure(allcaps, i);
        GstCaps *caps = gst_caps_new_empty();
        gst_caps_append_structure(caps, gst_structure_copy(s));
        GstAudioInfo info;
        gst_audio_info_from_caps(&info, caps);
        va_list varargs;
        va_start(varargs, prop);
        check_incorrect_plugin_caps(elem_name, srctemplate, sinktemplate, prop, varargs);
        va_end(varargs);

        gst_caps_unref(caps);
    }

    gst_caps_unref(allcaps);
}

void run_audio_test(const gchar *elem_name, const gchar *caps_string, GstStaticPadTemplate *srctemplate,
                    GstStaticPadTemplate *sinktemplate, SetupInBuffCb setup_inbuf, CheckOutBuffCb check_outbuf,
                    gpointer user_data, const gchar *prop, ...) {
    GstCaps *templ = gst_caps_from_string(caps_string);
    GstCaps *allcaps = gst_caps_normalize(templ);
    gint n = gst_caps_get_size(allcaps);

    for (gint i = 0; i < n; i++) {
        GstStructure *s = gst_caps_get_structure(allcaps, i);
        GstCaps *caps = gst_caps_new_empty();
        gst_caps_append_structure(caps, gst_structure_copy(s));
        GstAudioInfo info;
        gst_audio_info_from_caps(&info, caps);
        va_list varargs;
        va_start(varargs, prop);
        gint size = GST_AUDIO_INFO_RATE(&info) * GST_AUDIO_INFO_BPF(&info);
        check_plugin_caps(elem_name, caps, size, srctemplate, sinktemplate, setup_inbuf, check_outbuf, user_data, prop,
                          varargs);
        va_end(varargs);

        gst_caps_unref(caps);
    }

    gst_caps_unref(allcaps);
}

void check_bus_for_error(const gchar *plugin_name, GstStaticPadTemplate *srctemplate,
                         GstStaticPadTemplate *sinktemplate, const gchar *expected_msg, GQuark domain, gint code,
                         const gchar *prop, ...) {
    GstElement *element, *pipeline, *source, *sink;
    GstBus *bus;
    GstMessage *msg = NULL;
    GError *err = NULL;
    gchar *dbg_info = NULL;

    element = setup_plugin(plugin_name, srctemplate, sinktemplate);
    va_list varargs;
    va_start(varargs, prop);
    g_object_set_valist(G_OBJECT(element), prop, varargs);
    va_end(varargs);

    pipeline = gst_pipeline_new("test-pipeline");
    bus = gst_pipeline_get_bus(GST_PIPELINE(pipeline));
    source = gst_element_factory_make("videotestsrc", "source");
    sink = gst_element_factory_make("fakesink", "sink");

    gst_bin_add_many(GST_BIN(pipeline), source, element, sink, NULL);
    gst_element_link_many(source, element, sink, NULL);
    gst_element_set_state(pipeline, GST_STATE_PLAYING);
    msg = gst_bus_poll(bus, (GstMessageType)(GST_MESSAGE_ERROR), -1);
    gst_element_set_state(pipeline, GST_STATE_NULL);
    gst_element_get_state(pipeline, NULL, NULL, -1);
    gst_object_unref(pipeline);
    ck_assert_msg(msg != NULL, "Did not receive message");
    gst_check_message_error(msg, GST_MESSAGE_ERROR, domain, code);
    gst_message_parse_error(msg, &err, &dbg_info);
    g_printerr("Debugging info: -----------------------------\n %s \n---------------------------------------------\n",
               dbg_info);
    if (expected_msg)
        ck_assert_msg(strstr(dbg_info, expected_msg) != NULL, "Error message does not match expected message");
}

void check_multiple_property_init_fail_if_invalid_value(const gchar *plugin_name, GstStaticPadTemplate *srctemplate,
                                                        GstStaticPadTemplate *sinktemplate, const gchar *expected_msg,
                                                        const gchar *prop, ...) {
    GstElement *element;
    GstBus *bus;
    GstMessage *msg = NULL;
    GError *err = NULL;
    gchar *dbg_info = NULL;

    element = setup_plugin(plugin_name, srctemplate, sinktemplate);
    bus = gst_bus_new();
    gst_element_set_bus(element, bus);

    va_list varargs;
    va_start(varargs, prop);
    g_object_set_valist(G_OBJECT(element), prop, varargs);
    va_end(varargs);
    ck_assert_msg(gst_element_set_state(element, GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE,
                  "Element successfully changed state to playing");

    msg = gst_bus_poll(bus, (GstMessageType)(GST_MESSAGE_ERROR), -1);
    ck_assert_msg(msg != NULL, "Elelment received invalid property value and did not display an error");
    if (msg) {
        gst_message_parse_error(msg, &err, &dbg_info);
        if (dbg_info) {
            g_print("\n\nDebug Info = %s\n\n", dbg_info);
            g_printerr(
                "Debugging info: -----------------------------\n %s \n---------------------------------------------\n",
                dbg_info);
            ck_assert_msg(strstr(dbg_info, expected_msg) != NULL, "No bad property error was received");
        }
        gst_message_unref(msg);
    }

    g_error_free(err);
    g_free(dbg_info);
    gst_element_set_bus(element, NULL);
    gst_object_unref(GST_OBJECT(bus));
    cleanup_plugin(element);
}

void check_property_default_if_invalid_value(const gchar *plugin_name, const gchar *prop_name, GValue prop_value) {
    GParamSpec *prop_spec;
    GstElement *element;
    GValue received_prop_value = G_VALUE_INIT;
    GValue default_prop_value = G_VALUE_INIT;

    element = gst_check_setup_element(plugin_name);
    if (element) {
        prop_spec = g_object_class_find_property(G_OBJECT_GET_CLASS(element), prop_name);
        if (prop_spec) {
            g_value_init(&received_prop_value, prop_spec->value_type);
            g_value_init(&default_prop_value, prop_spec->value_type);

            g_object_get_property(G_OBJECT(element), prop_name, &default_prop_value);
            g_object_get_property(G_OBJECT(element), prop_name, &received_prop_value);

            ck_assert_msg(gst_value_compare(&default_prop_value, &received_prop_value) == GST_VALUE_EQUAL,
                          "The resulting property value is not equal to the default");
        }
        gst_check_teardown_element(element);
        element = NULL;
    }
}

void check_property_value_updated_correctly(const gchar *plugin_name, const gchar *prop_name, GValue prop_value) {
    GParamSpec *prop_spec;
    GstElement *element;
    GValue received_prop_value = G_VALUE_INIT;
    GValue default_prop_value = G_VALUE_INIT;

    element = gst_check_setup_element(plugin_name);
    if (element) {
        prop_spec = g_object_class_find_property(G_OBJECT_GET_CLASS(element), prop_name);
        if (prop_spec) {
            g_value_init(&received_prop_value, prop_spec->value_type);
            g_value_init(&default_prop_value, prop_spec->value_type);

            g_object_get_property(G_OBJECT(element), prop_name, &default_prop_value);

            ck_assert_msg(gst_value_compare(&default_prop_value, &prop_value) != GST_VALUE_EQUAL,
                          "New value is the same as default. Can't tell if value updated");
            g_object_set_property(G_OBJECT(element), prop_name, &prop_value);
            g_object_get_property(G_OBJECT(element), prop_name, &received_prop_value);

            if (gst_value_compare(&prop_value, &received_prop_value) == GST_VALUE_EQUAL)
                g_print("Values are the same\n");
            else {
                g_print("Values are NOT the same\n");
            }

            ck_assert_msg(gst_value_compare(&prop_value, &received_prop_value) == GST_VALUE_EQUAL,
                          "Recieved value is not the same as value set");
        }
        gst_check_teardown_element(element);
        element = NULL;
    }
}
