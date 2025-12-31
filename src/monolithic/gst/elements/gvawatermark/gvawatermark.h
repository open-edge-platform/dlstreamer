/*******************************************************************************
 * Copyright (C) 2021-2025 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#ifndef _GST_GVA_WATERMARK_H_
#define _GST_GVA_WATERMARK_H_

#include <gst/gst.h>
#include <gst/gstbin.h>

G_BEGIN_DECLS

#define GST_TYPE_GVA_WATERMARK (gst_gva_watermark_get_type())
#define GST_GVA_WATERMARK(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_GVA_WATERMARK, GstGvaWatermark))
#define GST_GVA_WATERMARK_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_GVA_WATERMARK, GstGvaWatermarkClass))
#define GST_IS_GVA_WATERMARK(obj) (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_GVA_WATERMARK))
#define GST_IS_GVA_WATERMARK_CLASS(obj) (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_GVA_WATERMARK))

typedef struct _GstGvaWatermark GstGvaWatermark;
typedef struct _GstGvaWatermarkClass GstGvaWatermarkClass;

enum WatermarkPath { WatermarkPathNone = 0, WatermarkPathVaVaapi, WatermarkPathDirect, WatermarkPathTransparent };

struct _GstGvaWatermark {
    GstBin base_gvawatermark;

    GstPad *sinkpad;
    GstPad *srcpad;
    gchar *device;
    gchar *displ_cfg;
    bool obb;
    bool disp_avgfps;

    bool use_watermarkimpl_only;

    enum WatermarkPath preferred_path;
    enum WatermarkPath active_path;
    WatermarkPath block_pad_source;
    bool is_active_nv12;
    bool have_vaapi;
    bool have_va;
    gulong block_probe_id;

    /* for all */
    GstElement *identity;
    GstElement *watermarkimpl;
    GstElement *postproc;
    /* for VA_API, VA path */
    GstElement *preproc;
    GstElement *capsfilter;
    GstElement *convert;
};

struct _GstGvaWatermarkClass {
    GstBinClass base_gvawatermark_class;
};

GType gst_gva_watermark_get_type(void);

G_END_DECLS

#endif
