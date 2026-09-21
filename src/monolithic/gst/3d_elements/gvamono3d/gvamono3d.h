/*******************************************************************************
 * Copyright (C) 2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#pragma once

#include <gst/base/gstbasetransform.h>
#include <gst/gst.h>
#include <gst/video/video.h>

G_BEGIN_DECLS

#define GST_TYPE_GVA_MONO3D (gst_gva_mono3d_get_type())
#define GST_GVA_MONO3D(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_GVA_MONO3D, GstGvaMono3d))
#define GST_GVA_MONO3D_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_GVA_MONO3D, GstGvaMono3dClass))
#define GST_IS_GVA_MONO3D(obj) (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_GVA_MONO3D))
#define GST_IS_GVA_MONO3D_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_GVA_MONO3D))

typedef struct _GstGvaMono3d GstGvaMono3d;
typedef struct _GstGvaMono3dClass GstGvaMono3dClass;

struct _GstGvaMono3d {
    GstBaseTransform parent;

    gchar *model;
    gchar *device;
    gchar *calibration_file;
    gfloat threshold; /* < 0 means "use the model's rt_info default" */
    guint topk;

    GstVideoInfo video_info;
    GMutex mutex;
    gboolean initialized;
    gboolean calibration_sent; /* P2 broadcast downstream as a sticky event only once */
    gboolean use_va_surface;   /* input is VAMemory NV12 -> OpenVINO surface sharing (zero-copy) */
    gboolean compiled;         /* runtime compiled for the negotiated memory path */
    gpointer va_display;       /* GstVaDisplay* obtained via GstContext sharing */
    gpointer runtime; /* MonoDetrRuntime* */
};

struct _GstGvaMono3dClass {
    GstBaseTransformClass parent_class;
};

GType gst_gva_mono3d_get_type(void);

G_END_DECLS
