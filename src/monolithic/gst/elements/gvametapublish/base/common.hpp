/*******************************************************************************
 * Copyright (C) 2021-2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#pragma once

#include <gst/gst.h>

extern GstStaticPadTemplate gva_meta_publish_sink_template;
extern GstStaticPadTemplate gva_meta_publish_src_template;

typedef enum { GVA_META_PUBLISH_JSON = 1, GVA_META_PUBLISH_JSON_LINES = 2 } FileFormat;

// File specific constants
constexpr auto STDOUT = "stdout";
constexpr auto DEFAULT_FILE_PATH = STDOUT;
constexpr auto DEFAULT_FILE_FORMAT = GVA_META_PUBLISH_JSON;

// Enum value names
constexpr auto UNKNOWN_VALUE_NAME = "unknown";

constexpr auto PUBLISH_METHOD_FILE_NAME = "file";
constexpr auto PUBLISH_METHOD_MQTT_NAME = "mqtt";
constexpr auto PUBLISH_METHOD_KAFKA_NAME = "kafka";

constexpr auto FILE_FORMAT_JSON_NAME = "json";
constexpr auto FILE_FORMAT_JSON_LINES_NAME = "json-lines";

// Broker specific constants
constexpr auto DEFAULT_ADDRESS = "";
constexpr auto DEFAULT_MQTTCLIENTID = "";
constexpr auto DEFAULT_MQTTUSER = "";
constexpr auto DEFAULT_MQTTPASSWORD = "";
constexpr auto DEFAULT_MQTTCONFIG_FILE = "";

constexpr auto DEFAULT_TOPIC = "";
constexpr auto DEFAULT_SIGNAL_HANDOFFS = false;
constexpr auto DEFAULT_MAX_CONNECT_ATTEMPTS = 1;
constexpr auto DEFAULT_MAX_RECONNECT_INTERVAL = 30;

const gchar *file_format_to_string(FileFormat format);

GType gva_metapublish_file_format_get_type(void);
#define GST_TYPE_GVA_METAPUBLISH_FILE_FORMAT (gva_metapublish_file_format_get_type())
