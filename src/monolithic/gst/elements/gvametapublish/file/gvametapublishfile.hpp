/*******************************************************************************
 * Copyright (C) 2018-2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/
#pragma once

#include <gvametapublishbase.hpp>

G_BEGIN_DECLS

#define GST_TYPE_GVA_META_PUBLISH_FILE (gva_meta_publish_file_get_type())
#define GVA_META_PUBLISH_FILE(obj)                                                                                     \
    (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_GVA_META_PUBLISH_FILE, GvaMetaPublishFile))
#define GVA_META_PUBLISH_FILE_CLASS(klass)                                                                             \
    (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_GVA_META_PUBLISH_FILE, GvaMetaPublishFileClass))
#define IS_GVA_META_PUBLISH_FILE(obj) (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_GVA_META_PUBLISH_FILE))
#define IS_GVA_META_PUBLISH_FILE_CLASS(obj) (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_GVA_META_PUBLISH_FILE))
#define GVA_META_PUBLISH_FILE_GET_CLASS(obj)                                                                           \
    (G_TYPE_INSTANCE_GET_CLASS((obj), GST_TYPE_GVA_META_PUBLISH_FILE, GvaMetaPublishFileClass))

struct GvaMetaPublishFile {
    GvaMetaPublishBase base;
    class GvaMetaPublishFilePrivate *impl;
};

struct GvaMetaPublishFileClass {
    GvaMetaPublishBaseClass base;
};

GType gva_meta_publish_file_get_type(void);

G_END_DECLS
