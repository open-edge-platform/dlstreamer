/*******************************************************************************
 * Copyright (C) 2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#ifndef __GST_GVAUPSCALE_H__
#define __GST_GVAUPSCALE_H__

#include <gst/video/gstvideofilter.h>

G_BEGIN_DECLS

#define GST_TYPE_GVAUPSCALE (gst_gvaupscale_get_type())
G_DECLARE_FINAL_TYPE(GstGvaUpscale, gst_gvaupscale, GST, GVAUPSCALE, GstVideoFilter)

// Opaque C++ implementation state (OpenVINO™ toolkit inference backend), defined in gvaupscale.cpp.
typedef struct _GvaUpscalePrivate GvaUpscalePrivate;

struct _GstGvaUpscale {
    GstVideoFilter parent_instance;

    // Properties
    gchar *model_path;
    gchar *device;
    gdouble scale;

    GvaUpscalePrivate *priv;
};

struct _GstGvaUpscaleClass {
    GstVideoFilterClass parent_class;
};

G_END_DECLS

#endif /* __GST_GVAUPSCALE_H__ */
