/*******************************************************************************
 * Copyright (C) 2021-2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#pragma once

#include <gst/gst.h>

G_BEGIN_DECLS

#define GVA_META_PUBLISH_NAME "Generic metadata publisher"
#define GVA_META_PUBLISH_DESCRIPTION "Publishes the JSON metadata to MQTT or Kafka message brokers or files."

#define GST_TYPE_GVA_META_PUBLISH (gva_meta_publish_get_type())
#define GVA_META_PUBLISH(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_GVA_META_PUBLISH, GvaMetaPublish))
#define GVA_META_PUBLISH_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_GVA_META_PUBLISH, GvaMetaPublishClass))
#define GST_IS_GVA_META_PUBLISH(obj) (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_GVA_META_PUBLISH))
#define GST_IS_GVA_META_PUBLISH_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_GVA_META_PUBLISH))

typedef enum { GVA_META_PUBLISH_FILE = 1, GVA_META_PUBLISH_MQTT = 2, GVA_META_PUBLISH_KAFKA = 3 } PublishMethodType;

struct GvaMetaPublish {
    GstBin base;
    class GvaMetaPublishPrivate *impl;
};

struct GvaMetaPublishClass {
    GstBinClass base;
};

GType gva_meta_publish_get_type(void);

G_END_DECLS
