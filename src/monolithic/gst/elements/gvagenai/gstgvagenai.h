/*******************************************************************************
 * Copyright (C) 2025-2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#pragma once

#include <gst/base/gstbasetransform.h>

G_BEGIN_DECLS

#define GST_TYPE_GVAGENAI (gst_gvagenai_get_type())
#define GST_GVAGENAI(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_GVAGENAI, GstGvaGenAI))
#define GST_GVAGENAI_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_GVAGENAI, GstGvaGenAIClass))
#define GST_IS_GVAGENAI(obj) (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_GVAGENAI))
#define GST_IS_GVAGENAI_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_GVAGENAI))

typedef struct _GstGvaGenAI GstGvaGenAI;
typedef struct _GstGvaGenAIClass GstGvaGenAIClass;

struct _GstGvaGenAI {
    GstBaseTransform element;

    gchar *device;
    gchar *model_path;
    gchar *prompt;
    gchar *prompt_path;
    gchar *generation_config;
    gchar *scheduler_config;
    gchar *model_cache_path;
    gdouble frame_rate;
    guint chunk_size;
    gboolean metrics;
    guint frame_counter;

    gboolean prompt_changed; // flag to indicate if prompt was updated and needs to be reloaded
    gchar *prompt_string;
    void *openvino_context;
};

struct _GstGvaGenAIClass {
    GstBaseTransformClass parent_class;
};

GType gst_gvagenai_get_type(void);

G_END_DECLS
