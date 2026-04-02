/*******************************************************************************
 * Copyright (C) 2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#ifndef __GST_G3D_INFERENCE_H__
#define __GST_G3D_INFERENCE_H__

#include <gst/base/gstbasetransform.h>
#include <gst/gst.h>

G_BEGIN_DECLS

#define GST_TYPE_G3D_INFERENCE (gst_g3d_inference_get_type())
#define GST_G3D_INFERENCE(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_G3D_INFERENCE, GstG3DInference))
#define GST_G3D_INFERENCE_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_G3D_INFERENCE, GstG3DInferenceClass))
#define GST_IS_G3D_INFERENCE(obj) (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_G3D_INFERENCE))
#define GST_IS_G3D_INFERENCE_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_G3D_INFERENCE))

typedef struct _GstG3DInference GstG3DInference;
typedef struct _GstG3DInferenceClass GstG3DInferenceClass;

struct _GstG3DInference {
    GstBaseTransform parent;

    gchar *config;
    gchar *device;
    gchar *model_type;
    gfloat score_threshold;

    GMutex mutex;
    gboolean initialized;
    gpointer runtime;
};

struct _GstG3DInferenceClass {
    GstBaseTransformClass parent_class;
};

GType gst_g3d_inference_get_type(void);

G_END_DECLS

#endif /* __GST_G3D_INFERENCE_H__ */