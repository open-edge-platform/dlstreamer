/*******************************************************************************
 * Copyright (C) 2025-2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#pragma once

#include <gst/base/gstbasetransform.h>

#include "backends/genai_config.h"

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

    // Backend construction config (grouped GObject-property storage).
    // Passed directly to the backend factory.
    GenAIBackendConfig config;

    // Pipeline / element behavior (not backend-construction config)
    gchar *prompt;
    gchar *prompt_path;
    gdouble frame_rate;
    guint chunk_size;
    guint frame_counter;
    gdouble input_fps; // input stream fps cached from caps, used to derive VideoMetadata.fps

    gboolean prompt_changed; // flag to indicate if prompt was updated and needs to be reloaded
    gchar *prompt_string;

    // Object-class trigger: forces a frame to VLM when upstream detection/classification
    // metadata (GstAnalyticsODMtd) matches one of these classes, regardless of frame-rate.
    gchar *trigger_classes;             // comma-separated class names, e.g. "person,fire"; NULL/empty = disabled
    gint trigger_mode;                  // GstGvaGenAITriggerMode: any (OR) or all (AND) of trigger_classes
    gdouble trigger_min_confidence;     // minimum ODMtd confidence for a match to count [0.0-1.0]
    gboolean trigger_classes_changed;   // flag: trigger_classes was updated and needs reparsing

    void *backend; // GvaGenAIRuntime * (opaque runtime state; allocated/freed in gstgvagenai.cpp)

    // Last inference result, persisted so gvawatermark renders across frames
    gchar *last_result;
    gfloat last_confidence;
};

struct _GstGvaGenAIClass {
    GstBaseTransformClass parent_class;
};

GType gst_gvagenai_get_type(void);

G_END_DECLS
