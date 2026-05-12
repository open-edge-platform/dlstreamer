/*******************************************************************************
 * Copyright (C) 2018-2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#pragma once

#include "gvametapublish_export.h"
#include <gst/base/gstbasetransform.h>

#include <string>

G_BEGIN_DECLS

#define GST_TYPE_GVA_META_PUBLISH_BASE (gva_meta_publish_base_get_type())
#define GVA_META_PUBLISH_BASE(obj)                                                                                     \
    (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_GVA_META_PUBLISH_BASE, GvaMetaPublishBase))
#define GVA_META_PUBLISH_BASE_CLASS(klass)                                                                             \
    (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_GVA_META_PUBLISH_BASE, GvaMetaPublishBaseClass))
#define IS_GVA_META_PUBLISH_BASE(obj) (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_GVA_META_PUBLISH_BASE))
#define IS_GVA_META_PUBLISH_BASE_CLASS(obj) (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_GVA_META_PUBLISH_BASE))
#define GVA_META_PUBLISH_BASE_GET_CLASS(obj)                                                                           \
    (G_TYPE_INSTANCE_GET_CLASS((obj), GST_TYPE_GVA_META_PUBLISH_BASE, GvaMetaPublishBaseClass))

struct GvaMetaPublishBase {
    GstBaseTransform base;
    class GvaMetaPublishBasePrivate *impl;
};

struct GvaMetaPublishBaseClass {
    GstBaseTransformClass base;

    void (*handoff)(GstElement *element, GstBuffer *buf);
    gboolean (*publish)(GvaMetaPublishBase *self, const std::string &message);
};

GVAMETAPUBLISH_EXPORTS GType gva_meta_publish_base_get_type(void);

G_END_DECLS
