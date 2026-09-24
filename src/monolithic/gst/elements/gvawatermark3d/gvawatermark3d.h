/*******************************************************************************
 * Copyright (C) 2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#ifndef __GST_GVAWATERMARK3D_H__
#define __GST_GVAWATERMARK3D_H__

#include <gst/gst.h>

G_BEGIN_DECLS

#define GST_TYPE_GVAWATERMARK3D (gst_gvawatermark3d_get_type())
G_DECLARE_FINAL_TYPE(GstGvaWatermark3D, gst_gvawatermark3d, GST, GVAWATERMARK3D, GstBin)

struct _GstGvaWatermark3D {
    GstBin parent_instance;
    GstPad *sinkpad;
    GstPad *srcpad;
    GstElement *vaconvert; // optional VA download (NULL when VA unavailable)
    GstElement *videoconvert;
    GstElement *capsfilter;
    GstElement *render;           // hidden GstGvaWatermark3DRender
    GstElement *videoconvert_out; // lets the bin output any format the downstream requests
};

G_END_DECLS

#endif /* __GST_GVAWATERMARK3D_H__ */
