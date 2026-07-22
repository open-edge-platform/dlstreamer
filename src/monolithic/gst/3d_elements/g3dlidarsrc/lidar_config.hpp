/*******************************************************************************
 * Copyright (C) 2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

/*
 * LiDAR configuration parsing for g3dlidarsrc.
 *
 * A single JSON config file describes which vendor/model the device is and how
 * its data is transported (UDP today; USB / PCAP / others in the future). The
 * schema is intentionally split into three layers so that adding a new vendor
 * or a new transport is a localized change:
 *
 *   {
 *     "vendor": "robosense",        // vendor identity (which SDK/backend)
 *     "model":  "RSE1",             // model within that vendor
 *     "transport": {                // HOW the data arrives (vendor-neutral)
 *       "type": "udp",              // discriminator: selects the fields below
 *       "bind_address": "0.0.0.0"   // local NIC to bind() to (UDP-generic)
 *     },
 *     "params": {                   // OPEN passthrough: vendor/model-specific
 *       "msop_port": 6699,          // extras, not interpreted here. Each
 *       "difop_port": 7788          // backend reads what it needs.
 *     }
 *   }
 *
 * Design notes:
 *   - "transport.type" is a discriminator. Different models of the same vendor
 *     may use different transports, so transport is NOT tied to vendor.
 *   - The "transport" object only carries vendor-neutral fields (e.g. UDP
 *     bind/multicast addresses). Vendor-specific protocol details such as
 *     RoboSense's MSOP/DIFOP port concept live in "params" and are read by
 *     the corresponding backend with sensible defaults.
 *   - The raw "transport" and "params" JSON objects are preserved so a backend
 *     can read fields this generic parser does not know about, without having
 *     to extend this struct for every vendor.
 */

#ifndef __G3D_LIDAR_CONFIG_HPP__
#define __G3D_LIDAR_CONFIG_HPP__

#include <fstream>
#include <stdexcept>
#include <string>

#include <nlohmann/json.hpp>

namespace g3dlidar {

using json = nlohmann::json;

/* Transport kinds understood by g3dlidarsrc. Extend as new ones are added. */
enum class TransportType {
    UDP,     /* network UDP packets (RoboSense MSOP/DIFOP today) */
    UNKNOWN, /* recognized string but not yet implemented (e.g. usb, pcap) */
};

inline TransportType transport_type_from_string(const std::string &s) {
    if (s == "udp")
        return TransportType::UDP;
    return TransportType::UNKNOWN;
}

/* Parsed transport description.
 *
 * `type_str` always holds the raw discriminator string (for diagnostics and
 * for transports not yet typed). The UDP-generic convenience fields are only
 * valid when `type == TransportType::UDP`. Vendor-specific protocol details
 * (e.g. RoboSense MSOP/DIFOP port numbers) do NOT belong here: they go in
 * `LidarConfig::params` and are read by the matching backend. `raw` keeps the
 * full transport object so a backend can pull additional, transport-specific
 * keys. */
struct TransportConfig {
    std::string type_str;
    TransportType type = TransportType::UNKNOWN;

    /* UDP-generic convenience fields (valid only when type == UDP).
     *   bind_address = local NIC IP to bind() the socket to. The default
     *                  "0.0.0.0" listens on every interface. This is NOT
     *                  the LiDAR device's IP. */
    std::string bind_address = "0.0.0.0";

    json raw; /* full "transport" object, for backend-specific extras */
};

/* Top-level parsed config. */
struct LidarConfig {
    std::string vendor;
    std::string model;
    TransportConfig transport;
    json params; /* open passthrough object (may be empty/null) */

    /* Parse from an already-loaded JSON object. Throws std::runtime_error with
     * a human-readable message on any schema problem. */
    static LidarConfig from_json(const json &root) {
        LidarConfig cfg;

        if (!root.is_object())
            throw std::runtime_error("config root must be a JSON object");

        if (!root.contains("vendor") || !root["vendor"].is_string())
            throw std::runtime_error("config missing required string field 'vendor'");
        cfg.vendor = root["vendor"].get<std::string>();

        if (!root.contains("model") || !root["model"].is_string())
            throw std::runtime_error("config missing required string field 'model'");
        cfg.model = root["model"].get<std::string>();

        if (!root.contains("transport") || !root["transport"].is_object())
            throw std::runtime_error("config missing required object field 'transport'");
        cfg.transport = parse_transport(root["transport"]);

        /* params is optional and free-form */
        if (root.contains("params")) {
            if (!root["params"].is_object())
                throw std::runtime_error("config field 'params' must be an object when present");
            cfg.params = root["params"];
        } else {
            cfg.params = json::object();
        }

        return cfg;
    }

    /* Parse from a config file path. Throws std::runtime_error on I/O or schema
     * errors, with the path included in the message. */
    static LidarConfig from_file(const std::string &path) {
        std::ifstream stream(path);
        if (!stream)
            throw std::runtime_error("failed to open lidar config file: " + path);

        json root;
        try {
            stream >> root;
        } catch (const json::parse_error &e) {
            throw std::runtime_error("invalid JSON in '" + path + "': " + e.what());
        }
        return from_json(root);
    }

  private:
    static TransportConfig parse_transport(const json &t) {
        TransportConfig tc;
        tc.raw = t;

        if (!t.contains("type") || !t["type"].is_string())
            throw std::runtime_error("transport missing required string field 'type'");
        tc.type_str = t["type"].get<std::string>();
        tc.type = transport_type_from_string(tc.type_str);

        switch (tc.type) {
        case TransportType::UDP:
            /* Only vendor-neutral UDP fields here. Vendor-specific port names
             * (MSOP/DIFOP for RoboSense, etc.) live in params, not transport. */
            if (t.contains("bind_address"))
                tc.bind_address = require_string(t, "bind_address");
            break;
        case TransportType::UNKNOWN:
            /* Leave fields at defaults. The caller decides whether to error;
             * keeping the raw object lets a future backend handle it. */
            break;
        }

        return tc;
    }

    static std::string require_string(const json &obj, const char *key) {
        if (!obj[key].is_string())
            throw std::runtime_error(std::string("transport field '") + key + "' must be a string");
        return obj[key].get<std::string>();
    }

    static int require_int(const json &obj, const char *key) {
        if (!obj[key].is_number_integer())
            throw std::runtime_error(std::string("transport field '") + key + "' must be an integer");
        return obj[key].get<int>();
    }
};

} // namespace g3dlidar

#endif /* __G3D_LIDAR_CONFIG_HPP__ */
