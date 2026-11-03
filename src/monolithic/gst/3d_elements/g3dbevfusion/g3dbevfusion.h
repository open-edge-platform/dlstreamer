/*******************************************************************************
 * Copyright (C) 2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#pragma once

#include <gst/base/gstbasetransform.h>
#include <gst/gst.h>

G_BEGIN_DECLS

#define GST_TYPE_G3D_BEVFUSION (gst_g3d_bevfusion_get_type())
#define GST_G3D_BEVFUSION(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_G3D_BEVFUSION, GstG3DBevFusion))
#define GST_G3D_BEVFUSION_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_G3D_BEVFUSION, GstG3DBevFusionClass))
#define GST_IS_G3D_BEVFUSION(obj) (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_G3D_BEVFUSION))
#define GST_IS_G3D_BEVFUSION_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_G3D_BEVFUSION))

typedef struct _GstG3DBevFusion GstG3DBevFusion;
typedef struct _GstG3DBevFusionClass GstG3DBevFusionClass;
typedef struct _GstG3DBevFusionPrivate GstG3DBevFusionPrivate;

struct _GstG3DBevFusion {
    GstBaseTransform parent;

    gchar *config;
    gchar *calibration;
    gchar *device;
    gfloat score_threshold;

    GstG3DBevFusionPrivate *priv;
};

struct _GstG3DBevFusionClass {
    GstBaseTransformClass parent_class;
};

GType gst_g3d_bevfusion_get_type(void);

G_END_DECLS
