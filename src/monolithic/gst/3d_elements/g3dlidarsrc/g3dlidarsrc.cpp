/*******************************************************************************
 * Copyright (C) 2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#include "g3dlidarsrc.h"
#include <dlstreamer/gst/metadata/g3d_lidar_meta.h>
#include <gst/gstinfo.h>
#include <string.h>

/* Disable PCAP support in rs_driver (we only need real-time hardware input) */
#define DISABLE_PCAP_PARSE

#include <rs_driver/api/lidar_driver.hpp>
#include <rs_driver/msg/point_cloud_msg.hpp>

#include "lidar_config.hpp"

#include <memory>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <string>
#include <chrono>
#include <stdexcept>

using namespace robosense::lidar;

typedef PointCloudT<PointXYZI> PointCloudMsg;

GST_DEBUG_CATEGORY_STATIC(gst_g3d_lidar_src_debug);
#define GST_CAT_DEFAULT gst_g3d_lidar_src_debug

/* Property IDs */
enum {
    PROP_0,
    PROP_CONFIG,
    PROP_NTP_SYNC,
    PROP_TIMEOUT,
    PROP_STREAM_ID
};

/* Default values */
#define DEFAULT_CONFIG NULL     /* no config file by default (required at start) */
#define DEFAULT_NTP_SYNC FALSE  /* FALSE = use pipeline clock (same as rtspsrc default) */
#define DEFAULT_TIMEOUT 5000000 /* 5 seconds in microseconds (same as rtspsrc default) */

#define FRAME_QUEUE_TIMEOUT_MS 500      /* Wait timeout in create() */
#define MAX_FRAME_QUEUE_SIZE 10
#define USEC_TO_SEC(us) ((us) / 1000000.0) /* Convert microseconds to seconds */

/* Src pad template */
static GstStaticPadTemplate src_template =
    GST_STATIC_PAD_TEMPLATE("src", GST_PAD_SRC, GST_PAD_ALWAYS, GST_STATIC_CAPS("application/x-lidar"));

/* Private data structure (C++ objects hidden from GObject C header) */
struct _GstG3DLidarSrcPrivate {
    LidarDriver<PointCloudMsg> *driver;

    std::mutex queue_mutex;
    std::condition_variable queue_cond;
    std::queue<std::shared_ptr<PointCloudMsg>> frame_queue;

    gboolean flushing;
    std::chrono::steady_clock::time_point last_frame_time;
};

/* Forward declarations */
static void gst_g3d_lidar_src_set_property(GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec);
static void gst_g3d_lidar_src_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec);
static void gst_g3d_lidar_src_finalize(GObject *object);

static gboolean gst_g3d_lidar_src_start(GstBaseSrc *src);
static gboolean gst_g3d_lidar_src_stop(GstBaseSrc *src);
static gboolean gst_g3d_lidar_src_unlock(GstBaseSrc *src);
static gboolean gst_g3d_lidar_src_unlock_stop(GstBaseSrc *src);
static GstFlowReturn gst_g3d_lidar_src_create(GstPushSrc *src, GstBuffer **buf);

G_DEFINE_TYPE_WITH_PRIVATE(GstG3DLidarSrc, gst_g3d_lidar_src, GST_TYPE_PUSH_SRC);

static void gst_g3d_lidar_src_class_init(GstG3DLidarSrcClass *klass) {
    GST_DEBUG_CATEGORY_INIT(gst_g3d_lidar_src_debug, "g3dlidarsrc", 0, "LiDAR Source Element");

    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
    GstElementClass *gstelement_class = GST_ELEMENT_CLASS(klass);
    GstBaseSrcClass *basesrc_class = GST_BASE_SRC_CLASS(klass);
    GstPushSrcClass *pushsrc_class = GST_PUSH_SRC_CLASS(klass);

    gobject_class->set_property = gst_g3d_lidar_src_set_property;
    gobject_class->get_property = gst_g3d_lidar_src_get_property;
    gobject_class->finalize = gst_g3d_lidar_src_finalize;

    /* Install properties */
    g_object_class_install_property(
        gobject_class, PROP_CONFIG,
        g_param_spec_string("config", "Config",
                            "Path to a JSON config file describing the LiDAR vendor, model and "
                            "transport. "
                            "Required.",
                            DEFAULT_CONFIG, (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(
        gobject_class, PROP_NTP_SYNC,
        g_param_spec_boolean("ntp-sync", "NTP Sync",
                             "Timestamp source for buffer PTS (TRUE = LiDAR clock from DIFOP, "
                             "FALSE = pipeline running time, starts from 0 at PLAYING)",
                             DEFAULT_NTP_SYNC, (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(
        gobject_class, PROP_TIMEOUT,
        g_param_spec_uint64("timeout", "Timeout",
                            "Timeout in microseconds for receiving data (0 = no timeout)",
                            0, G_MAXUINT64, DEFAULT_TIMEOUT,
                            (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(
        gobject_class, PROP_STREAM_ID,
        g_param_spec_uint("stream-id", "Stream ID",
                          "Stream identifier for this LiDAR source (used in metadata)",
                          0, G_MAXUINT, 0, (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    /* Element metadata */
    gst_element_class_set_static_metadata(gstelement_class, "G3D LiDAR Source", "Source/Device",
                                          "Receives real-time LiDAR point cloud data via rs_driver SDK (g3dlidarsrc)",
                                          "Intel Corporation");

    gst_element_class_add_static_pad_template(gstelement_class, &src_template);

    /* Virtual method overrides */
    basesrc_class->start = GST_DEBUG_FUNCPTR(gst_g3d_lidar_src_start);
    basesrc_class->stop = GST_DEBUG_FUNCPTR(gst_g3d_lidar_src_stop);
    basesrc_class->unlock = GST_DEBUG_FUNCPTR(gst_g3d_lidar_src_unlock);
    basesrc_class->unlock_stop = GST_DEBUG_FUNCPTR(gst_g3d_lidar_src_unlock_stop);
    pushsrc_class->create = GST_DEBUG_FUNCPTR(gst_g3d_lidar_src_create);

    /* Mark inherited GstBaseSrc properties that are NOT meaningful here as
     * deprecated, so `gst-inspect` flags them and users don't waste time on
     * them. blocksize/do-timestamp are also actively reset at start() with a
     * WARNING when the user tries to override them — see start() below.
     * `typefind` is already deprecated upstream.
     *
     * num-buffers and automatic-eos are intentionally left alone — they are
     * useful (e.g. `num-buffers=10` for tests) and have well-defined behavior
     * for our element. */
    static const char *deprecated_inherited[] = {
        "blocksize", "do-timestamp", "typefind", NULL
    };
    for (const char **p = deprecated_inherited; *p != NULL; p++) {
        GParamSpec *pspec = g_object_class_find_property(gobject_class, *p);
        if (pspec)
            pspec->flags = (GParamFlags)(pspec->flags | G_PARAM_DEPRECATED);
    }

    /* Note: gst_base_src_set_live() and gst_base_src_set_format()
     * are called in gst_g3d_lidar_src_init() on the instance, not here */
}

static void gst_g3d_lidar_src_init(GstG3DLidarSrc *self) {
    self->config = g_strdup(DEFAULT_CONFIG);
    self->ntp_sync = DEFAULT_NTP_SYNC;
    self->timeout = DEFAULT_TIMEOUT;

    self->stream_id = 0;
    self->frame_seq = 0;

    self->priv = (GstG3DLidarSrcPrivate *)gst_g3d_lidar_src_get_instance_private(self);
    new (&self->priv->queue_mutex) std::mutex();
    new (&self->priv->queue_cond) std::condition_variable();
    new (&self->priv->frame_queue) std::queue<std::shared_ptr<PointCloudMsg>>();
    self->priv->driver = nullptr;
    self->priv->flushing = FALSE;

    /* Configure as live source */
    gst_base_src_set_live(GST_BASE_SRC(self), TRUE);
    gst_base_src_set_format(GST_BASE_SRC(self), GST_FORMAT_TIME);

    /* Pin inherited GstBaseSrc properties that have no meaningful behavior
     * here. start() additionally checks if the user changed them and emits a
     * GST_ELEMENT_WARNING + reset, so any override is observable on the bus.
     *
     *   blocksize    : we implement create() not fill(); blocksize is unused.
     *   do-timestamp : we set PTS ourselves via ntp-sync; do-timestamp would
     *                  silently override that and break ntp-sync semantics.
     *   typefind     : already deprecated upstream and non-functional.
     *
     * num-buffers and automatic-eos are intentionally NOT locked: they are
     * useful (e.g. `num-buffers=10` for tests) and behave correctly for this
     * element. Users may set them as usual.
     */
    gst_base_src_set_blocksize(GST_BASE_SRC(self), 0);
    gst_base_src_set_do_timestamp(GST_BASE_SRC(self), FALSE);
    g_object_set(G_OBJECT(self), "typefind", FALSE, NULL);
}

static void gst_g3d_lidar_src_finalize(GObject *object) {
    GstG3DLidarSrc *self = GST_G3D_LIDAR_SRC(object);

    g_free(self->config);

    /* Destruct C++ objects */
    self->priv->frame_queue.~queue();
    self->priv->queue_cond.~condition_variable();
    self->priv->queue_mutex.~mutex();

    G_OBJECT_CLASS(gst_g3d_lidar_src_parent_class)->finalize(object);
}

static void gst_g3d_lidar_src_set_property(GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec) {
    GstG3DLidarSrc *self = GST_G3D_LIDAR_SRC(object);

    switch (prop_id) {
    case PROP_CONFIG:
        g_free(self->config);
        self->config = g_value_dup_string(value);
        break;
    case PROP_NTP_SYNC:
        self->ntp_sync = g_value_get_boolean(value);
        break;
    case PROP_TIMEOUT:
        self->timeout = g_value_get_uint64(value);
        break;
    case PROP_STREAM_ID:
        self->stream_id = g_value_get_uint(value);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
        break;
    }
}

static void gst_g3d_lidar_src_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec) {
    GstG3DLidarSrc *self = GST_G3D_LIDAR_SRC(object);

    switch (prop_id) {
    case PROP_CONFIG:
        g_value_set_string(value, self->config);
        break;
    case PROP_NTP_SYNC:
        g_value_set_boolean(value, self->ntp_sync);
        break;
    case PROP_TIMEOUT:
        g_value_set_uint64(value, self->timeout);
        break;
    case PROP_STREAM_ID:
        g_value_set_uint(value, self->stream_id);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
        break;
    }
}

/* rs_driver point cloud callback: push completed frame to our queue */
static void on_put_cloud(GstG3DLidarSrc *self, std::shared_ptr<PointCloudMsg> cloud) {
    std::lock_guard<std::mutex> lock(self->priv->queue_mutex);

    /* Record frame reception time */
    self->priv->last_frame_time = std::chrono::steady_clock::now();

    /* Drop oldest frame if queue is full to avoid unbounded growth */
    if (self->priv->frame_queue.size() >= MAX_FRAME_QUEUE_SIZE) {
        self->priv->frame_queue.pop();
        GST_WARNING_OBJECT(self, "Frame queue full, dropping oldest frame");
    }

    self->priv->frame_queue.push(cloud);
    self->priv->queue_cond.notify_one();
}

static gboolean gst_g3d_lidar_src_start(GstBaseSrc *src) {
    GstG3DLidarSrc *self = GST_G3D_LIDAR_SRC(src);

    /* If the user set any of the inapplicable inherited properties, surface a
     * WARNING on the bus and reset to our pinned values. We do this before any
     * real work so the warning shows up clearly in logs. Inherited pspecs
     * dispatch into the parent class, so a sub-class set_property override
     * can't catch the write — we have to detect it after the fact. */
    {
        GstBaseSrc *base = GST_BASE_SRC(self);
        if (gst_base_src_get_blocksize(base) != 0) {
            GST_ELEMENT_WARNING(self, RESOURCE, SETTINGS, (NULL),
                ("'blocksize' is not applicable to g3dlidarsrc (this element produces "
                 "frame-sized point-cloud buffers via create(), not byte-stream chunks). "
                 "Ignoring user value and resetting to 0."));
            gst_base_src_set_blocksize(base, 0);
        }
        if (gst_base_src_get_do_timestamp(base)) {
            GST_ELEMENT_WARNING(self, RESOURCE, SETTINGS, (NULL),
                ("'do-timestamp=true' would override the PTS this element computes "
                 "and break the 'ntp-sync' property. Use 'ntp-sync' to choose the "
                 "timestamp source. Ignoring user value and resetting to false."));
            gst_base_src_set_do_timestamp(base, FALSE);
        }
    }

    /* A config file is mandatory: it declares vendor, model and transport. */
    if (self->config == NULL || self->config[0] == '\0') {
        GST_ELEMENT_ERROR(self, RESOURCE, SETTINGS, (NULL),
                          ("No config set. Provide a JSON config via the 'config' property, e.g. "
                           "config=/path/to/lidar.json"));
        return FALSE;
    }

    /* Parse the JSON config (vendor / model / transport / params). */
    g3dlidar::LidarConfig cfg;
    try {
        cfg = g3dlidar::LidarConfig::from_file(std::string(self->config));
    } catch (const std::exception &e) {
        GST_ELEMENT_ERROR(self, RESOURCE, SETTINGS, (NULL),
                          ("Failed to parse config '%s': %s", self->config, e.what()));
        return FALSE;
    }

    /* Vendor selects the SDK backend. Only RoboSense is implemented today;
     * add other vendors as new branches here. */
    if (cfg.vendor != "robosense") {
        GST_ELEMENT_ERROR(self, RESOURCE, NOT_FOUND, (NULL),
                          ("Unsupported vendor: '%s' (only 'robosense' is implemented)", cfg.vendor.c_str()));
        return FALSE;
    }

    /* RoboSense (rs_driver) currently supports the UDP transport only.
     * Other transports (e.g. usb) are reserved: error out clearly until
     * a backend implements them. */
    if (cfg.transport.type != g3dlidar::TransportType::UDP) {
        GST_ELEMENT_ERROR(self, RESOURCE, NOT_FOUND, (NULL),
                          ("Unsupported transport '%s' for vendor 'robosense' "
                           "(only 'udp' is implemented)",
                           cfg.transport.type_str.c_str()));
        return FALSE;
    }

    /* Map vendor identity / transport into RSDriverParam, then read the open
     * `params` object for RoboSense-specific overrides. Anything in `params` is
     * optional: omitted keys keep the rs_driver defaults. Mechanical-only
     * decoder fields (start_angle / end_angle / split_*) are intentionally NOT
     * exposed yet — the only hardware tested is the RSE1 (MEMS). */
    RSDriverParam param;
    param.lidar_type = strToLidarType(cfg.model);
    param.input_type = InputType::ONLINE_LIDAR;
    /* rs_driver calls this field "host_address"; in our schema it's exposed as
     * the unambiguous "bind_address" (local NIC to bind to). We do not expose
     * multicast (group_address): unicast covers virtually all deployments and
     * rs_driver's default ("0.0.0.0") is exactly that. Add it back as a
     * transport field if a real multicast use case appears. */
    param.input_param.host_address = cfg.transport.bind_address;
    /* Pipeline clock vs. LiDAR clock is exposed as the GObject `ntp-sync`
     * property, not via params, so it overrides any params.use_lidar_clock. */
    param.decoder_param.use_lidar_clock = self->ntp_sync;

    try {
        const auto &p = cfg.params;
        auto get_bool = [&](const char *key, bool def) {
            if (!p.contains(key)) return def;
            if (!p[key].is_boolean())
                throw std::runtime_error(std::string("params.") + key + " must be a boolean");
            return p[key].get<bool>();
        };
        auto get_int = [&](const char *key, int def) {
            if (!p.contains(key)) return def;
            if (!p[key].is_number_integer())
                throw std::runtime_error(std::string("params.") + key + " must be an integer");
            return p[key].get<int>();
        };
        auto get_uint = [&](const char *key, unsigned def, unsigned max) {
            if (!p.contains(key)) return def;
            if (!p[key].is_number_unsigned())
                throw std::runtime_error(std::string("params.") + key + " must be a non-negative integer");
            unsigned v = p[key].get<unsigned>();
            if (v > max)
                throw std::runtime_error(std::string("params.") + key + " out of range");
            return v;
        };
        auto get_float = [&](const char *key, float def) {
            if (!p.contains(key)) return def;
            if (!p[key].is_number())
                throw std::runtime_error(std::string("params.") + key + " must be a number");
            return p[key].get<float>();
        };

        /* Mounting-pose transform (rs_driver `transform_param`) is intentionally
         * NOT exposed: rs_driver only honors it when built with
         * -DENABLE_TRANSFORM=ON (off by default), and RoboSense's own docs
         * advise against enabling it in production due to per-point matrix
         * multiplication cost. Apply rigid-body transforms downstream instead. */

        /* Input params (UDP ports + socket buffer). Defaults match the
         * RoboSense factory settings. imu_port is intentionally NOT exposed:
         * the only tested model (RSE1) delivers IMU data inside the DIFOP
         * packet itself, not on a separate UDP stream. user_layer_bytes /
         * tail_layer_bytes are also omitted — they are protocol-framing knobs
         * for customized firmware that we have not validated. */
        param.input_param.msop_port =
            (uint16_t)get_uint("msop_port", 6699, 65535);
        param.input_param.difop_port =
            (uint16_t)get_uint("difop_port", 7788, 65535);
        param.input_param.socket_recv_buf =
            get_uint("socket_recv_buf", 106496, 0xFFFFFFFFu);

        /* Decoder params (range clipping, NaN handling, timestamp policy). */
        param.decoder_param.min_distance =
            get_float("min_distance", param.decoder_param.min_distance);
        param.decoder_param.max_distance =
            get_float("max_distance", param.decoder_param.max_distance);
        param.decoder_param.dense_points =
            get_bool("dense_points", param.decoder_param.dense_points);
        param.decoder_param.ts_first_point =
            get_bool("ts_first_point", param.decoder_param.ts_first_point);
        param.decoder_param.wait_for_difop =
            get_bool("wait_for_difop", param.decoder_param.wait_for_difop);

        /* Reject unknown keys instead of silently ignoring them: a typo like
         * "msop_pot" would otherwise leave the user wondering why their port
         * change had no effect. The error message lists every accepted key so
         * the fix is obvious. Update this list whenever a new key is added
         * above. */
        static const char *const known_keys[] = {
            "msop_port", "difop_port", "socket_recv_buf",
            "min_distance", "max_distance",
            "dense_points", "ts_first_point", "wait_for_difop",
        };
        for (auto it = p.begin(); it != p.end(); ++it) {
            const std::string &key = it.key();
            bool ok = false;
            for (const char *k : known_keys) {
                if (key == k) { ok = true; break; }
            }
            if (!ok) {
                std::string accepted;
                for (size_t i = 0; i < sizeof(known_keys) / sizeof(known_keys[0]); ++i) {
                    if (i) accepted += ", ";
                    accepted += known_keys[i];
                }
                throw std::runtime_error(
                    "unknown key 'params." + key +
                    "' (check for typos; accepted keys for vendor 'robosense' are: " +
                    accepted + ")");
            }
        }
    } catch (const std::exception &e) {
        GST_ELEMENT_ERROR(self, RESOURCE, SETTINGS, (NULL),
                          ("Invalid params in config '%s': %s", self->config, e.what()));
        return FALSE;
    }

    GST_INFO_OBJECT(self,
                    "Starting g3dlidarsrc: vendor=%s model=%s transport=udp "
                    "bind=%s msop=%u difop=%u ntp-sync=%s",
                    cfg.vendor.c_str(), cfg.model.c_str(), cfg.transport.bind_address.c_str(),
                    param.input_param.msop_port, param.input_param.difop_port,
                    self->ntp_sync ? "true" : "false");
    /* Full RSDriverParam dump for debugging non-default params (range clip,
     * transform, etc.). rs_driver's print() writes to its own stderr logger. */
    if (gst_debug_category_get_threshold(gst_g3d_lidar_src_debug) >= GST_LEVEL_DEBUG)
        param.print();

    /* Create driver */
    self->priv->driver = new LidarDriver<PointCloudMsg>();

    /* Register callbacks */
    self->priv->driver->regPointCloudCallback(
        /* get_cloud: provide empty point cloud buffer */
        []() -> std::shared_ptr<PointCloudMsg> { return std::make_shared<PointCloudMsg>(); },
        /* put_cloud: receive completed frame */
        [self](std::shared_ptr<PointCloudMsg> cloud) { on_put_cloud(self, cloud); });

    self->priv->driver->regExceptionCallback([self](const Error &e) {
        GST_WARNING_OBJECT(self, "rs_driver exception: %s", e.toString().c_str());
    });

    /* Init and start */
    if (!self->priv->driver->init(param)) {
        GST_ELEMENT_ERROR(self, RESOURCE, OPEN_READ, (NULL), ("Failed to initialize rs_driver"));
        delete self->priv->driver;
        self->priv->driver = nullptr;
        return FALSE;
    }

    if (!self->priv->driver->start()) {
        GST_ELEMENT_ERROR(self, RESOURCE, OPEN_READ, (NULL), ("Failed to start rs_driver"));
        delete self->priv->driver;
        self->priv->driver = nullptr;
        return FALSE;
    }

    /* Initialize timeout tracking */
    self->priv->flushing = FALSE;
    self->priv->last_frame_time = std::chrono::steady_clock::now();
    self->frame_seq = 0;

    GST_INFO_OBJECT(self, "g3dlidarsrc started successfully, waiting for data from device... "
                    "(timeout=%.1fs)",
                    USEC_TO_SEC(self->timeout));
    return TRUE;
}

static gboolean gst_g3d_lidar_src_stop(GstBaseSrc *src) {
    GstG3DLidarSrc *self = GST_G3D_LIDAR_SRC(src);

    GST_INFO_OBJECT(self, "Stopping g3dlidarsrc");

    if (self->priv->driver) {
        self->priv->driver->stop();
        delete self->priv->driver;
        self->priv->driver = nullptr;
    }

    /* Clear frame queue */
    {
        std::lock_guard<std::mutex> lock(self->priv->queue_mutex);
        while (!self->priv->frame_queue.empty()) {
            self->priv->frame_queue.pop();
        }
    }

    self->frame_seq = 0;
    return TRUE;
}

static gboolean gst_g3d_lidar_src_unlock(GstBaseSrc *src) {
    GstG3DLidarSrc *self = GST_G3D_LIDAR_SRC(src);

    std::lock_guard<std::mutex> lock(self->priv->queue_mutex);
    self->priv->flushing = TRUE;
    self->priv->queue_cond.notify_all();

    return TRUE;
}

static gboolean gst_g3d_lidar_src_unlock_stop(GstBaseSrc *src) {
    GstG3DLidarSrc *self = GST_G3D_LIDAR_SRC(src);

    std::lock_guard<std::mutex> lock(self->priv->queue_mutex);
    self->priv->flushing = FALSE;

    return TRUE;
}

static GstFlowReturn gst_g3d_lidar_src_create(GstPushSrc *src, GstBuffer **buf) {
    GstG3DLidarSrc *self = GST_G3D_LIDAR_SRC(src);
    std::shared_ptr<PointCloudMsg> cloud;

    /* Wait for a frame from rs_driver */
    {
        std::unique_lock<std::mutex> lock(self->priv->queue_mutex);
        while (self->priv->frame_queue.empty()) {
            if (self->priv->flushing) {
                return GST_FLOW_FLUSHING;
            }

            /* Check timeout (like rtspsrc timeout handling) */
            if (self->timeout > 0) {
                auto now = std::chrono::steady_clock::now();
                auto elapsed_us = std::chrono::duration_cast<std::chrono::microseconds>(
                    now - self->priv->last_frame_time).count();

                /* Cast to gint64 to match elapsed_us type from chrono::count() */
                if (elapsed_us >= (gint64)self->timeout) {
                    GST_ELEMENT_ERROR(self, RESOURCE, READ, (NULL),
                        ("Timeout receiving data from LiDAR device. "
                         "No frames received for %.4f seconds. "
                         "Device may not be connected or network may be misconfigured. "
                         "Check: 1) Device is powered on and connected, "
                         "2) Transport settings in config '%s', "
                         "3) Firewall settings.",
                         USEC_TO_SEC(self->timeout),
                         self->config));
                    return GST_FLOW_ERROR;
                }
            }

            /* Wait with timeout to periodically check flushing and timeout */
            self->priv->queue_cond.wait_for(lock, std::chrono::milliseconds(FRAME_QUEUE_TIMEOUT_MS));
        }

        if (self->priv->flushing) {
            return GST_FLOW_FLUSHING;
        }

        cloud = self->priv->frame_queue.front();
        self->priv->frame_queue.pop();
    }

    if (!cloud || cloud->points.empty()) {
        GST_DEBUG_OBJECT(self, "Received empty point cloud, skipping");
        *buf = gst_buffer_new();
        return GST_FLOW_OK;
    }

    /* Convert to float[x, y, z, intensity] array */
    size_t point_count = cloud->points.size();
    size_t payload_size = point_count * 4 * sizeof(float);

    *buf = gst_buffer_new_allocate(NULL, payload_size, NULL);
    if (!*buf) {
        GST_ERROR_OBJECT(self, "Failed to allocate buffer of size %zu", payload_size);
        return GST_FLOW_ERROR;
    }

    GstMapInfo map;
    if (!gst_buffer_map(*buf, &map, GST_MAP_WRITE)) {
        gst_buffer_unref(*buf);
        *buf = NULL;
        GST_ERROR_OBJECT(self, "Failed to map buffer");
        return GST_FLOW_ERROR;
    }

    float *dst = (float *)map.data;
    for (size_t i = 0; i < point_count; i++) {
        dst[i * 4 + 0] = cloud->points[i].x;
        dst[i * 4 + 1] = cloud->points[i].y;
        dst[i * 4 + 2] = cloud->points[i].z;
        dst[i * 4 + 3] = (float)cloud->points[i].intensity;
    }

    gst_buffer_unmap(*buf, &map);

    /* Select timestamp source based on ntp-sync (like rtspsrc) */
    GstClockTime frame_ts = GST_CLOCK_TIME_NONE;

    if (self->ntp_sync) {
        /* Use LiDAR clock (from cloud->timestamp set by rsdriver using DIFOP timestamp) */
        frame_ts = (GstClockTime)(cloud->timestamp * GST_SECOND);
        GST_DEBUG_OBJECT(self, "Using LiDAR timestamp (ntp-sync=true): %.6f seconds -> %" GST_TIME_FORMAT,
                         cloud->timestamp, GST_TIME_ARGS(frame_ts));
    } else {
        /* Use GStreamer pipeline running time (starts from 0 at PLAYING).
         * running_time = clock_time - base_time. Without subtracting base_time
         * the PTS would be the absolute clock value (e.g. CLOCK_MONOTONIC since
         * boot for the default GstSystemClock), which contradicts the
         * "from 0" contract advertised on the ntp-sync property. */
        GstClock *clock = gst_element_get_clock(GST_ELEMENT(self));
        if (clock) {
            GstClockTime now = gst_clock_get_time(clock);
            GstClockTime base = gst_element_get_base_time(GST_ELEMENT(self));
            frame_ts = (now > base) ? (now - base) : 0;
            gst_object_unref(clock);
        } else {
            frame_ts = 0;
        }
        GST_DEBUG_OBJECT(self, "Using GStreamer pipeline running time (ntp-sync=false)");
    }

    GST_BUFFER_PTS(*buf) = frame_ts;
    GST_BUFFER_DTS(*buf) = GST_CLOCK_TIME_NONE;

    /* Attach LidarMeta */
    add_lidar_meta(*buf, (guint)point_count, self->frame_seq, frame_ts, self->stream_id);

    GST_LOG_OBJECT(self, "Frame %zu: %zu points, payload %zu bytes, ts=%" GST_TIME_FORMAT, self->frame_seq,
                   point_count, payload_size, GST_TIME_ARGS(frame_ts));

    self->frame_seq++;

    return GST_FLOW_OK;
}
