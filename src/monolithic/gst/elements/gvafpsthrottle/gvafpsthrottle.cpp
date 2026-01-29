/*******************************************************************************
 * Copyright (C) 2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#include "gvafpsthrottle.hpp"

#include <gst/base/gstbasetransform.h>
#include <gst/gst.h>
#include <gst/video/video.h>

#include <chrono>
#include <thread>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

GST_DEBUG_CATEGORY_STATIC(gst_gva_fps_throttle_debug_category);
#define GST_CAT_DEFAULT gst_gva_fps_throttle_debug_category

/* Helper macro to get current time in GStreamer clock time units */
#define GST_GET_MONOTONIC_TIME() (g_get_monotonic_time() * GST_USECOND)

/* prototypes */
static void gst_gva_fps_throttle_set_property(GObject *object, guint property_id, const GValue *value,
                                              GParamSpec *pspec);
static void gst_gva_fps_throttle_get_property(GObject *object, guint property_id, GValue *value, GParamSpec *pspec);

static gboolean gst_gva_fps_throttle_start(GstBaseTransform *trans);
static GstFlowReturn gst_gva_fps_throttle_transform_ip(GstBaseTransform *trans, GstBuffer *buf);

enum { PROP_0, PROP_TARGET_FPS };

/* class initialization */
G_DEFINE_TYPE_WITH_CODE(GstGvaFpsThrottle, gst_gva_fps_throttle, GST_TYPE_BASE_TRANSFORM,
                        GST_DEBUG_CATEGORY_INIT(gst_gva_fps_throttle_debug_category, "gvafpsthrottle", 0,
                                                "debug category for gvafpsthrottle element"));

static void gst_gva_fps_throttle_class_init(GstGvaFpsThrottleClass *klass) {
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
    GstBaseTransformClass *base_transform_class = GST_BASE_TRANSFORM_CLASS(klass);

    gst_element_class_add_pad_template(
        GST_ELEMENT_CLASS(klass),
        gst_pad_template_new("src", GST_PAD_SRC, GST_PAD_ALWAYS, gst_caps_from_string("ANY")));
    gst_element_class_add_pad_template(
        GST_ELEMENT_CLASS(klass),
        gst_pad_template_new("sink", GST_PAD_SINK, GST_PAD_ALWAYS, gst_caps_from_string("ANY")));

    gst_element_class_set_static_metadata(GST_ELEMENT_CLASS(klass), "Framerate Throttle", "Video/Filter",
                                          "Throttles framerate by limiting buffer flow", "Intel Corporation");

    gobject_class->set_property = gst_gva_fps_throttle_set_property;
    gobject_class->get_property = gst_gva_fps_throttle_get_property;

    base_transform_class->start = GST_DEBUG_FUNCPTR(gst_gva_fps_throttle_start);
    base_transform_class->transform_ip = GST_DEBUG_FUNCPTR(gst_gva_fps_throttle_transform_ip);

    g_object_class_install_property(
        gobject_class, PROP_TARGET_FPS,
        g_param_spec_double("target-fps", "Target FPS", "Target frames per second to limit buffer flow", 0.0,
                            G_MAXDOUBLE, 0.0, static_cast<GParamFlags>(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
}

static void gst_gva_fps_throttle_init(GstGvaFpsThrottle *gvafpsthrottle) {
    gvafpsthrottle->target_fps = 0.0;
    gvafpsthrottle->last_buffer_time = GST_CLOCK_TIME_NONE;
    gvafpsthrottle->frame_duration = 0;

    gst_base_transform_set_in_place(GST_BASE_TRANSFORM(gvafpsthrottle), TRUE);
    gst_base_transform_set_passthrough(GST_BASE_TRANSFORM(gvafpsthrottle), TRUE);
}

void gst_gva_fps_throttle_set_property(GObject *object, guint property_id, const GValue *value, GParamSpec *pspec) {
    GstGvaFpsThrottle *gvafpsthrottle = GST_GVA_FPS_THROTTLE(object);

    switch (property_id) {
    case PROP_TARGET_FPS:
        gvafpsthrottle->target_fps = g_value_get_double(value);
        if (gvafpsthrottle->target_fps <= 0.0) {
            GST_ELEMENT_ERROR(gvafpsthrottle, LIBRARY, SETTINGS,
                              ("Invalid target-fps value: %f", gvafpsthrottle->target_fps),
                              ("target-fps must be greater than 0"));
            gvafpsthrottle->target_fps = 0.0;
            gvafpsthrottle->frame_duration = 0;
            return;
        }
        gvafpsthrottle->frame_duration = (GstClockTime)(GST_SECOND / gvafpsthrottle->target_fps);
        /* Warn if the frame duration is too small for accurate sleeping (< 100 microseconds) */
        if (gvafpsthrottle->frame_duration < 100 * GST_USECOND) {
            GST_WARNING_OBJECT(gvafpsthrottle,
                               "target-fps is very high (%f), frame duration (%" GST_TIME_FORMAT
                               ") may be too small for accurate sleep timing",
                               gvafpsthrottle->target_fps, GST_TIME_ARGS(gvafpsthrottle->frame_duration));
        }
        GST_DEBUG_OBJECT(gvafpsthrottle, "Setting target-fps to %f (frame duration: %" GST_TIME_FORMAT ")",
                         gvafpsthrottle->target_fps, GST_TIME_ARGS(gvafpsthrottle->frame_duration));
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
        break;
    }
}

void gst_gva_fps_throttle_get_property(GObject *object, guint property_id, GValue *value, GParamSpec *pspec) {
    GstGvaFpsThrottle *gvafpsthrottle = GST_GVA_FPS_THROTTLE(object);

    switch (property_id) {
    case PROP_TARGET_FPS:
        g_value_set_double(value, gvafpsthrottle->target_fps);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
        break;
    }
}

static gboolean gst_gva_fps_throttle_start(GstBaseTransform *trans) {
    GstGvaFpsThrottle *gvafpsthrottle = GST_GVA_FPS_THROTTLE(trans);

    gvafpsthrottle->last_buffer_time = GST_CLOCK_TIME_NONE;

    GST_INFO_OBJECT(gvafpsthrottle, "Started with target-fps: %f", gvafpsthrottle->target_fps);

    return TRUE;
}

static GstFlowReturn gst_gva_fps_throttle_transform_ip(GstBaseTransform *trans, GstBuffer *buf) {
    GstGvaFpsThrottle *self = GST_GVA_FPS_THROTTLE(trans);
    GstClockTime current_time;
    GstClockTime elapsed_time;
    GstClockTime sleep_time;

    (void)buf; /* unused */

    /* If target_fps is not set (0), pass through without throttling */
    if (self->target_fps <= 0.0) {
        return GST_FLOW_OK;
    }

    current_time = GST_GET_MONOTONIC_TIME();

    if (GST_CLOCK_TIME_IS_VALID(self->last_buffer_time)) {
        elapsed_time = current_time - self->last_buffer_time;

        /* If we're processing faster than the target framerate, sleep */
        if (elapsed_time < self->frame_duration) {
            sleep_time = self->frame_duration - elapsed_time;

            GST_LOG_OBJECT(self,
                           "Elapsed time %" GST_TIME_FORMAT " < frame duration %" GST_TIME_FORMAT
                           ", sleeping for %" GST_TIME_FORMAT,
                           GST_TIME_ARGS(elapsed_time), GST_TIME_ARGS(self->frame_duration), GST_TIME_ARGS(sleep_time));

            double us = static_cast<double>(GST_TIME_AS_USECONDS(sleep_time));
#ifdef _WIN32
            // Use high-resolution waitable timer for accurate sleep on Windows
            HANDLE timer = CreateWaitableTimerEx(NULL, NULL, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
            if (!timer) {
                return GST_FLOW_ERROR;
            }
            LARGE_INTEGER li = {};
            li.QuadPart = us * -10LL;
            if (!SetWaitableTimer(timer, &li, 0, NULL, NULL, FALSE)) {
                CloseHandle(timer);
                return GST_FLOW_ERROR;
            }
            if (WaitForSingleObject(timer, INFINITE) != WAIT_OBJECT_0) {
                GST_LOG_OBJECT(self, "WaitForSingleObject failed (%d)\n", GetLastError());
            }
            CloseHandle(timer);
#else
            std::this_thread::sleep_for(std::chrono::nanoseconds(int(1000 * us)));
#endif

            current_time = GST_GET_MONOTONIC_TIME();
        } else {
            GST_LOG_OBJECT(self,
                           "Elapsed time %" GST_TIME_FORMAT " >= frame duration %" GST_TIME_FORMAT ", no sleep needed",
                           GST_TIME_ARGS(elapsed_time), GST_TIME_ARGS(self->frame_duration));
        }
    }

    self->last_buffer_time = current_time;

    return GST_FLOW_OK;
}
