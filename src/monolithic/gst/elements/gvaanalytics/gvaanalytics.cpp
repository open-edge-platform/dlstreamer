/*******************************************************************************
 * Copyright (C) 2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#include "config.h"

#include "dlstreamer/gst/metadata/gva_tripwire_meta.h"
#include "dlstreamer/gst/metadata/gva_zone_meta.h"
#include "gvaanalytics.h"
#include "gvaanalyticsimpl.h"

#include <gst/analytics/gstanalyticsmeta.h>
#include <gst/base/gstbasetransform.h>
#include <gst/gst.h>
#include <gst/video/video.h>

#include "dlstreamer/gst/utils.h"

#include <fstream>
#include <map>
#include <memory>
#include <nlohmann/json.hpp>
#include <vector>

using json = nlohmann::json;

GST_DEBUG_CATEGORY_STATIC(gva_analytics_debug_category);
#define GST_CAT_DEFAULT gva_analytics_debug_category

enum {
    PROP_0,
    PROP_CONFIG,
    PROP_ZONES,
    PROP_TRIPWIRES,
    PROP_DRAW_ZONES,
    PROP_DRAW_TRIPWIRES,
};

static void gva_analytics_set_property(GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec);
static void gva_analytics_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec);
static void gva_analytics_finalize(GObject *object);
static gboolean gva_analytics_start(GstBaseTransform *base);
static gboolean gva_analytics_stop(GstBaseTransform *base);
static GstFlowReturn gva_analytics_transform_ip(GstBaseTransform *base, GstBuffer *buf);

#define GVA_ANALYTICS_GET_TYPE(type_name) G_DEFINE_TYPE(GvaAnalytics, type_name, GST_TYPE_BASE_TRANSFORM)

struct GvaAnalyticsPrivate {
    gchar *config_path;
    gchar *zones_json;
    gchar *tripwires_json;
    gboolean draw_zones;
    gboolean draw_tripwires;

    std::vector<Tripwire> tripwires;
    std::vector<Zone> zones;
    std::map<guint64, ObjectTrackingState> tracking_states;
};

static GstStaticPadTemplate gva_analytics_sink_template =
    GST_STATIC_PAD_TEMPLATE("sink", GST_PAD_SINK, GST_PAD_ALWAYS, GST_STATIC_CAPS_ANY);

static GstStaticPadTemplate gva_analytics_src_template =
    GST_STATIC_PAD_TEMPLATE("src", GST_PAD_SRC, GST_PAD_ALWAYS, GST_STATIC_CAPS_ANY);

static void gva_analytics_class_init(GvaAnalyticsClass *klass) {
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
    GstBaseTransformClass *base_transform_class = GST_BASE_TRANSFORM_CLASS(klass);
    GstElementClass *element_class = GST_ELEMENT_CLASS(klass);

    gst_element_class_set_static_metadata(element_class, "GVA Analytics", "Generic", "GVA Analytics element",
                                          "Intel Corporation");

    gst_element_class_add_pad_template(element_class, gst_static_pad_template_get(&gva_analytics_sink_template));
    gst_element_class_add_pad_template(element_class, gst_static_pad_template_get(&gva_analytics_src_template));

    gobject_class->set_property = gva_analytics_set_property;
    gobject_class->get_property = gva_analytics_get_property;
    gobject_class->finalize = gva_analytics_finalize;

    // Install properties
    g_object_class_install_property(gobject_class, PROP_CONFIG,
                                    g_param_spec_string("config", "Config", "Path to JSON configuration file", NULL,
                                                        (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(gobject_class, PROP_ZONES,
                                    g_param_spec_string("zones", "Zones", "Inline JSON zones configuration", NULL,
                                                        (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(gobject_class, PROP_TRIPWIRES,
                                    g_param_spec_string("tripwires", "Tripwires", "Inline JSON tripwires configuration",
                                                        NULL,
                                                        (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(gobject_class, PROP_DRAW_ZONES,
                                    g_param_spec_boolean("draw-zones", "Draw Zones",
                                                         "Attach watermark metadata for drawing zones", TRUE,
                                                         (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(gobject_class, PROP_DRAW_TRIPWIRES,
                                    g_param_spec_boolean("draw-tripwires", "Draw Tripwires",
                                                         "Attach watermark metadata for drawing tripwires", TRUE,
                                                         (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    base_transform_class->start = GST_DEBUG_FUNCPTR(gva_analytics_start);
    base_transform_class->stop = GST_DEBUG_FUNCPTR(gva_analytics_stop);
    base_transform_class->transform_ip = GST_DEBUG_FUNCPTR(gva_analytics_transform_ip);

    base_transform_class->passthrough_on_same_caps = TRUE;

    GST_DEBUG_CATEGORY_INIT(gva_analytics_debug_category, "gvaanalytics", 0, "GVA Analytics element");
}

static void gva_analytics_init(GvaAnalytics *self) {
    GvaAnalyticsPrivate *priv = g_new0(GvaAnalyticsPrivate, 1);
    new (priv) GvaAnalyticsPrivate();

    priv->config_path = NULL;
    priv->zones_json = NULL;
    priv->tripwires_json = NULL;
    priv->draw_zones = TRUE;
    priv->draw_tripwires = TRUE;

    self->impl = priv;
}

static void gva_analytics_finalize(GObject *object) {
    GvaAnalytics *self = GVA_ANALYTICS_CAST(object);
    GvaAnalyticsPrivate *priv = self->impl;

    if (priv) {
        g_free(priv->config_path);
        g_free(priv->zones_json);
        g_free(priv->tripwires_json);
        priv->~GvaAnalyticsPrivate();
        g_free(priv);
        self->impl = nullptr;
    }

    G_OBJECT_CLASS(g_type_class_peek_parent(G_OBJECT_GET_CLASS(object)))->finalize(object);
}

static void gva_analytics_set_property(GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec) {
    GvaAnalytics *self = GVA_ANALYTICS_CAST(object);
    GvaAnalyticsPrivate *priv = self->impl;

    switch (prop_id) {
    case PROP_CONFIG: {
        if (priv->config_path)
            g_free(priv->config_path);
        priv->config_path = g_value_dup_string(value);
        GST_DEBUG_OBJECT(object, "Set config: %s", priv->config_path);
        break;
    }
    case PROP_ZONES: {
        if (priv->zones_json)
            g_free(priv->zones_json);
        priv->zones_json = g_value_dup_string(value);
        GST_DEBUG_OBJECT(object, "Set zones JSON");
        break;
    }
    case PROP_TRIPWIRES: {
        if (priv->tripwires_json)
            g_free(priv->tripwires_json);
        priv->tripwires_json = g_value_dup_string(value);
        GST_DEBUG_OBJECT(object, "Set tripwires JSON");
        break;
    }
    case PROP_DRAW_ZONES: {
        priv->draw_zones = g_value_get_boolean(value);
        GST_DEBUG_OBJECT(object, "Set draw-zones: %s", priv->draw_zones ? "true" : "false");
        break;
    }
    case PROP_DRAW_TRIPWIRES: {
        priv->draw_tripwires = g_value_get_boolean(value);
        GST_DEBUG_OBJECT(object, "Set draw-tripwires: %s", priv->draw_tripwires ? "true" : "false");
        break;
    }
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
        break;
    }
}

static void gva_analytics_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec) {
    GvaAnalytics *self = GVA_ANALYTICS_CAST(object);
    GvaAnalyticsPrivate *priv = self->impl;

    switch (prop_id) {
    case PROP_CONFIG:
        g_value_set_string(value, priv->config_path);
        break;
    case PROP_ZONES:
        g_value_set_string(value, priv->zones_json);
        break;
    case PROP_TRIPWIRES:
        g_value_set_string(value, priv->tripwires_json);
        break;
    case PROP_DRAW_ZONES:
        g_value_set_boolean(value, priv->draw_zones);
        break;
    case PROP_DRAW_TRIPWIRES:
        g_value_set_boolean(value, priv->draw_tripwires);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
        break;
    }
}

static gboolean gva_analytics_start(GstBaseTransform *base) {
    GvaAnalytics *self = GVA_ANALYTICS_CAST(base);
    GvaAnalyticsPrivate *priv = self->impl;

    GST_DEBUG_OBJECT(base, "Starting gva_analytics");

    // Load configuration from file if specified
    if (priv->config_path) {
        try {
            std::ifstream config_file(priv->config_path);
            if (!config_file.is_open()) {
                GST_ERROR_OBJECT(base, "Failed to open config file: %s", priv->config_path);
                return FALSE;
            }

            json config = json::parse(config_file);

            // Parse zones from config
            if (config.contains("zones")) {
                for (const auto &zone_obj : config["zones"]) {
                    Zone zone;
                    zone.id = zone_obj["id"].get<std::string>();

                    // Determine zone type
                    std::string type_str = "polygon"; // default
                    if (zone_obj.contains("type")) {
                        type_str = zone_obj["type"].get<std::string>();
                    }

                    if (type_str == "circle") {
                        zone.type = CIRCLE;
                        zone.center.x = zone_obj["center"]["x"].get<int>();
                        zone.center.y = zone_obj["center"]["y"].get<int>();
                        zone.radius = zone_obj["radius"].get<int>();
                        GST_DEBUG_OBJECT(base, "Loaded circular zone '%s' at (%d,%d) with radius %d", zone.id.c_str(),
                                         zone.center.x, zone.center.y, zone.radius);
                    } else {
                        zone.type = POLYGON;
                        for (const auto &pt : zone_obj["points"]) {
                            zone.points.push_back({pt["x"].get<int>(), pt["y"].get<int>()});
                        }
                        GST_DEBUG_OBJECT(base, "Loaded polygon zone '%s' with %zu points", zone.id.c_str(),
                                         zone.points.size());
                    }

                    // Parse color if specified
                    if (zone_obj.contains("color")) {
                        auto color = zone_obj["color"];
                        if (color.contains("r"))
                            zone.r = color["r"].get<int>();
                        if (color.contains("g"))
                            zone.g = color["g"].get<int>();
                        if (color.contains("b"))
                            zone.b = color["b"].get<int>();
                    }

                    // Parse thickness if specified
                    if (zone_obj.contains("thickness")) {
                        zone.thickness = zone_obj["thickness"].get<int>();
                    }
                    priv->zones.push_back(zone);
                }
                GST_DEBUG_OBJECT(base, "Loaded %zu zones from config", priv->zones.size());
            }

            // Parse tripwires from config
            if (config.contains("tripwires")) {
                for (const auto &tripwire_obj : config["tripwires"]) {
                    Tripwire tripwire;
                    tripwire.id = tripwire_obj["id"].get<std::string>();
                    for (const auto &pt : tripwire_obj["points"]) {
                        tripwire.points.push_back({pt["x"].get<int>(), pt["y"].get<int>()});
                    }

                    // Parse color if specified
                    if (tripwire_obj.contains("color")) {
                        auto color = tripwire_obj["color"];
                        if (color.contains("r"))
                            tripwire.r = color["r"].get<int>();
                        if (color.contains("g"))
                            tripwire.g = color["g"].get<int>();
                        if (color.contains("b"))
                            tripwire.b = color["b"].get<int>();
                    }

                    // Parse thickness if specified
                    if (tripwire_obj.contains("thickness")) {
                        tripwire.thickness = tripwire_obj["thickness"].get<int>();
                    }

                    priv->tripwires.push_back(tripwire);
                }
                GST_DEBUG_OBJECT(base, "Loaded %zu tripwires from config", priv->tripwires.size());
            }
        } catch (const std::exception &e) {
            GST_ERROR_OBJECT(base, "Error parsing config file: %s", e.what());
            return FALSE;
        }
    }

    // Parse zones from inline JSON if specified
    if (priv->zones_json) {
        try {
            json zones_data = json::parse(priv->zones_json);
            for (const auto &zone_obj : zones_data) {
                Zone zone;
                zone.id = zone_obj["id"].get<std::string>();

                // Determine zone type
                std::string type_str = "polygon"; // default
                if (zone_obj.contains("type")) {
                    type_str = zone_obj["type"].get<std::string>();
                }

                if (type_str == "circle") {
                    zone.type = CIRCLE;
                    zone.center.x = zone_obj["center"]["x"].get<int>();
                    zone.center.y = zone_obj["center"]["y"].get<int>();
                    zone.radius = zone_obj["radius"].get<int>();
                    GST_DEBUG_OBJECT(base, "Loaded circular zone '%s' at (%d,%d) with radius %d from inline JSON",
                                     zone.id.c_str(), zone.center.x, zone.center.y, zone.radius);
                } else {
                    zone.type = POLYGON;
                    for (const auto &pt : zone_obj["points"]) {
                        zone.points.push_back({pt["x"].get<int>(), pt["y"].get<int>()});
                    }
                    GST_DEBUG_OBJECT(base, "Loaded polygon zone '%s' with %zu points from inline JSON", zone.id.c_str(),
                                     zone.points.size());
                }

                // Parse color if specified
                if (zone_obj.contains("color")) {
                    auto color = zone_obj["color"];
                    if (color.contains("r"))
                        zone.r = color["r"].get<int>();
                    if (color.contains("g"))
                        zone.g = color["g"].get<int>();
                    if (color.contains("b"))
                        zone.b = color["b"].get<int>();
                }

                // Parse thickness if specified
                if (zone_obj.contains("thickness")) {
                    zone.thickness = zone_obj["thickness"].get<int>();
                }
                priv->zones.push_back(zone);
            }
            GST_DEBUG_OBJECT(base, "Loaded %zu zones from inline JSON", zones_data.size());
        } catch (const std::exception &e) {
            GST_ERROR_OBJECT(base, "Error parsing zones JSON: %s", e.what());
            return FALSE;
        }
    }

    // Parse tripwires from inline JSON if specified
    if (priv->tripwires_json) {
        try {
            json tripwires_data = json::parse(priv->tripwires_json);
            for (const auto &tripwire_obj : tripwires_data) {
                Tripwire tripwire;
                tripwire.id = tripwire_obj["id"].get<std::string>();
                for (const auto &pt : tripwire_obj["points"]) {
                    tripwire.points.push_back({pt["x"].get<int>(), pt["y"].get<int>()});
                }

                // Parse color if specified
                if (tripwire_obj.contains("color")) {
                    auto color = tripwire_obj["color"];
                    if (color.contains("r"))
                        tripwire.r = color["r"].get<int>();
                    if (color.contains("g"))
                        tripwire.g = color["g"].get<int>();
                    if (color.contains("b"))
                        tripwire.b = color["b"].get<int>();
                }

                // Parse thickness if specified
                if (tripwire_obj.contains("thickness")) {
                    tripwire.thickness = tripwire_obj["thickness"].get<int>();
                }

                priv->tripwires.push_back(tripwire);
            }
            GST_DEBUG_OBJECT(base, "Loaded %zu tripwires from inline JSON", tripwires_data.size());
        } catch (const std::exception &e) {
            GST_ERROR_OBJECT(base, "Error parsing tripwires JSON: %s", e.what());
            return FALSE;
        }
    }

    GST_INFO_OBJECT(base, "Initialized with %zu zones and %zu tripwires", priv->zones.size(), priv->tripwires.size());

    return TRUE;
}

static gboolean gva_analytics_stop(GstBaseTransform *base) {
    GvaAnalytics *self = GVA_ANALYTICS_CAST(base);
    GvaAnalyticsPrivate *priv = self->impl;

    GST_DEBUG_OBJECT(base, "Stopping gva_analytics");

    priv->zones.clear();
    priv->tripwires.clear();
    priv->tracking_states.clear();

    return TRUE;
}

static GstFlowReturn gva_analytics_transform_ip(GstBaseTransform *base, GstBuffer *buf) {
    GvaAnalytics *self = GVA_ANALYTICS_CAST(base);
    GvaAnalyticsPrivate *priv = self->impl;

    if (!buf) {
        return GST_FLOW_ERROR;
    }

    // Extract and process tracking metadata from buffer
    GstAnalyticsRelationMeta *analytics_meta = gst_buffer_get_analytics_relation_meta(buf);

    // Process detection metadata and check for zone membership and tripwire crossings
    process_object_detections(base, analytics_meta, priv->zones, priv->tripwires, priv->tracking_states);

    // Always add zone drawing metadata if draw_zones is enabled
    if (priv->draw_zones && !priv->zones.empty()) {
        attach_zone_drawing_metadata(base, buf, priv->zones);
    }

    // Always add tripwire drawing metadata if draw_tripwires is enabled
    if (priv->draw_tripwires && !priv->tripwires.empty()) {
        attach_tripwire_drawing_metadata(base, buf, priv->tripwires);
    }

    GST_DEBUG_OBJECT(base, "Processing buffer with %zu zones and %zu tripwires", priv->zones.size(),
                     priv->tripwires.size());

    return GST_FLOW_OK;
}

GVA_ANALYTICS_GET_TYPE(gva_analytics)
