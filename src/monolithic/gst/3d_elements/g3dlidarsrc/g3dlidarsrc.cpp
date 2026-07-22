/*******************************************************************************
 * Copyright (C) 2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#include "g3dlidarsrc.h"
#include <dlstreamer/gst/metadata/g3d_lidar_meta.h>
#include <dlstreamer/lidar/g3d_lidar_backend_api.h>
#include <gst/gstinfo.h>
#include <string.h>

#include <dlfcn.h>

#include "lidar_config.hpp"

#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <queue>
#include <stdexcept>
#include <string>
#include <vector>

/* The vendor backend library is loaded by bare name at runtime:
 *   - Windows: g3dlidar_<vendor>.dll
 *   - Linux:   libg3dlidar_<vendor>.so
 * The <vendor> comes from the config, so the element is vendor-agnostic. */
#ifdef _WIN32
#define G3D_BACKEND_LIB_PREFIX "g3dlidar_"
#define G3D_BACKEND_LIB_SUFFIX ".dll"
#else
#define G3D_BACKEND_LIB_PREFIX "libg3dlidar_"
#define G3D_BACKEND_LIB_SUFFIX ".so"
#endif

GST_DEBUG_CATEGORY_STATIC(gst_g3d_lidar_src_debug);
#define GST_CAT_DEFAULT gst_g3d_lidar_src_debug

/* Property IDs */
enum { PROP_0, PROP_CONFIG, PROP_NTP_SYNC, PROP_TIMEOUT, PROP_STREAM_ID };

/* Default values */
#define DEFAULT_CONFIG NULL     /* no config file by default (required at start) */
#define DEFAULT_NTP_SYNC FALSE  /* FALSE = use pipeline clock (same as rtspsrc default) */
#define DEFAULT_TIMEOUT 5000000 /* 5 seconds in microseconds (same as rtspsrc default) */

#define FRAME_QUEUE_TIMEOUT_MS 500 /* Wait timeout in create() */
#define MAX_FRAME_QUEUE_SIZE 10
#define USEC_TO_SEC(us) ((us) / 1000000.0) /* Convert microseconds to seconds */

/* Src pad template */
static GstStaticPadTemplate src_template =
    GST_STATIC_PAD_TEMPLATE("src", GST_PAD_SRC, GST_PAD_ALWAYS, GST_STATIC_CAPS("application/x-lidar"));

/* One completed frame, copied out of the backend's borrowed buffer (which is
 * only valid during the cloud callback) so it can be queued for create(). */
struct LidarFrame {
    std::vector<float> xyzi; /* point_count * 4 floats: x, y, z, intensity */
    guint point_count;
    double timestamp;
};

/* Private data structure (C++ objects hidden from GObject C header) */
struct _GstG3DLidarSrcPrivate {
    /* Vendor backend (g3dlidar_<vendor>.dll or libg3dlidar_<vendor>.so), loaded at runtime via dlopen(). */
    void *sdk_handle;             /* dlopen() handle for the backend .so */
    g3d_lidar_backend_handle *rs; /* backend instance */
    g3d_lidar_backend_handle *(*create_fn)(void);
    g3d_lidar_error_code (*set_callbacks_fn)(g3d_lidar_backend_handle *, g3d_lidar_cloud_cb, g3d_lidar_error_cb,
                                             void *);
    g3d_lidar_error_code (*init_fn)(g3d_lidar_backend_handle *, const g3d_lidar_params *, char *, int);
    g3d_lidar_error_code (*start_fn)(g3d_lidar_backend_handle *);
    void (*stop_fn)(g3d_lidar_backend_handle *);
    void (*destroy_fn)(g3d_lidar_backend_handle *);

    std::mutex queue_mutex;
    std::condition_variable queue_cond;
    std::queue<std::shared_ptr<LidarFrame>> frame_queue;

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
        g_param_spec_uint64("timeout", "Timeout", "Timeout in microseconds for receiving data (0 = no timeout)", 0,
                            G_MAXUINT64, DEFAULT_TIMEOUT, (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(
        gobject_class, PROP_STREAM_ID,
        g_param_spec_uint("stream-id", "Stream ID", "Stream identifier for this LiDAR source (used in metadata)", 0,
                          G_MAXUINT, 0, (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

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
    static const char *deprecated_inherited[] = {"blocksize", "do-timestamp", "typefind", NULL};
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
    new (&self->priv->frame_queue) std::queue<std::shared_ptr<LidarFrame>>();
    self->priv->sdk_handle = nullptr;
    self->priv->rs = nullptr;
    self->priv->create_fn = nullptr;
    self->priv->set_callbacks_fn = nullptr;
    self->priv->init_fn = nullptr;
    self->priv->start_fn = nullptr;
    self->priv->stop_fn = nullptr;
    self->priv->destroy_fn = nullptr;
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

    /* Fallback cleanup in case stop() did not run. */
    if (self->priv->rs && self->priv->destroy_fn) {
        self->priv->destroy_fn(self->priv->rs);
        self->priv->rs = nullptr;
    }
    if (self->priv->sdk_handle) {
        dlclose(self->priv->sdk_handle);
        self->priv->sdk_handle = nullptr;
    }

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

/* Shim cloud callback (called from rs_driver's receive thread via the shim):
 * copy the borrowed frame and push it to our queue. The xyzi pointer is only
 * valid for the duration of this call, so we copy it into a LidarFrame. */
static void on_cloud_cb(void *user, const g3d_lidar_frame *frame) {
    GstG3DLidarSrc *self = GST_G3D_LIDAR_SRC(user);

    auto lf = std::make_shared<LidarFrame>();
    lf->point_count = frame->point_count;
    lf->timestamp = frame->timestamp;
    lf->xyzi.assign(frame->xyzi, frame->xyzi + (size_t)frame->point_count * 4);

    std::lock_guard<std::mutex> lock(self->priv->queue_mutex);

    /* Record frame reception time */
    self->priv->last_frame_time = std::chrono::steady_clock::now();

    /* Drop oldest frame if queue is full to avoid unbounded growth */
    if (self->priv->frame_queue.size() >= MAX_FRAME_QUEUE_SIZE) {
        self->priv->frame_queue.pop();
        GST_WARNING_OBJECT(self, "Frame queue full, dropping oldest frame");
    }

    self->priv->frame_queue.push(lf);
    self->priv->queue_cond.notify_one();
}

/* Shim error callback: surface rs_driver exceptions as element warnings. */
static void on_error_cb(void *user, const char *msg) {
    GstG3DLidarSrc *self = GST_G3D_LIDAR_SRC(user);
    GST_WARNING_OBJECT(self, "rs_driver exception: %s", msg ? msg : "(null)");
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

    /* Transport is vendor-neutral; only UDP is implemented today. Vendor support
     * itself is decided by whether the matching backend library exists (below),
     * so the element does not hardcode a vendor allowlist. */
    if (cfg.transport.type != g3dlidar::TransportType::UDP) {
        GST_ELEMENT_ERROR(self, RESOURCE, NOT_FOUND, (NULL),
                          ("Unsupported transport '%s' (only 'udp' is implemented)", cfg.transport.type_str.c_str()));
        return FALSE;
    }

    /* Build the vendor-neutral params. Vendor-specific options (ports, range
     * clipping, SDK toggles) are passed through verbatim as the raw `params`
     * JSON string and parsed by the backend; the element does not interpret
     * them. cfg (and params_json) outlive this call, backing the char pointers,
     * which the backend only reads during init below. */
    std::string params_json = cfg.params.dump();
    g3d_lidar_params params;
    params.model = cfg.model.c_str();
    /* "bind_address" is the local NIC to bind the UDP socket to. Multicast
     * (group_address) is not exposed: unicast covers virtually all deployments. */
    params.bind_address = cfg.transport.bind_address.c_str();
    /* Pipeline clock vs. LiDAR clock is exposed as the GObject `ntp-sync`
     * property, not via params, so it overrides any use_lidar_clock in params. */
    params.use_lidar_clock = self->ntp_sync ? 1 : 0;
    params.params_json = params_json.c_str();

    GST_INFO_OBJECT(self, "Starting g3dlidarsrc: vendor=%s model=%s transport=udp bind=%s ntp-sync=%s",
                    cfg.vendor.c_str(), cfg.model.c_str(), cfg.transport.bind_address.c_str(),
                    self->ntp_sync ? "true" : "false");

    /* The vendor string is interpolated into a library name and handed to
     * dlopen(), so it must be strictly validated first. Without this, a config
     * could smuggle path separators or "." components (e.g. vendor="../../evil")
     * to load an arbitrary library off disk. Restrict to a conservative
     * allowlist: non-empty, only [A-Za-z0-9_], no path separators or dots. */
    if (cfg.vendor.empty() ||
        cfg.vendor.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_") !=
            std::string::npos) {
        GST_ELEMENT_ERROR(self, RESOURCE, SETTINGS, (NULL),
                          ("Invalid vendor '%s' in config: only ASCII letters, digits and underscore "
                           "are allowed (no path separators or dots).",
                           cfg.vendor.c_str()));
        return FALSE;
    }

    /* Derive the backend library name from the vendor and load it. Each vendor
     * backend (g3dlidar_<vendor>.dll on Windows, libg3dlidar_<vendor>.so on Linux) implements the C ABI in
     * <dlstreamer/lidar/g3d_lidar_backend_api.h>. Loaded by bare name so the
     * platform dynamic loader searches its standard library paths. */
    std::string backend_lib = G3D_BACKEND_LIB_PREFIX + cfg.vendor + G3D_BACKEND_LIB_SUFFIX;
    self->priv->sdk_handle = dlopen(backend_lib.c_str(), RTLD_LAZY);
    if (!self->priv->sdk_handle) {
        GST_ELEMENT_ERROR(self, LIBRARY, INIT, (NULL),
                          ("Failed to load backend '%s' for vendor '%s': %s. Ensure the vendor "
                           "backend is built (its ENABLE_LIDAR_<VENDOR> CMake option is ON) and on "
                           "the library path (LD_LIBRARY_PATH or rpath).",
                           backend_lib.c_str(), cfg.vendor.c_str(), dlerror()));
        return FALSE;
    }

#define G3D_LOAD_SYM(field, type, name)                                                                                \
    do {                                                                                                               \
        self->priv->field = (type)dlsym(self->priv->sdk_handle, name);                                                 \
        if (!self->priv->field) {                                                                                      \
            GST_ELEMENT_ERROR(self, LIBRARY, INIT, (NULL),                                                             \
                              ("Failed to resolve symbol '%s' in %s: %s", name, backend_lib.c_str(), dlerror()));      \
            dlclose(self->priv->sdk_handle);                                                                           \
            self->priv->sdk_handle = nullptr;                                                                          \
            return FALSE;                                                                                              \
        }                                                                                                              \
    } while (0)

    G3D_LOAD_SYM(create_fn, g3d_lidar_backend_handle * (*)(void), "g3d_lidar_backend_create");
    G3D_LOAD_SYM(set_callbacks_fn,
                 g3d_lidar_error_code(*)(g3d_lidar_backend_handle *, g3d_lidar_cloud_cb, g3d_lidar_error_cb, void *),
                 "g3d_lidar_backend_set_callbacks");
    G3D_LOAD_SYM(init_fn, g3d_lidar_error_code(*)(g3d_lidar_backend_handle *, const g3d_lidar_params *, char *, int),
                 "g3d_lidar_backend_init");
    G3D_LOAD_SYM(start_fn, g3d_lidar_error_code(*)(g3d_lidar_backend_handle *), "g3d_lidar_backend_start");
    G3D_LOAD_SYM(stop_fn, void (*)(g3d_lidar_backend_handle *), "g3d_lidar_backend_stop");
    G3D_LOAD_SYM(destroy_fn, void (*)(g3d_lidar_backend_handle *), "g3d_lidar_backend_destroy");
#undef G3D_LOAD_SYM

    /* Create backend instance and register callbacks. */
    self->priv->rs = self->priv->create_fn();
    if (!self->priv->rs) {
        GST_ELEMENT_ERROR(self, RESOURCE, OPEN_READ, (NULL),
                          ("Failed to create '%s' backend instance", cfg.vendor.c_str()));
        dlclose(self->priv->sdk_handle);
        self->priv->sdk_handle = nullptr;
        return FALSE;
    }
    self->priv->set_callbacks_fn(self->priv->rs, on_cloud_cb, on_error_cb, self);

    /* Init and start. The backend writes a human-readable reason into err_buf on
     * failure (bad params, unknown model, SDK init failure). */
    char err_buf[256] = {0};
    if (self->priv->init_fn(self->priv->rs, &params, err_buf, (int)sizeof(err_buf)) != G3D_LIDAR_OK) {
        GST_ELEMENT_ERROR(
            self, RESOURCE, OPEN_READ, (NULL),
            ("Failed to initialize '%s' backend: %s", cfg.vendor.c_str(), err_buf[0] ? err_buf : "(no detail)"));
        self->priv->destroy_fn(self->priv->rs);
        self->priv->rs = nullptr;
        dlclose(self->priv->sdk_handle);
        self->priv->sdk_handle = nullptr;
        return FALSE;
    }

    if (self->priv->start_fn(self->priv->rs) != G3D_LIDAR_OK) {
        GST_ELEMENT_ERROR(self, RESOURCE, OPEN_READ, (NULL), ("Failed to start '%s' backend", cfg.vendor.c_str()));
        self->priv->destroy_fn(self->priv->rs);
        self->priv->rs = nullptr;
        dlclose(self->priv->sdk_handle);
        self->priv->sdk_handle = nullptr;
        return FALSE;
    }

    /* Initialize timeout tracking */
    self->priv->flushing = FALSE;
    self->priv->last_frame_time = std::chrono::steady_clock::now();
    self->frame_seq = 0;

    GST_INFO_OBJECT(self,
                    "g3dlidarsrc started successfully, waiting for data from device... "
                    "(timeout=%.1fs)",
                    USEC_TO_SEC(self->timeout));
    return TRUE;
}

static gboolean gst_g3d_lidar_src_stop(GstBaseSrc *src) {
    GstG3DLidarSrc *self = GST_G3D_LIDAR_SRC(src);

    GST_INFO_OBJECT(self, "Stopping g3dlidarsrc");

    if (self->priv->rs) {
        self->priv->stop_fn(self->priv->rs);
        self->priv->destroy_fn(self->priv->rs);
        self->priv->rs = nullptr;
    }
    if (self->priv->sdk_handle) {
        dlclose(self->priv->sdk_handle);
        self->priv->sdk_handle = nullptr;
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
    std::shared_ptr<LidarFrame> cloud;

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
                auto elapsed_us =
                    std::chrono::duration_cast<std::chrono::microseconds>(now - self->priv->last_frame_time).count();

                /* Cast to gint64 to match elapsed_us type from chrono::count() */
                if (elapsed_us >= (gint64)self->timeout) {
                    GST_ELEMENT_ERROR(self, RESOURCE, READ, (NULL),
                                      ("Timeout receiving data from LiDAR device. "
                                       "No frames received for %.4f seconds. "
                                       "Device may not be connected or network may be misconfigured. "
                                       "Check: 1) Device is powered on and connected, "
                                       "2) Transport settings in config '%s', "
                                       "3) Firewall settings.",
                                       USEC_TO_SEC(self->timeout), self->config));
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

    if (!cloud || cloud->point_count == 0) {
        GST_DEBUG_OBJECT(self, "Received empty point cloud, skipping");
        *buf = gst_buffer_new();
        return GST_FLOW_OK;
    }

    /* The shim already delivered the frame as a flat [x, y, z, intensity] block,
     * so we just copy it into the output buffer. */
    size_t point_count = cloud->point_count;
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

    memcpy(map.data, cloud->xyzi.data(), payload_size);

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

    GST_LOG_OBJECT(self, "Frame %zu: %zu points, payload %zu bytes, ts=%" GST_TIME_FORMAT, self->frame_seq, point_count,
                   payload_size, GST_TIME_ARGS(frame_ts));

    self->frame_seq++;

    return GST_FLOW_OK;
}
