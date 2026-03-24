/*******************************************************************************
 * Copyright (C) 2018-2025 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#include "gvametapublishbase.hpp"
#include "common.hpp"

#include <dlstreamer/gst/metadata/g3d_lidar_meta.h>
#include <gva_json_meta.h>
#include <utils.h>

GST_DEBUG_CATEGORY_STATIC(gva_meta_publish_base_debug_category);
#define GST_CAT_DEFAULT gva_meta_publish_base_debug_category

enum {
    SIGNAL_HANDOFF,
    /* FILL ME */
    LAST_SIGNAL
};

enum {
    PROP_0,
    PROP_SIGNAL_HANDOFFS,
};

namespace {
guint gst_interpret_signals[LAST_SIGNAL] = {0};
}

class GvaMetaPublishBasePrivate {
  public:
    GvaMetaPublishBasePrivate(GstBaseTransform *base) : _base(base) {
    }

    ~GvaMetaPublishBasePrivate() = default;

    bool get_property(guint property_id, GValue *value) {
        switch (property_id) {
        case PROP_SIGNAL_HANDOFFS:
            g_value_set_boolean(value, _signal_handoffs);
            break;
        default:
            return false;
        }
        return true;
    }

    gboolean set_property(guint property_id, const GValue *value) {
        switch (property_id) {
        case PROP_SIGNAL_HANDOFFS:
            _signal_handoffs = g_value_get_boolean(value);
            break;
        default:
            return false;
        }
        return true;
    }

    GstFlowReturn transform_ip(GstBuffer *buf) {
        GST_DEBUG_OBJECT(_base, "transform ip");
        auto json_meta = GST_GVA_JSON_META_GET(buf);
        if (_signal_handoffs) {
            GST_DEBUG_OBJECT(_base, "Signal handoffs");
            g_signal_emit(_base, gst_interpret_signals[SIGNAL_HANDOFF], 0, buf);
        }
        if (!json_meta || !json_meta->message) {
            GST_DEBUG_OBJECT(_base, "No JSON metadata");
            return GST_FLOW_OK;
        }

        GvaMetaPublishBaseClass *klass = GVA_META_PUBLISH_BASE_GET_CLASS(_base);
        if (!klass->publish(GVA_META_PUBLISH_BASE(_base), std::string(json_meta->message))) {
            GST_ELEMENT_ERROR(_base, RESOURCE, NOT_FOUND, ("Failed to publish message"), (NULL));
            return GST_FLOW_ERROR;
        }
        return GST_FLOW_OK;
    }

  private:
    GstBaseTransform *_base;

    bool _signal_handoffs = false;
};

/* class initialization */
G_DEFINE_TYPE_EXTENDED(GvaMetaPublishBase, gva_meta_publish_base, GST_TYPE_BASE_TRANSFORM, G_TYPE_FLAG_ABSTRACT,
                       G_ADD_PRIVATE(GvaMetaPublishBase);
                       GST_DEBUG_CATEGORY_INIT(gva_meta_publish_base_debug_category, "gvametapublishbase", 0,
                                               "debug category for gvametapublishbase element"));

static void gva_meta_publish_base_init(GvaMetaPublishBase *self) {
    GST_DEBUG_OBJECT(self, "gva_meta_publish_base_init");

    // Initialize of private data
    auto *priv_memory = gva_meta_publish_base_get_instance_private(self);
    // This won't be converted to shared ptr because of memory placement
    self->impl = new (priv_memory) GvaMetaPublishBasePrivate(&self->base);
}

static void gva_meta_publish_base_finalize(GObject *object) {
    auto self = GVA_META_PUBLISH_BASE(object);
    g_assert(self->impl && "Expected valid 'impl' pointer during finalize");

    if (self->impl) {
        // Destroy C++ structure manually
        self->impl->~GvaMetaPublishBasePrivate();
        self->impl = nullptr;
    }
}

void gva_meta_publish_base_set_property(GObject *object, guint property_id, const GValue *value, GParamSpec *pspec) {
    GvaMetaPublishBase *self = GVA_META_PUBLISH_BASE(object);

    GST_DEBUG_OBJECT(self, "set_property %d", property_id);

    if (!self->impl->set_property(property_id, value))
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
}

void gva_meta_publish_base_get_property(GObject *object, guint property_id, GValue *value, GParamSpec *pspec) {
    GvaMetaPublishBase *self = GVA_META_PUBLISH_BASE(object);

    GST_DEBUG_OBJECT(self, "get_property");

    if (!self->impl->get_property(property_id, value))
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
}

static void gva_meta_publish_base_before_transform(GstBaseTransform *trans, GstBuffer *buffer) {
    GvaMetaPublishBase *self = GVA_META_PUBLISH_BASE(trans);

    GST_DEBUG_OBJECT(self, "before transform");
    if (gst_buffer_get_meta(buffer, LIDAR_META_API_TYPE) && trans->segment.format != GST_FORMAT_TIME) {
        GST_DEBUG_OBJECT(self, "Skipping stream-time sync for LiDAR buffer with non-time segment format");
        return;
    }

    GstClockTime timestamp;

    // TODO: do we need it?
    timestamp = gst_segment_to_stream_time(&trans->segment, GST_FORMAT_TIME, GST_BUFFER_TIMESTAMP(buffer));
    GST_LOG_OBJECT(self, "Got stream time of %d" GST_TIME_FORMAT, GST_TIME_ARGS(timestamp));
    if (GST_CLOCK_TIME_IS_VALID(timestamp))
        gst_object_sync_values(GST_OBJECT(trans), timestamp);
}

static void gva_meta_publish_base_class_init(GvaMetaPublishBaseClass *klass) {
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);

    GstBaseTransformClass *base_transform_class = GST_BASE_TRANSFORM_CLASS(klass);

    gst_element_class_add_pad_template(GST_ELEMENT_CLASS(klass),
                                       gst_static_pad_template_get(&gva_meta_publish_src_template));
    gst_element_class_add_pad_template(GST_ELEMENT_CLASS(klass),
                                       gst_static_pad_template_get(&gva_meta_publish_sink_template));

    gobject_class->set_property = gva_meta_publish_base_set_property;
    gobject_class->get_property = gva_meta_publish_base_get_property;
    gobject_class->finalize = gva_meta_publish_base_finalize;

    base_transform_class->before_transform = gva_meta_publish_base_before_transform;
    base_transform_class->transform_ip = [](GstBaseTransform *base, GstBuffer *buf) {
        return GVA_META_PUBLISH_BASE(base)->impl->transform_ip(buf);
    };

    g_object_class_install_property(
        gobject_class, PROP_SIGNAL_HANDOFFS,
        g_param_spec_boolean("signal-handoffs", "Signal handoffs", "Send signal before pushing the buffer",
                             DEFAULT_SIGNAL_HANDOFFS,
                             static_cast<GParamFlags>(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_CONSTRUCT)));

    gst_interpret_signals[SIGNAL_HANDOFF] = g_signal_new(
        "handoff", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST, G_STRUCT_OFFSET(GvaMetaPublishBaseClass, handoff), NULL,
        NULL, g_cclosure_marshal_generic, G_TYPE_NONE, 1, GST_TYPE_BUFFER | G_SIGNAL_TYPE_STATIC_SCOPE);
}
