/*******************************************************************************
 * Copyright (C) 2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#ifndef _GST_GVA_FPS_THROTTLE_H_
#define _GST_GVA_FPS_THROTTLE_H_

#include <gst/base/gstbasetransform.h>
#include <gst/video/video.h>

G_BEGIN_DECLS

#define GST_TYPE_GVA_FPS_THROTTLE (gst_gva_fps_throttle_get_type())
#define GST_GVA_FPS_THROTTLE(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_GVA_FPS_THROTTLE, GstGvaFpsThrottle))
#define GST_GVA_FPS_THROTTLE_CLASS(klass)                                                                              \
    (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_GVA_FPS_THROTTLE, GstGvaFpsThrottleClass))
#define GST_IS_GVA_FPS_THROTTLE(obj) (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_GVA_FPS_THROTTLE))
#define GST_IS_GVA_FPS_THROTTLE_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_GVA_FPS_THROTTLE))

typedef struct _GstGvaFpsThrottle GstGvaFpsThrottle;
typedef struct _GstGvaFpsThrottleClass GstGvaFpsThrottleClass;

struct _GstGvaFpsThrottle {
    GstBaseTransform base_gvafpsthrottle;

    /* Properties */
    gdouble target_fps;

    /* State */
    GstClockTime last_buffer_time;
    GstClockTime frame_duration;
};

struct _GstGvaFpsThrottleClass {
    GstBaseTransformClass base_gvafpsthrottle_class;
};

GType gst_gva_fps_throttle_get_type(void);

G_END_DECLS

#endif
