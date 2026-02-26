/*******************************************************************************
 * Copyright (C) 2018-2025 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
using json = nlohmann::json;

#include "gvametapublishmqtt.hpp"

#include <common.hpp>
#include <safe_arithmetic.hpp>

#include <MQTTAsync.h>
#include <uuid/uuid.h>

#include <cstdint>
#include <string>
#include <thread>

GST_DEBUG_CATEGORY_STATIC(gva_meta_publish_mqtt_debug_category);
#define GST_CAT_DEFAULT gva_meta_publish_mqtt_debug_category

namespace {
std::string generate_client_id() {
    uuid_t binuuid;
    uuid_generate_random(binuuid);
    char uuid[37];
    // 36 character UUID string plus terminating character
    uuid_unparse(binuuid, uuid);
    return uuid;
}

} // namespace

/* Properties */
enum {
    PROP_0,
    PROP_ADDRESS,
    PROP_CLIENT_ID,
    PROP_TOPIC,
    PROP_MAX_CONNECT_ATTEMPTS,
    PROP_MAX_RECONNECT_INTERVAL,
    PROP_USERNAME,
    PROP_PASSWORD,
    PROP_JSON_CONFIG_FILE,
};

class GvaMetaPublishMqttPrivate {
  private:
    // MQTT CALLBACKS
    void on_connect_success(MQTTAsync_successData * /*response*/) {
        GST_DEBUG_OBJECT(_base, "Successfully connected to MQTT");
    }

    void on_connect_failure(MQTTAsync_failureData *response) {
        char error_message[256];
        snprintf(error_message, sizeof(error_message), "Connect failed, rc %d\n", response ? response->code : 0);
        GST_WARNING_OBJECT(_base, "%s", error_message);
        try_reconnect();
    }

    void on_connection_lost(char *cause) {
        GST_WARNING_OBJECT(_base, "Connection to MQTT lost. Cause: %s. Attempting to reconnect", cause);
        try_reconnect();
    }

    int on_message_arrived(char * /*topicName*/, int /*topicLen*/, MQTTAsync_message * /*message*/) {
        return TRUE;
    }

    void on_delivery_complete(MQTTAsync_token /*token*/) {
    }

    void on_send_success(MQTTAsync_successData * /*response*/) {
        GST_DEBUG_OBJECT(_base, "Message successfully published to MQTT");
    }

    void on_send_failure(MQTTAsync_failureData * /*response*/) {
        GST_ERROR_OBJECT(_base, "Message failed to publish to MQTT");
    }

    void on_disconnect_success(MQTTAsync_successData * /*response*/) {
        GST_DEBUG_OBJECT(_base, "Successfully disconnected from MQTT.");
    }

    void on_disconnect_failure(MQTTAsync_failureData * /*response*/) {
        GST_ERROR_OBJECT(_base, "Failed to disconnect from MQTT.");
    }

    void try_reconnect() {
        if (_connection_attempt == _max_connect_attempts) {
            GST_ELEMENT_ERROR(_base, RESOURCE, NOT_FOUND,
                              ("Failed to connect to MQTT after maximum configured attempts."), (nullptr));
            return;
        }
        _connection_attempt++;
        _sleep_time = std::min(2 * _sleep_time, _max_reconnect_interval);

        std::this_thread::sleep_for(std::chrono::seconds(_sleep_time));
        GST_DEBUG_OBJECT(_base, "Attempt %d to connect to MQTT.", _connection_attempt);
        auto c = MQTTAsync_connect(_client, &_connect_options);
        if (c != MQTTASYNC_SUCCESS) {
            GST_ERROR_OBJECT(_base, "Failed to start connection attempt to MQTT. Error code %d.", c);
        }
    }

  public:
    GvaMetaPublishMqttPrivate(GvaMetaPublishBase *parent) : _base(parent) {
        _connect_options = MQTTAsync_connectOptions_initializer;
        _connect_options.keepAliveInterval = 20;
        _connect_options.cleansession = 1;
        _connect_options.context = this;
        _connect_options.onSuccess = [](void *context, MQTTAsync_successData *response) {
            if (!context) {
                GST_ERROR("Got null context on mqtt connect_success callback");
                return;
            }
            static_cast<GvaMetaPublishMqttPrivate *>(context)->on_connect_success(response);
        };
        _connect_options.onFailure = [](void *context, MQTTAsync_failureData *response) {
            if (!context) {
                GST_ERROR("Got null context on mqtt connect_failure callback");
                return;
            }
            static_cast<GvaMetaPublishMqttPrivate *>(context)->on_connect_failure(response);
        };

        _disconnect_options = MQTTAsync_disconnectOptions_initializer;
        _disconnect_options.context = this;
        _disconnect_options.onSuccess = [](void *context, MQTTAsync_successData *response) {
            if (!context) {
                GST_ERROR("Got null context on mqtt disconnect_success callback");
                return;
            }
            static_cast<GvaMetaPublishMqttPrivate *>(context)->on_disconnect_success(response);
        };
        _disconnect_options.onFailure = [](void *context, MQTTAsync_failureData *response) {
            if (!context) {
                GST_ERROR("Got null context on mqtt disconnect_failure callback");
                return;
            }
            static_cast<GvaMetaPublishMqttPrivate *>(context)->on_disconnect_failure(response);
        };
    }

    ~GvaMetaPublishMqttPrivate() {
        MQTTAsync_destroy(&_client);
        GST_DEBUG("Successfully freed MQTT client.");
    }

    bool apply_json_config() {
        std::ifstream file(_json_config_file);
        if (!file.is_open()) {
            g_printerr("Unable to open JSON configuration file: %s\n", _json_config_file.c_str());
            return false;
        }

        json j;
        file >> j;

        if (j.contains("address") && !j["address"].is_null()) {
            _address = j["address"].get<std::string>();
        }
        if (j.contains("client-id") && !j["client-id"].is_null()) {
            _client_id = j["client-id"].get<std::string>();
        }
        if (j.contains("topic") && !j["topic"].is_null()) {
            _topic = j["topic"].get<std::string>();
        }
        if (j.contains("username") && !j["username"].is_null()) {
            _username = j["username"].get<std::string>();
        }
        if (j.contains("password") && !j["password"].is_null()) {
            _password = j["password"].get<std::string>();
        }
        if (j.contains("max-connect-attempts") && !j["max-connect-attempts"].is_null()) {
            _max_connect_attempts = j["max-connect-attempts"].get<uint32_t>();
        }
        if (j.contains("max-reconnect-interval") && !j["max-reconnect-interval"].is_null()) {
            _max_reconnect_interval = j["max-reconnect-interval"].get<uint32_t>();
        }
        if (j.contains("TLS") && !j["TLS"].is_null()) {
            _TLS = j["TLS"].get<bool>();
        }
        if (j.contains("ssl_verify") && !j["ssl_verify"].is_null()) {
            _ssl_verify = j["ssl_verify"].get<uint32_t>();
        }
        if (j.contains("ssl_enable_server_cert_auth") && !j["ssl_enable_server_cert_auth"].is_null()) {
            _ssl_enable_server_cert_auth = j["ssl_enable_server_cert_auth"].get<uint32_t>();
        }
        if (j.contains("ssl_CA_certificate") && !j["ssl_CA_certificate"].is_null()) {
            _ssl_CA_certificate = j["ssl_CA_certificate"].get<std::string>();
        }
        if (j.contains("ssl_client_certificate") && !j["ssl_client_certificate"].is_null()) {
            _ssl_client_certificate = j["ssl_client_certificate"].get<std::string>();
        }
        if (j.contains("ssl_private_key") && !j["ssl_private_key"].is_null()) {
            _ssl_private_key = j["ssl_private_key"].get<std::string>();
        }
        if (j.contains("ssl_private_key_pwd") && !j["ssl_private_key_pwd"].is_null()) {
            _ssl_private_key_pwd = j["ssl_private_key_pwd"].get<std::string>();
        }

        return true;
    }

    gboolean start() {
        if (_client_id.empty())
            _client_id = generate_client_id();
        _connection_attempt = 1;
        _sleep_time = 1;

        if (_TLS) {
            const std::string prefix = "ssl://";
            if (_address.compare(0, prefix.length(), prefix) != 0) {
                _address = prefix + _address;
            }
        }

        auto sts =
            MQTTAsync_create(&_client, _address.c_str(), _client_id.c_str(), MQTTCLIENT_PERSISTENCE_NONE, nullptr);
        if (sts != MQTTASYNC_SUCCESS) {
            GST_ERROR_OBJECT(_base, "Failed to create MQTTAsync handler. Error code: %d", sts);
            return false;
        }

        sts = MQTTAsync_setCallbacks(
            _client, this,
            [](void *context, char *cause) {
                if (!context) {
                    GST_ERROR("Got null context on mqtt connect_lost callback");
                    return;
                }
                static_cast<GvaMetaPublishMqttPrivate *>(context)->on_connection_lost(cause);
            },
            [](void *context, char *topicName, int topicLen, MQTTAsync_message *message) {
                if (!context) {
                    GST_ERROR("Got null context on mqtt message_arrived callback");
                    return 0;
                }
                return static_cast<GvaMetaPublishMqttPrivate *>(context)->on_message_arrived(topicName, topicLen,
                                                                                             message);
            },
            [](void *context, MQTTAsync_token token) {
                if (!context) {
                    GST_ERROR("Got null context on mqtt delivery_complete callback");
                    return;
                }
                static_cast<GvaMetaPublishMqttPrivate *>(context)->on_delivery_complete(token);
            });
        if (sts != MQTTASYNC_SUCCESS) {
            GST_ERROR_OBJECT(_base, "Failed to set callbacks for MQTTAsync handler. Error code: %d", sts);
            return false;
        }

        MQTTAsync_SSLOptions sslOptions = MQTTAsync_SSLOptions_initializer;

        if (_TLS) {
            // Set TLS options
            sslOptions.sslVersion = MQTT_SSL_VERSION_TLS_1_2;
            sslOptions.verify = _ssl_verify;
            sslOptions.trustStore = _ssl_CA_certificate.empty() ? nullptr : _ssl_CA_certificate.c_str();
            sslOptions.keyStore = _ssl_client_certificate.empty() ? nullptr : _ssl_client_certificate.c_str();
            sslOptions.privateKey = _ssl_private_key.empty() ? nullptr : _ssl_private_key.c_str();
            sslOptions.enabledCipherSuites =
                "ECDHE-RSA-AES256-GCM-SHA384:ECDHE-ECDSA-AES256-GCM-SHA384"; // recommended cipher suites
            sslOptions.enableServerCertAuth = _ssl_enable_server_cert_auth;

            _connect_options.ssl = &sslOptions;
        }

        auto c = MQTTAsync_connect(_client, &_connect_options);
        if (c != MQTTASYNC_SUCCESS) {
            GST_ERROR_OBJECT(_base, "Failed to start connection attempt to MQTT. Error code %d.", c);
            return false;
        }
        GST_DEBUG_OBJECT(_base, "Connect request sent to MQTT.");
        return true;
    }

    gboolean publish(const std::string &message) {
        MQTTAsync_message mqtt_message = MQTTAsync_message_initializer;
        mqtt_message.payload = const_cast<char *>(message.c_str());
        mqtt_message.payloadlen = safe_convert<int>(message.size());
        mqtt_message.retained = FALSE;

        // TODO Validate message is JSON
        MQTTAsync_responseOptions ro = MQTTAsync_responseOptions_initializer;
        ro.context = this;
        ro.onSuccess = [](void *context, MQTTAsync_successData *response) {
            if (!context) {
                GST_ERROR("Got null context on mqtt success callback");
                return;
            }
            static_cast<GvaMetaPublishMqttPrivate *>(context)->on_send_success(response);
        };
        ro.onFailure = [](void *context, MQTTAsync_failureData *response) {
            if (!context) {
                GST_ERROR("Got null context on mqtt failure callback");
                return;
            }
            static_cast<GvaMetaPublishMqttPrivate *>(context)->on_send_failure(response);
        };

        auto c = MQTTAsync_sendMessage(_client, _topic.c_str(), &mqtt_message, &ro);
        if (c != MQTTASYNC_SUCCESS) {
            GST_ERROR_OBJECT(_base, "Message was not accepted for publication. Error code %d.", c);
            return true;
        }
        GST_DEBUG_OBJECT(_base, "MQTT message sent.");
        return true;
    }

    gboolean stop() {
        if (!MQTTAsync_isConnected(_client)) {
            GST_DEBUG_OBJECT(_base, "MQTT client is not connected. Nothing to disconnect");
            return true;
        }

        auto c = MQTTAsync_disconnect(_client, &_disconnect_options);
        if (c != MQTTASYNC_SUCCESS) {
            GST_ERROR_OBJECT(_base, "Disconnection from MQTT failed with error code %d.", c);
            return false;
        }
        GST_DEBUG_OBJECT(_base, "Disconnect request sent to MQTT.");
        return true;
    }

    bool set_property(guint prop_id, const GValue *value) {
        switch (prop_id) {
        case PROP_ADDRESS:
            _address = g_value_get_string(value);
            break;
        case PROP_CLIENT_ID:
            _client_id = g_value_get_string(value);
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
            return false;
        }

        return true;
    }

    bool get_property(guint prop_id, GValue *value) {
        switch (prop_id) {
        case PROP_ADDRESS:
            g_value_set_string(value, _address.c_str());
            break;
        case PROP_CLIENT_ID:
            g_value_set_string(value, _client_id.c_str());
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
        case PROP_USERNAME:
            g_value_set_string(value, _username.c_str());
            break;
        case PROP_PASSWORD:
            g_value_set_string(value, _password.c_str());
            break;
        case PROP_JSON_CONFIG_FILE: // Handle JSON configuration file property
            g_value_set_string(value, _json_config_file.c_str());
            break;
        default:
            return false;
        }
        return true;
    }

  private:
    GvaMetaPublishBase *_base;

    std::string _address;
    std::string _client_id;
    std::string _topic;
    std::string _username;
    std::string _password;
    uint32_t _max_connect_attempts;
    uint32_t _max_reconnect_interval;

    std::string _json_config_file;

    uint32_t _ssl_verify = 0;
    uint32_t _ssl_enable_server_cert_auth = 0;

    bool _TLS = false;

    std::string _ssl_CA_certificate;
    std::string _ssl_client_certificate;
    std::string _ssl_private_key;
    std::string _ssl_private_key_pwd;

    MQTTAsync _client;
    MQTTAsync_connectOptions _connect_options;
    MQTTAsync_disconnectOptions _disconnect_options;
    uint32_t _connection_attempt;
    uint32_t _sleep_time;
};

G_DEFINE_TYPE_EXTENDED(GvaMetaPublishMqtt, gva_meta_publish_mqtt, GST_TYPE_GVA_META_PUBLISH_BASE, 0,
                       G_ADD_PRIVATE(GvaMetaPublishMqtt);
                       GST_DEBUG_CATEGORY_INIT(gva_meta_publish_mqtt_debug_category, "gvametapublishmqtt", 0,
                                               "debug category for gvametapublishmqtt element"));

static void gva_meta_publish_mqtt_init(GvaMetaPublishMqtt *self) {
    // Initialize of private data
    auto *priv_memory = gva_meta_publish_mqtt_get_instance_private(self);
    // This won't be converted to shared ptr because of memory placement
    self->impl = new (priv_memory) GvaMetaPublishMqttPrivate(&self->base);
}

static void gva_meta_publish_mqtt_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec) {
    auto self = GVA_META_PUBLISH_MQTT(object);

    if (!self->impl->get_property(prop_id, value))
        G_OBJECT_CLASS(gva_meta_publish_mqtt_parent_class)->get_property(object, prop_id, value, pspec);
}

static void gva_meta_publish_mqtt_set_property(GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec) {
    auto self = GVA_META_PUBLISH_MQTT(object);

    if (!self->impl->set_property(prop_id, value))
        G_OBJECT_CLASS(gva_meta_publish_mqtt_parent_class)->set_property(object, prop_id, value, pspec);
}

static void gva_meta_publish_mqtt_finalize(GObject *object) {
    auto self = GVA_META_PUBLISH_MQTT(object);
    g_assert(self->impl && "Expected valid 'impl' pointer during finalize");

    if (self->impl) {
        self->impl->~GvaMetaPublishMqttPrivate();
        self->impl = nullptr;
    }

    G_OBJECT_CLASS(gva_meta_publish_mqtt_parent_class)->finalize(object);
}

static GstStateChangeReturn gva_meta_publish_mqtt_change_state(GstElement *element, GstStateChange transition) {
    GvaMetaPublishMqtt *self = GVA_META_PUBLISH_MQTT(element);

    switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY: {
        GValue json_config_file_value = G_VALUE_INIT;
        g_value_init(&json_config_file_value, G_TYPE_STRING);

        bool res = self->impl->get_property(PROP_JSON_CONFIG_FILE, &json_config_file_value);

        if (res) {
            const gchar *json_config_file = g_value_get_string(&json_config_file_value);
            if (json_config_file && *json_config_file != '\0') {
                if (!self->impl->apply_json_config()) {
                    GST_ERROR_OBJECT(self, "Failed to apply JSON configuration");
                    g_value_unset(&json_config_file_value);
                    return GST_STATE_CHANGE_FAILURE;
                }
            }
        }
        g_value_unset(&json_config_file_value);
        break;
    }
    default:
        break;
    }

    return GST_ELEMENT_CLASS(gva_meta_publish_mqtt_parent_class)->change_state(element, transition);
}

static void gva_meta_publish_mqtt_class_init(GvaMetaPublishMqttClass *klass) {
    auto gobject_class = G_OBJECT_CLASS(klass);
    auto base_transform_class = GST_BASE_TRANSFORM_CLASS(klass);
    auto base_metapublish_class = GVA_META_PUBLISH_BASE_CLASS(klass);

    gobject_class->set_property = gva_meta_publish_mqtt_set_property;
    gobject_class->get_property = gva_meta_publish_mqtt_get_property;
    gobject_class->finalize = gva_meta_publish_mqtt_finalize;

    base_transform_class->start = [](GstBaseTransform *base) { return GVA_META_PUBLISH_MQTT(base)->impl->start(); };
    base_transform_class->stop = [](GstBaseTransform *base) { return GVA_META_PUBLISH_MQTT(base)->impl->stop(); };

    base_metapublish_class->publish = [](GvaMetaPublishBase *base, const std::string &message) {
        return GVA_META_PUBLISH_MQTT(base)->impl->publish(message);
    };

    gst_element_class_set_static_metadata(GST_ELEMENT_CLASS(klass), "Mqtt metadata publisher", "Metadata",
                                          "Publishes the JSON metadata to MQTT message broker", "Intel Corporation");

    auto prm_flags = static_cast<GParamFlags>(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_CONSTRUCT);
    g_object_class_install_property(
        gobject_class, PROP_ADDRESS,
        g_param_spec_string("address", "Address", "Broker address", DEFAULT_ADDRESS, prm_flags));
    g_object_class_install_property(gobject_class, PROP_CLIENT_ID,
                                    g_param_spec_string("client-id", "MQTT Client ID",
                                                        "Unique identifier for the MQTT "
                                                        "client. If not provided, one will be generated for you.",
                                                        DEFAULT_MQTTCLIENTID, prm_flags));
    g_object_class_install_property(
        gobject_class, PROP_TOPIC,
        g_param_spec_string("topic", "Topic", "Topic on which to send broker messages", DEFAULT_TOPIC, prm_flags));
    g_object_class_install_property(gobject_class, PROP_MAX_CONNECT_ATTEMPTS,
                                    g_param_spec_uint("max-connect-attempts", "Max Connect Attempts",
                                                      "Maximum number of failed connection "
                                                      "attempts before it is considered fatal.",
                                                      1, 10, DEFAULT_MAX_CONNECT_ATTEMPTS, prm_flags));
    g_object_class_install_property(
        gobject_class, PROP_MAX_RECONNECT_INTERVAL,
        g_param_spec_uint("max-reconnect-interval", "Max Reconnect Interval",
                          "Maximum time in seconds between reconnection attempts. Initial "
                          "interval is 1 second and will be doubled on each failure up to this maximum interval.",
                          1, 300, DEFAULT_MAX_RECONNECT_INTERVAL, prm_flags));
    g_object_class_install_property(gobject_class, PROP_USERNAME,
                                    g_param_spec_string("username", "Username",
                                                        "Username for MQTT broker authentication", DEFAULT_MQTTUSER,
                                                        prm_flags));

    g_object_class_install_property(gobject_class, PROP_PASSWORD,
                                    g_param_spec_string("password", "Password",
                                                        "Password for MQTT broker authentication", DEFAULT_MQTTPASSWORD,
                                                        prm_flags));

    g_object_class_install_property(gobject_class, PROP_JSON_CONFIG_FILE,
                                    g_param_spec_string("mqtt-config", "Config", "[method= mqtt] MQTT config file",
                                                        DEFAULT_MQTTCONFIG_FILE, prm_flags));

    // Override the state change function
    GstElementClass *element_class = GST_ELEMENT_CLASS(klass);
    element_class->change_state = gva_meta_publish_mqtt_change_state;
}

static gboolean plugin_init(GstPlugin *plugin) {
    gboolean result = TRUE;
    result &= gst_element_register(plugin, "gvametapublishmqtt", GST_RANK_NONE, GST_TYPE_GVA_META_PUBLISH_MQTT);
    return result;
}

GST_PLUGIN_DEFINE(GST_VERSION_MAJOR, GST_VERSION_MINOR, gvametapublishmqtt,
                  PRODUCT_FULL_NAME " MQTT metapublish element", plugin_init, PLUGIN_VERSION, PLUGIN_LICENSE,
                  PACKAGE_NAME, GST_PACKAGE_ORIGIN)
