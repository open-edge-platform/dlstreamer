/*******************************************************************************
 * Copyright (C) 2022-2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#pragma once

#include <gst/base/gstbasetransform.h>
#include <gst/gst.h>

G_BEGIN_DECLS

#define TENSOR_SPLIT_BATCH_NAME "Split input tensor (remove batch dimension from tensor shape)"
#define TENSOR_SPLIT_BATCH_DESCRIPTION TENSOR_SPLIT_BATCH_NAME

#define GST_TYPE_TENSOR_SPLIT_BATCH (batch_split_get_type())
#define TENSOR_SPLIT_BATCH(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_TENSOR_SPLIT_BATCH, TensorSplitBatch))
#define TENSOR_SPLIT_BATCH_CLASS(klass)                                                                                \
    (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_TENSOR_SPLIT_BATCH, TensorSplitBatchClass))
#define GST_IS_TENSOR_SPLIT_BATCH(obj) (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_TENSOR_SPLIT_BATCH))
#define GST_IS_TENSOR_SPLIT_BATCH_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_TENSOR_SPLIT_BATCH))

#define STREAMID_CONTEXT_NAME "stream_id"
#define STREAMID_CONTEXT_FIELD_NAME "stream_id"

typedef struct _TensorSplitBatch {
    GstBaseTransform base;
} TensorSplitBatch;

typedef struct _TensorSplitBatchClass {
    GstBaseTransformClass base_class;
} TensorSplitBatchClass;

GType batch_split_get_type(void);

G_END_DECLS
