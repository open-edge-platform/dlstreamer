/*******************************************************************************
 * Copyright (C) 2018-2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#pragma once

#include <gva_base_inference.h>

#include <gst/base/gstbasetransform.h>

G_BEGIN_DECLS

#define GST_TYPE_GVA_CLASSIFY (gst_gva_classify_get_type())
#define GST_GVA_CLASSIFY(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_GVA_CLASSIFY, GstGvaClassify))
#define GST_GVA_CLASSIFY_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_GVA_CLASSIFY, GstGvaClassifyClass))
#define GST_IS_GVA_CLASSIFY(obj) (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_GVA_CLASSIFY))
#define GST_IS_GVA_CLASSIFY_CLASS(obj) (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_GVA_CLASSIFY))

typedef struct _GstGvaClassify {
    GvaBaseInference base_inference;
    // properties:
    guint reclassify_interval;
    gboolean skip_raw_tensors;

    struct ClassificationHistory *classification_history;
} GstGvaClassify;

typedef struct _GstGvaClassifyClass {
    GvaBaseInferenceClass base_class;
} GstGvaClassifyClass;

GType gst_gva_classify_get_type(void);

G_END_DECLS
