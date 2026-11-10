/*******************************************************************************
 * Copyright (C) 2018-2024 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#ifndef _GST_GVA_INFERENCE_H_
#define _GST_GVA_INFERENCE_H_

#include <gst/base/gstbasetransform.h>

#include "gva_base_inference.h"

G_BEGIN_DECLS

#define GST_TYPE_GVA_INFERENCE (gst_gva_inference_get_type())
#define GST_GVA_INFERENCE(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_GVA_INFERENCE, GstGvaInference))
#define GST_GVA_INFERENCE_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_GVA_INFERENCE, GstGvaInferenceClass))
#define GST_IS_GVA_INFERENCE(obj) (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_GVA_INFERENCE))
#define GST_IS_GVA_INFERENCE_CLASS(obj) (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_GVA_INFERENCE))

typedef struct _GstGvaInference {
    GvaBaseInference base_inference;
} GstGvaInference;

typedef struct _GstGvaInferenceClass {
    GvaBaseInferenceClass base_class;
} GstGvaInferenceClass;

GType gst_gva_inference_get_type(void);

G_END_DECLS

#endif
