/*******************************************************************************
 * Copyright (C) 2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#ifndef _GST_GVA_MONO3D_H_
#define _GST_GVA_MONO3D_H_

#include "gva_base_inference.h"

#include <gst/base/gstbasetransform.h>

G_BEGIN_DECLS

#define GST_TYPE_GVA_MONO3D (gst_gva_mono3d_get_type())
#define GST_GVA_MONO3D(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_GVA_MONO3D, GstGvaMono3d))
#define GST_GVA_MONO3D_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_GVA_MONO3D, GstGvaMono3dClass))
#define GST_IS_GVA_MONO3D(obj) (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_GVA_MONO3D))
#define GST_IS_GVA_MONO3D_CLASS(obj) (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_GVA_MONO3D))

typedef struct _GstGvaMono3d {
    GvaBaseInference base_inference;
    double threshold;
    gboolean threshold_explicitly_set;
    /* Camera calibration for lifting 2D detections into 3D: KITTI .txt (P2 row) or JSON
     * ("intrinsic_matrix" 3x3 / "projection_matrix" 3x4, optional "image_size"). */
    gchar *calibration_file;
} GstGvaMono3d;

typedef struct _GstGvaMono3dClass {
    GvaBaseInferenceClass base_class;
} GstGvaMono3dClass;

GType gst_gva_mono3d_get_type(void);

G_END_DECLS

#endif
