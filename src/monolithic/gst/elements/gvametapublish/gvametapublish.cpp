/*******************************************************************************
 * Copyright (C) 2021-2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#include "gvametapublish.hpp"

#include <common.hpp>

#include <cstdint>
#include <stdexcept>
#include <string>

GST_DEBUG_CATEGORY_STATIC(gva_meta_publish_debug_category);
#define GST_CAT_DEFAULT gva_meta_publish_debug_category

namespace {
constexpr auto DEFAULT_PUBLISH_METHOD = GVA_META_PUBLISH_FILE;

const gchar *method_type_to_string(PublishMethodType method_type) {
    switch (method_type) {
    case GVA_META_PUBLISH_FILE:
        return PUBLISH_METHOD_FILE_NAME;
    case GVA_META_PUBLISH_MQTT:
        return PUBLISH_METHOD_MQTT_NAME;
    case GVA_META_PUBLISH_KAFKA:
        return PUBLISH_METHOD_KAFKA_NAME;
    default:
        return UNKNOWN_VALUE_NAME;
    }
}
} // namespace

/* Properties */
enum {
    PROP_0,
    PROP_PUBLISH_METHOD,
    PROP_FILE_PATH,
    PROP_FILE_FORMAT,
    PROP_ADDRESS,
    PROP_MQTTCLIENTID,
    PROP_TOPIC,
    PROP_MAX_CONNECT_ATTEMPTS,
    PROP_MAX_RECONNECT_INTERVAL,
    PROP_USERNAME,
    PROP_PASSWORD,
    PROP_JSON_CONFIG_FILE,
    PROP_SIGNAL_HANDOFFS,
};

class GvaMetaPublishPrivate {
  public:
    GvaMetaPublishPrivate(GstBin *base) : _base(base) {
        auto pad_tmpl = gst_static_pad_template_get(&gva_meta_publish_sink_template);
        _sinkpad = gst_ghost_pad_new_no_target_from_template("sink", pad_tmpl);
        gst_object_unref(pad_tmpl);

        pad_tmpl = gst_static_pad_template_get(&gva_meta_publish_src_template);
        _srcpad = gst_ghost_pad_new_no_target_from_template("src", pad_tmpl);
        gst_object_unref(pad_tmpl);

        gst_element_add_pad(GST_ELEMENT(_base), _sinkpad);
        gst_element_add_pad(GST_ELEMENT(_base), _srcpad);
    }

    ~GvaMetaPublishPrivate() = default;

    void set_property(guint prop_id, const GValue *value, GParamSpec *pspec) {
        switch (prop_id) {
        case PROP_FILE_PATH:
            _file_path = g_value_get_string(value);
            break;
        case PROP_FILE_FORMAT:
            _file_format = static_cast<FileFormat>(g_value_get_enum(value));
            break;
        case PROP_PUBLISH_METHOD:
            _method = static_cast<PublishMethodType>(g_value_get_enum(value));
            break;
        case PROP_ADDRESS:
            _address = g_value_get_string(value);
            break;
        case PROP_MQTTCLIENTID:
            _mqtt_client_id = g_value_get_string(value);
            break;
        case PROP_TOPIC:
            _topic = g_value_get_string(value);
            break;
        case PROP_MAX_CONNECT_ATTEMPTS:
            _max_connect_attempts = g_value_get_uint(value);
            break;
        case PROP_MAX_RECONNECT_INTERVAL:
            _max_reconnect_interval = g_value_get_uint(value);
            break;
        case PROP_USERNAME:
            _username = g_value_get_string(value);
            break;
        case PROP_PASSWORD:
            _password = g_value_get_string(value);
            break;
        case PROP_JSON_CONFIG_FILE: // Handle JSON configuration file property
            _json_config_file = g_value_get_string(value);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID(G_OBJECT(_base), prop_id, pspec);
            break;
        }
    }

    void get_property(guint prop_id, GValue *value, GParamSpec *pspec) {
        switch (prop_id) {
        case PROP_FILE_PATH:
            g_value_set_string(value, _file_path.c_str());
            break;
        case PROP_FILE_FORMAT:
            g_value_set_enum(value, _file_format);
            break;
        case PROP_PUBLISH_METHOD:
            g_value_set_enum(value, _method);
            break;
        case PROP_ADDRESS:
            g_value_set_string(value, _address.c_str());
            break;
        case PROP_MQTTCLIENTID:
            g_value_set_string(value, _mqtt_client_id.c_str());
            break;
        case PROP_TOPIC:
            g_value_set_string(value, _topic.c_str());
            break;
        case PROP_MAX_CONNECT_ATTEMPTS:
            g_value_set_uint(value, _max_connect_attempts);
            break;
        case PROP_MAX_RECONNECT_INTERVAL:
            g_value_set_uint(value, _max_reconnect_interval);
            break;
        case PROP_USERNAME: // Handle username property
            g_value_set_string(value, _username.c_str());
            break;
        case PROP_PASSWORD: // Handle password property
            g_value_set_string(value, _password.c_str());
            break;
        case PROP_JSON_CONFIG_FILE: // Handle JSON configuration file property
            g_value_set_string(value, _json_config_file.c_str());
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID(G_OBJECT(_base), prop_id, pspec);
            break;
        }
    }

    bool init_elements() {

        GST_INFO_OBJECT(_base,
                        "%s parameters:\n -- Method: %s\n -- File path: %s\n -- File format: %s\n -- Address: %s\n "
                        "-- Mqtt client ID: %s\n -- Kafka topic: %s\n -- Max connect attempts: %d\n "
                        "-- Max reconnect interval: %d\n -- Username: %s\n -- Password: %s\n -- JSON Config File: %s\n "
                        "-- Signal handoffs: %s\n",
                        GST_ELEMENT_NAME(GST_ELEMENT_CAST(_base)), method_type_to_string(_method), _file_path.c_str(),
                        file_format_to_string(_file_format), _address.c_str(), _mqtt_client_id.c_str(), _topic.c_str(),
                        _max_connect_attempts, _max_reconnect_interval, _username.c_str(), _password.c_str(),
                        _json_config_file.c_str(), _signal_handoffs ? "true" : "false");

        switch (_method) {
        case GVA_META_PUBLISH_FILE:
            if ((_metapublish = gst_element_factory_make("gvametapublishfile", nullptr)))
                g_object_set(_metapublish, "file-format", _file_format, "file-path", _file_path.c_str(), nullptr);
            break;
        case GVA_META_PUBLISH_MQTT:
            if ((_metapublish = gst_element_factory_make("gvametapublishmqtt", nullptr))) {
                g_object_set(_metapublish, "address", _address.c_str(), "client-id", _mqtt_client_id.c_str(), "topic",
                             _topic.c_str(), "max-connect-attempts", _max_connect_attempts, "max-reconnect-interval",
                             _max_reconnect_interval, "username", _username.c_str(), "password", _password.c_str(),
                             "mqtt-config", _json_config_file.c_str(), nullptr);
            }
            break;
        case GVA_META_PUBLISH_KAFKA:
            if ((_metapublish = gst_element_factory_make("gvametapublishkafka", nullptr)))
                g_object_set(_metapublish, "address", _address.c_str(), "topic", _topic.c_str(), "max-connect-attempts",
                             _max_connect_attempts, "max-reconnect-interval", _max_reconnect_interval, nullptr);
            break;
        default:
            GST_ERROR_OBJECT(_base, "Unknown publish method %d (%s)", _method, method_type_to_string(_method));
            return false;
        }

        if (!_metapublish) {
            GST_ERROR_OBJECT(
                _base,
                "Failed to create element for method: %s\n\n"
                "Please refer to 'install_metapublish_dependencies.sh' script to install required dependencies:\n"
                "https://github.com/open-edge-platform/dlstreamer/tree/master/scripts/"
                "install_metapublish_dependencies.sh\n"
                "After installation clear GStreamer registry cache to refresh plugins.\n",
                method_type_to_string(_method));
            return false;
        }
        g_object_set(_metapublish, "signal-handoffs", _signal_handoffs, nullptr);
        gst_bin_add_many(GST_BIN(_base), _metapublish, nullptr);

        bool ret = true;
        GstPad *pad = gst_element_get_static_pad(_metapublish, "src");
        ret &= gst_ghost_pad_set_target(GST_GHOST_PAD_CAST(_srcpad), pad);
        gst_object_unref(pad);

        pad = gst_element_get_static_pad(_metapublish, "sink");
        ret &= gst_ghost_pad_set_target(GST_GHOST_PAD_CAST(_sinkpad), pad);
        gst_object_unref(pad);

        return ret;
    }

  private:
    GstBin *_base;

    GstPad *_srcpad = nullptr;
    GstPad *_sinkpad = nullptr;

    GstElement *_metapublish = nullptr;

    PublishMethodType _method = GVA_META_PUBLISH_FILE;
    std::string _file_path;
    FileFormat _file_format = GVA_META_PUBLISH_JSON;
    std::string _address;
    std::string _mqtt_client_id;
    std::string _topic;
    uint32_t _max_connect_attempts = 0;
    uint32_t _max_reconnect_interval = 0;
    std::string _username;
    std::string _password;
    std::string _json_config_file;
    bool _signal_handoffs = false;
};

G_DEFINE_TYPE_EXTENDED(GvaMetaPublish, gva_meta_publish, GST_TYPE_BIN, 0, G_ADD_PRIVATE(GvaMetaPublish);
                       GST_DEBUG_CATEGORY_INIT(gva_meta_publish_debug_category, "gvametapublish", 0,
                                               "debug category for gvametapublish element"));

#define GST_TYPE_GVA_METAPUBLISH_METHOD (gst_gva_metapublish_method_get_type())
static GType gst_gva_metapublish_method_get_type(void) {
    static GType gva_metapublish_method_type = 0;
    static const GEnumValue method_types[] = {{GVA_META_PUBLISH_FILE, "File publish", PUBLISH_METHOD_FILE_NAME},
                                              {GVA_META_PUBLISH_MQTT, "MQTT publish", PUBLISH_METHOD_MQTT_NAME},
                                              {GVA_META_PUBLISH_KAFKA, "Kafka publish", PUBLISH_METHOD_KAFKA_NAME},
                                              {0, NULL, NULL}};

    if (!gva_metapublish_method_type) {
        gva_metapublish_method_type = g_enum_register_static("GstGVAMetaPublishMethod", method_types);
    }

    return gva_metapublish_method_type;
}

static void gva_meta_publish_init(GvaMetaPublish *self) {
    // Initialize of private data
    auto *priv_memory = gva_meta_publish_get_instance_private(self);
    // This won't be converted to shared ptr because of memory placement
    self->impl = new (priv_memory) GvaMetaPublishPrivate(&self->base);
}

static void gva_meta_publish_finalize(GObject *object) {
    auto self = GVA_META_PUBLISH(object);
    g_assert(self->impl && "Expected valid 'impl' pointer during finalize");

    if (self->impl) {
        // Destroy C++ structure manually
        self->impl->~GvaMetaPublishPrivate();
        self->impl = nullptr;
    }

    G_OBJECT_CLASS(gva_meta_publish_parent_class)->finalize(object);
}

static GstStateChangeReturn gva_meta_publish_change_state(GstElement *element, GstStateChange transition) {
    auto self = GVA_META_PUBLISH(element);

    switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
        if (!self->impl->init_elements())
            return GST_STATE_CHANGE_FAILURE;
        break;
    default:
        break;
    }

    return GST_ELEMENT_CLASS(gva_meta_publish_parent_class)->change_state(element, transition);
}

static void gva_meta_publish_class_init(GvaMetaPublishClass *klass) {
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
    GstElementClass *element_class = GST_ELEMENT_CLASS(klass);

    element_class->change_state = gva_meta_publish_change_state;

    gobject_class->set_property = [](GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec) {
        return GVA_META_PUBLISH(object)->impl->set_property(prop_id, value, pspec);
    };
    gobject_class->get_property = [](GObject *object, guint prop_id, GValue *value, GParamSpec *pspec) {
        return GVA_META_PUBLISH(object)->impl->get_property(prop_id, value, pspec);
    };
    gobject_class->finalize = gva_meta_publish_finalize;

    gst_element_class_add_pad_template(element_class, gst_static_pad_template_get(&gva_meta_publish_src_template));
    gst_element_class_add_pad_template(element_class, gst_static_pad_template_get(&gva_meta_publish_sink_template));

    gst_element_class_set_static_metadata(GST_ELEMENT_CLASS(klass), GVA_META_PUBLISH_NAME, "Metadata",
                                          GVA_META_PUBLISH_DESCRIPTION, "Intel Corporation");

    auto prm_flags = static_cast<GParamFlags>(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_CONSTRUCT);
    g_object_class_install_property(
        gobject_class, PROP_FILE_PATH,
        g_param_spec_string("file-path", "FilePath",
                            "[method= file] Absolute path to output file for publishing inferences.", DEFAULT_FILE_PATH,
                            prm_flags));
    g_object_class_install_property(
        gobject_class, PROP_FILE_FORMAT,
        g_param_spec_enum("file-format", "File Format", "[method= file] Structure of JSON objects in the file",
                          GST_TYPE_GVA_METAPUBLISH_FILE_FORMAT, DEFAULT_FILE_FORMAT, prm_flags));
    g_object_class_install_property(gobject_class, PROP_PUBLISH_METHOD,
                                    g_param_spec_enum("method", "Publish method", "Publishing method",
                                                      GST_TYPE_GVA_METAPUBLISH_METHOD, DEFAULT_PUBLISH_METHOD,
                                                      prm_flags));

    g_object_class_install_property(
        gobject_class, PROP_ADDRESS,
        g_param_spec_string("address", "Address", "[method= kafka | mqtt] Broker address", DEFAULT_ADDRESS, prm_flags));
    g_object_class_install_property(gobject_class, PROP_MQTTCLIENTID,
                                    g_param_spec_string("mqtt-client-id", "MQTT Client ID",
                                                        "[method= mqtt] Unique identifier for the MQTT "
                                                        "client. If not provided, one will be generated for you.",
                                                        DEFAULT_MQTTCLIENTID, prm_flags));
    g_object_class_install_property(gobject_class, PROP_TOPIC,
                                    g_param_spec_string("topic", "Topic",
                                                        "[method= kafka | mqtt] Topic on which to send broker messages",
                                                        DEFAULT_TOPIC, prm_flags));
    g_object_class_install_property(gobject_class, PROP_MAX_CONNECT_ATTEMPTS,
                                    g_param_spec_uint("max-connect-attempts", "Max Connect Attempts",
                                                      "[method= kafka | mqtt] Maximum number of failed connection "
                                                      "attempts before it is considered fatal.",
                                                      1, 10, DEFAULT_MAX_CONNECT_ATTEMPTS, prm_flags));
    g_object_class_install_property(
        gobject_class, PROP_MAX_RECONNECT_INTERVAL,
        g_param_spec_uint("max-reconnect-interval", "Max Reconnect Interval",
                          "[method= kafka | mqtt] Maximum time in seconds between reconnection attempts. Initial "
                          "interval is 1 second and will be doubled on each failure up to this maximum interval.",
                          1, 300, DEFAULT_MAX_RECONNECT_INTERVAL, prm_flags));
    g_object_class_install_property(gobject_class, PROP_USERNAME,
                                    g_param_spec_string("username", "Username",
                                                        "[method= mqtt] Username for MQTT broker authentication",
                                                        DEFAULT_MQTTUSER, prm_flags));
    g_object_class_install_property(gobject_class, PROP_PASSWORD,
                                    g_param_spec_string("password", "Password",
                                                        "[method= mqtt] Password for MQTT broker authentication",
                                                        DEFAULT_MQTTPASSWORD, prm_flags));
    g_object_class_install_property(gobject_class, PROP_JSON_CONFIG_FILE,
                                    g_param_spec_string("mqtt-config", "Config", "[method= mqtt] MQTT config file",
                                                        DEFAULT_MQTTCONFIG_FILE, prm_flags));
}
