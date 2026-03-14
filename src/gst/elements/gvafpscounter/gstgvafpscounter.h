/*******************************************************************************
 * Copyright (C) 2018-2025 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#ifndef _GST_GVA_FPSCOUNTER_H_
#define _GST_GVA_FPSCOUNTER_H_

#include <gst/base/gstbasetransform.h>
#include <gst/video/video.h>

G_BEGIN_DECLS

#define GST_TYPE_GVA_FPSCOUNTER (gst_gva_fpscounter_get_type())
#define GST_GVA_FPSCOUNTER(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_GVA_FPSCOUNTER, GstGvaFpscounter))
#define GST_GVA_FPSCOUNTER_CLASS(klass)                                                                                \
    (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_GVA_FPSCOUNTER, GstGvaFpscounterClass))
#define GST_IS_GVA_FPSCOUNTER(obj) (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_GVA_FPSCOUNTER))
#define GST_IS_GVA_FPSCOUNTER_CLASS(obj) (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_GVA_FPSCOUNTER))

typedef struct _GstGvaFpscounter GstGvaFpscounter;
typedef struct _GstGvaFpscounterClass GstGvaFpscounterClass;

struct _GstGvaFpscounter {
    GstBaseTransform base_gvafpscounter;
    gchar *interval;
    guint starting_frame;
    guint detections;
    gfloat avg_fps;
    gchar *write_pipe;
    gchar *read_pipe;
    gboolean print_std_dev;
    gboolean print_latency;
};

struct _GstGvaFpscounterClass {
    GstBaseTransformClass base_gvafpscounter_class;
};

GType gst_gva_fpscounter_get_type(void);

G_END_DECLS

#endif
