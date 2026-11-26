/*******************************************************************************
 * Copyright (C) 2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

/*
 * Livox LiDAR backend (Livox-SDK2), e.g. Mid-360 / Mid-360S / HAP.
 *
 * Implements the vendor-neutral C ABI in <dlstreamer/lidar/g3d_lidar_backend_api.h>.
 * g3dlidarsrc dlopen()s this library when the config `vendor` is "livox".
 *
 * Unlike rs_driver, Livox-SDK2 delivers one callback per UDP packet (~96 points,
 * ~0.5 ms of data), not per frame. This backend therefore assembles frames
 * itself: every point carries a device timestamp (packet timestamp + i * point
 * interval), and a frame is cut whenever a point crosses a `frame_period_ms`
 * boundary of the device clock. When the LiDAR is time-synchronized (PPS/GPS or
 * PTP) those boundaries are aligned to whole seconds, so with a 100 ms period
 * each frame starts exactly on a PPS-aligned 100 ms tick -- the same ticks a
 * hardware-triggered camera fires on.
 *
 * Livox-SDK2 is a process-wide singleton (global init/uninit, global callbacks),
 * so only one instance of this backend may be active per process.
 */

#include <livox_lidar_api.h>
#include <livox_lidar_def.h>

#include <dlstreamer/lidar/g3d_lidar_backend_api.h>

#include <nlohmann/json.hpp>

#include <arpa/inet.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <climits>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <mutex>
#include <new>
#include <stdexcept>
#include <string>
#include <vector>

using json = nlohmann::json;

namespace {

/* Supported models -> top-level key Livox-SDK2 expects in its JSON config. The
 * SDK only talks to devices whose type matches a key present in the config, so
 * a Mid-360S configured as "MID360" is silently ignored. */
struct ModelInfo {
    const char *model;
    const char *sdk_cfg_key;
};
const ModelInfo kModels[] = {
    {"MID360", "MID360"},
    {"MID360S", "Mid360s"},
    {"HAP", "HAP"},
};

/* Livox factory UDP ports (LiDAR side / host side). */
constexpr int kLidarCmdPort = 56100, kLidarPushPort = 56200, kLidarPointPort = 56300, kLidarImuPort = 56400,
              kLidarLogPort = 56500;
constexpr int kHostCmdPort = 56101, kHostPushPort = 56201, kHostPointPort = 56301, kHostImuPort = 56401,
              kHostLogPort = 56501;

struct LivoxParams {
    std::string lidar_ip; /* empty = accept the first LiDAR discovered */
    unsigned frame_period_ms = 100;
    float min_range = 0.0f; /* metres, 0 = disabled */
    float max_range = 0.0f; /* metres, 0 = disabled */
    bool sdk_log = false;   /* keep Livox-SDK2 console logging enabled */
};

uint64_t realtime_ns() {
    timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

std::string to_upper(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)std::toupper(c); });
    return s;
}

void set_err(char *err_buf, int err_size, const std::string &msg) {
    if (err_buf && err_size > 0)
        std::snprintf(err_buf, (size_t)err_size, "%s", msg.c_str());
}

} // namespace

/* Backend instance behind the opaque g3d_lidar_backend_handle. */
struct g3d_lidar_backend_handle {
    g3d_lidar_cloud_cb cloud_cb = nullptr;
    g3d_lidar_error_cb error_cb = nullptr;
    void *user = nullptr;

    LivoxParams params;
    bool use_lidar_clock = false;
    uint64_t period_ns = 100000000ull;

    /* SDK handle of the LiDAR we stream from. Livox-SDK2 uses the device IPv4
     * address (network byte order, as returned by inet_addr) as the handle. */
    uint32_t target_handle = 0;             /* fixed from params.lidar_ip, 0 = any */
    std::atomic<uint32_t> active_handle{0}; /* latched first device when target_handle == 0 */

    bool sdk_inited = false;
    std::atomic<bool> running{false};

    /* Frame assembly state; touched by the SDK point thread and by start(). */
    std::mutex frame_mutex;
    std::vector<float> flat;
    int64_t cur_frame_id = -1;     /* device_time / period of the frame being built */
    bool cur_frame_partial = true; /* first frame after (re)start is incomplete -> dropped */

    /* host_realtime - device_time, estimated as the minimum observed
     * (arrival time - last point time) over the current and previous second. */
    int64_t offset_min_cur = INT64_MAX;
    int64_t offset_min_prev = INT64_MAX;
    uint64_t offset_bucket = 0;

    std::atomic<int> last_work_state{-1};
};

namespace {

/* Only one backend instance may own the (global) Livox SDK at a time. */
std::atomic<g3d_lidar_backend_handle *> g_sdk_owner{nullptr};

void report(g3d_lidar_backend_handle *h, const std::string &msg) {
    if (h->error_cb)
        h->error_cb(h->user, msg.c_str());
}

std::string ip_of_handle(uint32_t handle) {
    in_addr a;
    a.s_addr = handle;
    char buf[INET_ADDRSTRLEN] = {0};
    inet_ntop(AF_INET, &a, buf, sizeof(buf));
    return buf;
}

bool accept_handle(g3d_lidar_backend_handle *h, uint32_t handle) {
    if (h->target_handle != 0)
        return handle == h->target_handle;
    uint32_t expected = 0;
    if (h->active_handle.compare_exchange_strong(expected, handle))
        return true;
    return expected == handle;
}

/* Hand the assembled frame to the element and reset the buffer. Caller holds frame_mutex. */
void emit_frame(g3d_lidar_backend_handle *h) {
    const uint32_t n = (uint32_t)(h->flat.size() / 4);
    if (n > 0 && !h->cur_frame_partial && h->cloud_cb) {
        const uint64_t start_dev_ns = (uint64_t)h->cur_frame_id * h->period_ns;
        double ts;
        if (h->use_lidar_clock) {
            ts = (double)start_dev_ns / 1e9;
        } else {
            int64_t off = std::min(h->offset_min_cur, h->offset_min_prev);
            if (off == INT64_MAX)
                off = 0;
            ts = ((double)start_dev_ns + (double)off) / 1e9;
        }
        g3d_lidar_frame frame;
        frame.xyzi = h->flat.data();
        frame.point_count = n;
        frame.timestamp = ts;
        h->cloud_cb(h->user, &frame);
    }
    h->flat.clear();
    h->cur_frame_partial = false;
}

template <typename P>
void decode_point(const P &pt, float scale, g3d_lidar_backend_handle *h) {
    if (pt.x == 0 && pt.y == 0 && pt.z == 0)
        return; /* no return for this beam */
    const float x = (float)pt.x * scale, y = (float)pt.y * scale, z = (float)pt.z * scale;
    if (h->params.min_range > 0.0f || h->params.max_range > 0.0f) {
        const float r2 = x * x + y * y + z * z;
        if (h->params.min_range > 0.0f && r2 < h->params.min_range * h->params.min_range)
            return;
        if (h->params.max_range > 0.0f && r2 > h->params.max_range * h->params.max_range)
            return;
    }
    h->flat.push_back(x);
    h->flat.push_back(y);
    h->flat.push_back(z);
    h->flat.push_back((float)pt.reflectivity);
}

template <typename P>
void process_points(g3d_lidar_backend_handle *h, const LivoxLidarEthernetPacket *pkt, uint64_t pkt_ts, float scale) {
    const uint32_t n = pkt->dot_num;
    const P *pts = reinterpret_cast<const P *>(pkt->data);
    const double step_ns = (double)pkt->time_interval * 100.0 / (double)n; /* time_interval unit: 0.1 us */

    std::lock_guard<std::mutex> lock(h->frame_mutex);
    for (uint32_t i = 0; i < n; i++) {
        const uint64_t t = pkt_ts + (uint64_t)((double)i * step_ns);
        const int64_t fid = (int64_t)(t / h->period_ns);
        if (h->cur_frame_id < 0) {
            h->cur_frame_id = fid;
        } else if (fid != h->cur_frame_id) {
            /* Non-consecutive frame id: clock jump (e.g. time sync acquired) or
             * lost data. Both the frame being built and the one starting now are
             * incomplete, so drop them. */
            const bool jump = fid != h->cur_frame_id + 1;
            if (jump)
                h->cur_frame_partial = true;
            emit_frame(h);
            if (jump)
                h->cur_frame_partial = true;
            h->cur_frame_id = fid;
        }
        decode_point(pts[i], scale, h);
    }
}

void on_point_cloud(const uint32_t handle, const uint8_t /*dev_type*/, LivoxLidarEthernetPacket *pkt, void *client) {
    auto *h = static_cast<g3d_lidar_backend_handle *>(client);
    if (!h || !pkt || !h->running.load(std::memory_order_acquire))
        return;
    if (!accept_handle(h, handle))
        return;

    size_t point_size;
    float scale;
    if (pkt->data_type == kLivoxLidarCartesianCoordinateHighData) {
        point_size = sizeof(LivoxLidarCartesianHighRawPoint);
        scale = 0.001f; /* mm -> m */
    } else if (pkt->data_type == kLivoxLidarCartesianCoordinateLowData) {
        point_size = sizeof(LivoxLidarCartesianLowRawPoint);
        scale = 0.01f; /* cm -> m */
    } else {
        return; /* spherical / IMU data is not supported */
    }
    const uint32_t n = pkt->dot_num;
    if (n == 0 || pkt->length < offsetof(LivoxLidarEthernetPacket, data) + (size_t)n * point_size)
        return;

    uint64_t pkt_ts;
    std::memcpy(&pkt_ts, pkt->timestamp, sizeof(pkt_ts));

    /* Clock offset estimate (host realtime - device time) for use_lidar_clock == 0.
     * Network/stack latency only ever adds delay, so the minimum is the best estimate. */
    const uint64_t last_pt_ts = pkt_ts + (uint64_t)pkt->time_interval * 100ull;
    const int64_t off = (int64_t)(realtime_ns() - last_pt_ts);
    const uint64_t bucket = last_pt_ts / 1000000000ull;
    {
        std::lock_guard<std::mutex> lock(h->frame_mutex);
        if (bucket != h->offset_bucket) {
            h->offset_min_prev = (bucket == h->offset_bucket + 1) ? h->offset_min_cur : INT64_MAX;
            h->offset_min_cur = INT64_MAX;
            h->offset_bucket = bucket;
        }
        h->offset_min_cur = std::min(h->offset_min_cur, off);
    }

    if (pkt->data_type == kLivoxLidarCartesianCoordinateHighData)
        process_points<LivoxLidarCartesianHighRawPoint>(h, pkt, pkt_ts, scale);
    else
        process_points<LivoxLidarCartesianLowRawPoint>(h, pkt, pkt_ts, scale);
}

void on_async_result(livox_status status, uint32_t handle, LivoxLidarAsyncControlResponse *resp, void *client) {
    auto *h = static_cast<g3d_lidar_backend_handle *>(client);
    if (!h)
        return;
    if (status != kLivoxLidarStatusSuccess || (resp && resp->ret_code != 0)) {
        report(h, "Livox LiDAR " + ip_of_handle(handle) +
                      ": configuration command failed (status=" + std::to_string((int)status) +
                      ", ret_code=" + std::to_string(resp ? (int)resp->ret_code : -1) + ")");
    }
}

void on_info_change(const uint32_t handle, const LivoxLidarInfo *info, void *client) {
    auto *h = static_cast<g3d_lidar_backend_handle *>(client);
    if (!h || !info)
        return;
    if (!accept_handle(h, handle)) {
        report(h, std::string("Livox: ignoring LiDAR ") + info->lidar_ip + " (SN " + info->sn +
                      "); only one device per element is supported, set params.lidar_ip to choose");
        return;
    }
    /* Cartesian (mm) points, sampling on. Both are no-ops if already set. */
    SetLivoxLidarPclDataType(handle, kLivoxLidarCartesianCoordinateHighData, on_async_result, h);
    SetLivoxLidarWorkMode(handle, kLivoxLidarNormal, on_async_result, h);
}

/* Periodic status push (JSON). Surface transitions into/out of the error state,
 * e.g. scan-module faults that stop point output until the unit is power-cycled. */
void on_status_push(const uint32_t handle, const uint8_t /*dev_type*/, const char *info, void *client) {
    auto *h = static_cast<g3d_lidar_backend_handle *>(client);
    if (!h || !info || !accept_handle(h, handle))
        return;
    json j = json::parse(info, nullptr, /*allow_exceptions=*/false);
    if (!j.is_object() || !j.contains("cur_work_state") || !j["cur_work_state"].is_number_integer())
        return;
    const int state = j["cur_work_state"].get<int>();
    const int prev = h->last_work_state.exchange(state);
    if (state == prev)
        return;
    if (state == kLivoxLidarError) {
        std::string hms = j.contains("hms_code") ? j["hms_code"].dump() : "[]";
        report(h, "Livox LiDAR " + ip_of_handle(handle) + " entered ERROR state (no points will be sent), hms_code=" +
                      hms + ". Power-cycling the LiDAR usually recovers it.");
    } else if (prev == kLivoxLidarError) {
        report(h, "Livox LiDAR " + ip_of_handle(handle) + " left ERROR state (cur_work_state=" + std::to_string(state) +
                      ")");
    }
}

LivoxParams parse_params(const char *params_json) {
    LivoxParams out;
    json p = json::object();
    if (params_json && params_json[0] != '\0') {
        p = json::parse(params_json);
        if (!p.is_object())
            throw std::runtime_error("'params' must be a JSON object");
    }

    static const char *const known_keys[] = {"lidar_ip", "frame_period_ms", "min_range", "max_range", "sdk_log"};
    for (auto it = p.begin(); it != p.end(); ++it) {
        bool ok = false;
        for (const char *k : known_keys)
            ok = ok || it.key() == k;
        if (!ok) {
            std::string accepted;
            for (const char *k : known_keys)
                accepted += (accepted.empty() ? "" : ", ") + std::string(k);
            throw std::runtime_error("unknown key 'params." + it.key() +
                                     "' (check for typos; accepted keys for vendor 'livox' are: " + accepted + ")");
        }
    }

    if (p.contains("lidar_ip")) {
        if (!p["lidar_ip"].is_string())
            throw std::runtime_error("params.lidar_ip must be a string");
        out.lidar_ip = p["lidar_ip"].get<std::string>();
        in_addr a;
        if (!out.lidar_ip.empty() && inet_pton(AF_INET, out.lidar_ip.c_str(), &a) != 1)
            throw std::runtime_error("params.lidar_ip is not a valid IPv4 address: '" + out.lidar_ip + "'");
    }
    if (p.contains("frame_period_ms")) {
        if (!p["frame_period_ms"].is_number_unsigned())
            throw std::runtime_error("params.frame_period_ms must be a positive integer");
        out.frame_period_ms = p["frame_period_ms"].get<unsigned>();
        if (out.frame_period_ms < 10 || out.frame_period_ms > 10000)
            throw std::runtime_error("params.frame_period_ms out of range [10, 10000]");
    }
    auto get_range = [&](const char *key) {
        if (!p.contains(key))
            return 0.0f;
        if (!p[key].is_number() || p[key].get<double>() < 0.0)
            throw std::runtime_error(std::string("params.") + key + " must be a non-negative number (metres)");
        return p[key].get<float>();
    };
    out.min_range = get_range("min_range");
    out.max_range = get_range("max_range");
    if (out.max_range > 0.0f && out.max_range <= out.min_range)
        throw std::runtime_error("params.max_range must be greater than params.min_range");
    if (p.contains("sdk_log")) {
        if (!p["sdk_log"].is_boolean())
            throw std::runtime_error("params.sdk_log must be a boolean");
        out.sdk_log = p["sdk_log"].get<bool>();
    }
    return out;
}

/* Livox-SDK2 only accepts a config file path, so render one into a temp file. */
std::string write_sdk_config(const char *cfg_key, const std::string &host_ip) {
    json host = {{"host_ip", host_ip},
                 {"cmd_data_port", kHostCmdPort},
                 {"push_msg_port", kHostPushPort},
                 {"point_data_port", kHostPointPort},
                 {"imu_data_port", kHostImuPort},
                 {"log_data_port", kHostLogPort}};
    /* No multicast_ip: unicast to host_ip (multicast status pushes are not received by the SDK). */
    json cfg = {{cfg_key,
                 {{"lidar_net_info",
                   {{"cmd_data_port", kLidarCmdPort},
                    {"push_msg_port", kLidarPushPort},
                    {"point_data_port", kLidarPointPort},
                    {"imu_data_port", kLidarImuPort},
                    {"log_data_port", kLidarLogPort}}},
                  {"host_net_info", json::array({host})}}}};

    const char *tmpdir = std::getenv("TMPDIR");
    std::string path = std::string(tmpdir && tmpdir[0] ? tmpdir : "/tmp") + "/g3dlidar_livox_XXXXXX";
    std::vector<char> buf(path.begin(), path.end());
    buf.push_back('\0');
    int fd = mkstemp(buf.data());
    if (fd < 0)
        throw std::runtime_error("cannot create temporary Livox SDK config file in " + path);
    const std::string text = cfg.dump(2);
    const bool ok = write(fd, text.data(), text.size()) == (ssize_t)text.size();
    close(fd);
    if (!ok) {
        unlink(buf.data());
        throw std::runtime_error("cannot write temporary Livox SDK config file");
    }
    return std::string(buf.data());
}

void shutdown_sdk(g3d_lidar_backend_handle *h) {
    h->running.store(false, std::memory_order_release);
    if (!h->sdk_inited)
        return;
    SetLivoxLidarPointCloudCallBack(nullptr, nullptr);
    SetLivoxLidarInfoChangeCallback(nullptr, nullptr);
    SetLivoxLidarInfoCallback(nullptr, nullptr);
    LivoxLidarSdkUninit(); /* joins SDK threads: no callbacks run after this */
    h->sdk_inited = false;
    g3d_lidar_backend_handle *self = h;
    g_sdk_owner.compare_exchange_strong(self, nullptr);
}

} // namespace

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
    if (h->sdk_inited)
        return G3D_LIDAR_OK;

    const std::string model = to_upper(params->model);
    const char *cfg_key = nullptr;
    for (const auto &m : kModels)
        if (model == m.model)
            cfg_key = m.sdk_cfg_key;
    if (!cfg_key) {
        std::string accepted;
        for (const auto &m : kModels)
            accepted += (accepted.empty() ? "" : ", ") + std::string(m.model);
        set_err(err_buf, err_size,
                "unsupported Livox model '" + std::string(params->model) + "' (supported: " + accepted + ")");
        return G3D_LIDAR_BAD_PARAMS;
    }

    /* Livox-SDK2 binds its sockets to, and tells the LiDAR to stream to, this
     * address -- a wildcard cannot work here. */
    const std::string host_ip = params->bind_address ? params->bind_address : "";
    in_addr a;
    if (host_ip.empty() || host_ip == "0.0.0.0" || inet_pton(AF_INET, host_ip.c_str(), &a) != 1) {
        set_err(err_buf, err_size,
                "transport.bind_address must be the IPv4 address of the local NIC connected to the Livox LiDAR "
                "(e.g. \"192.168.1.50\"); got '" +
                    host_ip + "'");
        return G3D_LIDAR_BAD_PARAMS;
    }

    try {
        h->params = parse_params(params->params_json);
    } catch (const std::exception &e) {
        set_err(err_buf, err_size, e.what());
        return G3D_LIDAR_BAD_PARAMS;
    }
    h->use_lidar_clock = params->use_lidar_clock != 0;
    h->period_ns = (uint64_t)h->params.frame_period_ms * 1000000ull;
    h->target_handle = h->params.lidar_ip.empty() ? 0 : (uint32_t)inet_addr(h->params.lidar_ip.c_str());
    h->active_handle.store(0);

    g3d_lidar_backend_handle *expected = nullptr;
    if (!g_sdk_owner.compare_exchange_strong(expected, h)) {
        set_err(err_buf, err_size,
                "Livox-SDK2 is already in use by another g3dlidarsrc instance in this process "
                "(only one Livox source per process is supported)");
        return G3D_LIDAR_INIT_FAIL;
    }

    std::string cfg_path;
    try {
        cfg_path = write_sdk_config(cfg_key, host_ip);
    } catch (const std::exception &e) {
        g_sdk_owner.store(nullptr);
        set_err(err_buf, err_size, e.what());
        return G3D_LIDAR_INIT_FAIL;
    }

    if (!h->params.sdk_log)
        DisableLivoxSdkConsoleLogger();
    const bool ok = LivoxLidarSdkInit(cfg_path.c_str());
    unlink(cfg_path.c_str()); /* parsed during init */
    if (!ok) {
        g_sdk_owner.store(nullptr);
        set_err(err_buf, err_size,
                "LivoxLidarSdkInit failed (model='" + std::string(params->model) + "', bind_address='" + host_ip +
                    "'); enable params.sdk_log for details");
        return G3D_LIDAR_INIT_FAIL;
    }
    h->sdk_inited = true;

    SetLivoxLidarPointCloudCallBack(on_point_cloud, h);
    SetLivoxLidarInfoChangeCallback(on_info_change, h);
    SetLivoxLidarInfoCallback(on_status_push, h);
    return G3D_LIDAR_OK;
}

G3D_LIDAR_BACKEND_EXPORT g3d_lidar_error_code g3d_lidar_backend_start(g3d_lidar_backend_handle *h) {
    if (!h)
        return G3D_LIDAR_NULLPTR;
    if (!h->sdk_inited || !LivoxLidarSdkStart())
        return G3D_LIDAR_START_FAIL;
    {
        std::lock_guard<std::mutex> lock(h->frame_mutex);
        h->flat.clear();
        h->flat.reserve(4 * 32768);
        h->cur_frame_id = -1;
        h->cur_frame_partial = true;
    }
    h->running.store(true, std::memory_order_release);
    return G3D_LIDAR_OK;
}

G3D_LIDAR_BACKEND_EXPORT void g3d_lidar_backend_stop(g3d_lidar_backend_handle *h) {
    if (h)
        h->running.store(false, std::memory_order_release);
}

G3D_LIDAR_BACKEND_EXPORT void g3d_lidar_backend_destroy(g3d_lidar_backend_handle *h) {
    if (!h)
        return;
    shutdown_sdk(h);
    delete h;
}

} // extern "C"
