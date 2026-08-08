/*******************************************************************************
 * Copyright (C) 2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#pragma once

#include <gst/gst.h>
#include <gst/base/gstbasetransform.h>

G_BEGIN_DECLS

typedef struct {
    GstBaseTransform parent;

    struct GvaAnalyticsPrivate *impl;
} GvaAnalytics;

typedef struct {
    GstBaseTransformClass parent_class;
} GvaAnalyticsClass;

GType gva_analytics_get_type();

GST_DEBUG_CATEGORY_EXTERN(gva_analytics_debug_category);

#define GVA_ANALYTICS_CAST(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj), gva_analytics_get_type(), GvaAnalytics))
#define GVA_ANALYTICS_TYPE (gva_analytics_get_type())

G_END_DECLS
