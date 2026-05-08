/*******************************************************************************
 * Copyright (C) 2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#pragma once

#include <gst/base/gstbasetransform.h>

G_BEGIN_DECLS

typedef struct {
    GstBaseTransform parent;

    struct GvaAnaliticsPrivate *impl;
} GvaAnalitics;

typedef struct {
    GstBaseTransformClass parent_class;
} GvaAnaliticsClass;

GType gva_analitics_get_type();

#define GVA_ANALITICS_CAST(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj), gva_analitics_get_type(), GvaAnalitics))
#define GVA_ANALITICS_TYPE (gva_analitics_get_type())

G_END_DECLS
