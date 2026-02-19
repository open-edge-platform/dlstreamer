/*******************************************************************************
 * Copyright (C) 2018-2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#pragma once

#include "opencv2/core.hpp"
#include "opencv2/imgproc.hpp"
#include <gst/check/gstcheck.h>
#include <gst/video/video.h>

#define VIDEO_CAPS_TEMPLATE_STRING GST_VIDEO_CAPS_MAKE("{ BGR }")
// Video format for video info of buffer
constexpr auto TEST_BUFFER_VIDEO_FORMAT = GST_VIDEO_FORMAT_BGR;
// Color conversion code for `get_image`. By default `get_image` returns BGR.
// The COLOR_COLORCVT_MAX means no conversion.
constexpr auto TEST_OCV_COLOR_CONVERT_CODE = cv::COLOR_COLORCVT_MAX;

cv::Mat get_image(const std::string &imagePath, cv::ColorConversionCodes color_convert_code = cv::COLOR_COLORCVT_MAX);
void get_audio_data(guint8 audio_data[], int size, std::string file_path);

struct Resolution {
    gint width;
    gint height;
};

typedef void (*SetupInBuffCb)(GstBuffer *inbuffer, gpointer user_data);
typedef void (*CheckOutBuffCb)(GstBuffer *outbuffer, gpointer user_data);
/**
 * @brief Run simple test based on GstCheck for one element
 * It setups element, generates input buffer and checks output buffer.
 *
 * @param elem_name - Name of gst element to test
 * @param caps_string - Capability string to generate caps
 * @param resolution - Caps resolution for test
 * @param srctemplate - Gst pad template to create src pad
 * @param sinktemplate - Gst pad template to create sink pad
 * @param setup_inbuf - Function to setup input buffer
 * @param check_outbuf - Function to check output buffer
 * @param user_data - User data which will be passed to callbacks
 * @param prop - Additional properties to setup element (as key value pairs)
 * @param ...
 */
void run_test(const gchar *elem_name, const gchar *caps_string, Resolution resolution,
              GstStaticPadTemplate *srctemplate, GstStaticPadTemplate *sinktemplate, SetupInBuffCb setup_inbuf,
              CheckOutBuffCb check_outbuf, gpointer user_data, const gchar *prop, ...);
void run_test_fail(const gchar *elem_name, const gchar *caps_string, Resolution resolution,
                   GstStaticPadTemplate *srctemplate, GstStaticPadTemplate *sinktemplate, SetupInBuffCb setup_inbuf,
                   gpointer user_data, const gchar *prop, ...);
void run_audio_test_fail(const gchar *elem_name, const gchar *caps_string, GstStaticPadTemplate *srctemplate,
                         GstStaticPadTemplate *sinktemplate, const gchar *prop, ...);
void run_audio_test(const gchar *elem_name, const gchar *caps_string, GstStaticPadTemplate *srctemplate,
                    GstStaticPadTemplate *sinktemplate, SetupInBuffCb setup_inbuf, CheckOutBuffCb check_outbuf,
                    gpointer user_data, const gchar *prop, ...);
void check_multiple_property_init_fail_if_invalid_value(const gchar *plugin_name, GstStaticPadTemplate *srctemplate,
                                                        GstStaticPadTemplate *sinktemplate, const gchar *expected_msg,
                                                        const gchar *prop, ...);
void check_property_default_if_invalid_value(const gchar *plugin_name, const gchar *prop_name, GValue prop_value);
void check_property_value_updated_correctly(const gchar *plugin_name, const gchar *prop_name, GValue prop_value);
void check_bus_for_error(const gchar *plugin_name, GstStaticPadTemplate *srctemplate,
                         GstStaticPadTemplate *sinktemplate, const gchar *expected_msg, GQuark domain, gint code,
                         const gchar *prop, ...);

const char *g_value_get_as_string(GValue *value);
