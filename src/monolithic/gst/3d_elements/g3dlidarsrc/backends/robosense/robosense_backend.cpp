/*******************************************************************************
 * Copyright (C) 2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

/*
 * RoboSense LiDAR backend (rs_driver).
 *
 * This is the only translation unit that includes rs_driver. It instantiates
 * the header-only C++ templates and implements the vendor-neutral C ABI in
 * <dlstreamer/lidar/g3d_lidar_backend_api.h>, which g3dlidarsrc loads at runtime
 * via dlopen()/dlsym(). The element derives this library's name from the config
 * `vendor` field ("robosense" -> libg3dlidar_robosense.so).
 *
 * Vendor-specific parameters (MSOP/DIFOP ports, range clipping, SDK toggles)
 * are parsed here from the raw `params` JSON string handed down by the element,
 * so the element stays free of any RoboSense-specific schema.
 */

/* Disable PCAP support in rs_driver (we only need real-time hardware input). */
#define DISABLE_PCAP_PARSE

#include <rs_driver/api/lidar_driver.hpp>
#include <rs_driver/msg/point_cloud_msg.hpp>

#include <dlstreamer/lidar/g3d_lidar_backend_api.h>

#include <nlohmann/json.hpp>

#include <cstdio>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

using namespace robosense::lidar;
using json = nlohmann::json;

typedef PointCloudT<PointXYZI> PointCloudMsg;

/* Backend instance behind the opaque g3d_lidar_backend_handle. */
struct g3d_lidar_backend_handle {
    LidarDriver<PointCloudMsg> driver;

    g3d_lidar_cloud_cb cloud_cb = nullptr;
    g3d_lidar_error_cb error_cb = nullptr;
    void *user = nullptr;

    /* Reused across frames to avoid per-frame allocation when flattening the
     * point cloud into the contiguous x,y,z,intensity layout the element wants. */
    std::vector<float> flat;
};

/* rs_driver delivers a completed frame here. Flatten it and hand a borrowed
 * view to the registered C callback; the buffer stays valid only for the call. */
static void put_cloud(g3d_lidar_backend_handle *h, std::shared_ptr<PointCloudMsg> cloud) {
    if (!h->cloud_cb || !cloud)
        return;

    const size_t n = cloud->points.size();
    h->flat.resize(n * 4);
    for (size_t i = 0; i < n; i++) {
        const auto &p = cloud->points[i];
        h->flat[i * 4 + 0] = p.x;
        h->flat[i * 4 + 1] = p.y;
        h->flat[i * 4 + 2] = p.z;
        h->flat[i * 4 + 3] = (float)p.intensity;
    }

    g3d_lidar_frame frame;
    frame.xyzi = h->flat.data();
    frame.point_count = (uint32_t)n;
    frame.timestamp = cloud->timestamp;

    h->cloud_cb(h->user, &frame);
}

/* Parse the vendor-specific `params` object into the rs_driver param struct.
 * Defaults mirror rs_driver's own RSDriverParam defaults. Throws
 * std::runtime_error (with a user-facing message) on any invalid or unknown key. */
static void apply_robosense_params(const char *params_json, RSDriverParam &param) {
    json p = json::object();
    if (params_json && params_json[0] != '\0') {
        p = json::parse(params_json);
        if (!p.is_object())
            throw std::runtime_error("'params' must be a JSON object");
    }

    auto get_bool = [&](const char *key, bool def) {
        if (!p.contains(key))
            return def;
        if (!p[key].is_boolean())
            throw std::runtime_error(std::string("params.") + key + " must be a boolean");
        return p[key].get<bool>();
    };
    auto get_uint = [&](const char *key, unsigned def, unsigned max) {
        if (!p.contains(key))
            return def;
        if (!p[key].is_number_unsigned())
            throw std::runtime_error(std::string("params.") + key + " must be a non-negative integer");
        unsigned v = p[key].get<unsigned>();
        if (v > max)
            throw std::runtime_error(std::string("params.") + key + " out of range");
        return v;
    };
    auto get_float = [&](const char *key, float def) {
        if (!p.contains(key))
            return def;
        if (!p[key].is_number())
            throw std::runtime_error(std::string("params.") + key + " must be a number");
        return p[key].get<float>();
    };

    /* Input params (UDP ports + socket buffer). Defaults match RoboSense
     * factory settings. */
    param.input_param.msop_port = (uint16_t)get_uint("msop_port", 6699, 65535);
    param.input_param.difop_port = (uint16_t)get_uint("difop_port", 7788, 65535);
    param.input_param.socket_recv_buf = get_uint("socket_recv_buf", 106496, 0xFFFFFFFFu);

    /* Decoder params (range clipping, NaN handling, timestamp policy). 0.0 for
     * min/max distance disables clipping (rs_driver default). */
    param.decoder_param.min_distance = get_float("min_distance", 0.0f);
    param.decoder_param.max_distance = get_float("max_distance", 0.0f);
    param.decoder_param.dense_points = get_bool("dense_points", false);
    param.decoder_param.ts_first_point = get_bool("ts_first_point", false);
    param.decoder_param.wait_for_difop = get_bool("wait_for_difop", true);

    /* Reject unknown keys instead of silently ignoring them: a typo like
     * "msop_pot" would otherwise leave the user wondering why their port change
     * had no effect. Update this list whenever a key is added above. */
    static const char *const known_keys[] = {
        "msop_port",    "difop_port",   "socket_recv_buf", "min_distance",
        "max_distance", "dense_points", "ts_first_point",  "wait_for_difop",
    };
    for (auto it = p.begin(); it != p.end(); ++it) {
        bool ok = false;
        for (const char *k : known_keys)
            if (it.key() == k) {
                ok = true;
                break;
            }
        if (!ok) {
            std::string accepted;
            for (size_t i = 0; i < sizeof(known_keys) / sizeof(known_keys[0]); ++i) {
                if (i)
                    accepted += ", ";
                accepted += known_keys[i];
            }
            throw std::runtime_error("unknown key 'params." + it.key() +
                                     "' (check for typos; accepted keys for vendor 'robosense' are: " + accepted + ")");
        }
    }
}

extern "C" {

G3D_LIDAR_BACKEND_EXPORT g3d_lidar_backend_handle *g3d_lidar_backend_create(void) {
    return new (std::nothrow) g3d_lidar_backend_handle();
}

G3D_LIDAR_BACKEND_EXPORT g3d_lidar_error_code g3d_lidar_backend_set_callbacks(g3d_lidar_backend_handle *h,
                                                                              g3d_lidar_cloud_cb on_cloud,
                                                                              g3d_lidar_error_cb on_error, void *user) {
    if (!h)
        return G3D_LIDAR_NULLPTR;
    h->cloud_cb = on_cloud;
    h->error_cb = on_error;
    h->user = user;
    return G3D_LIDAR_OK;
}

G3D_LIDAR_BACKEND_EXPORT g3d_lidar_error_code g3d_lidar_backend_init(g3d_lidar_backend_handle *h,
                                                                     const g3d_lidar_params *params, char *err_buf,
                                                                     int err_size) {
    if (!h || !params || !params->model)
        return G3D_LIDAR_NULLPTR;

    RSDriverParam param;
    param.lidar_type = strToLidarType(params->model);
    param.input_type = InputType::ONLINE_LIDAR;

    if (params->bind_address)
        param.input_param.host_address = params->bind_address; /* rs_driver calls it "host_address" */
    param.decoder_param.use_lidar_clock = params->use_lidar_clock != 0;

    try {
        apply_robosense_params(params->params_json, param);
    } catch (const std::exception &e) {
        if (err_buf && err_size > 0)
            std::snprintf(err_buf, (size_t)err_size, "%s", e.what());
        return G3D_LIDAR_BAD_PARAMS;
    }

    /* Register callbacks: get_cloud supplies an empty buffer, put_cloud bridges
     * the completed frame to the C callback. */
    h->driver.regPointCloudCallback(
        []() -> std::shared_ptr<PointCloudMsg> { return std::make_shared<PointCloudMsg>(); },
        [h](std::shared_ptr<PointCloudMsg> cloud) { put_cloud(h, cloud); });

    h->driver.regExceptionCallback([h](const Error &e) {
        if (h->error_cb)
            h->error_cb(h->user, e.toString().c_str());
    });

    if (!h->driver.init(param)) {
        if (err_buf && err_size > 0)
            std::snprintf(err_buf, (size_t)err_size, "rs_driver init failed (model='%s')", params->model);
        return G3D_LIDAR_INIT_FAIL;
    }

    return G3D_LIDAR_OK;
}

G3D_LIDAR_BACKEND_EXPORT g3d_lidar_error_code g3d_lidar_backend_start(g3d_lidar_backend_handle *h) {
    if (!h)
        return G3D_LIDAR_NULLPTR;
    if (!h->driver.start())
        return G3D_LIDAR_START_FAIL;
    return G3D_LIDAR_OK;
}

G3D_LIDAR_BACKEND_EXPORT void g3d_lidar_backend_stop(g3d_lidar_backend_handle *h) {
    if (h)
        h->driver.stop();
}

G3D_LIDAR_BACKEND_EXPORT void g3d_lidar_backend_destroy(g3d_lidar_backend_handle *h) {
    if (!h)
        return;
    h->driver.stop();
    delete h;
}

} // extern "C"
