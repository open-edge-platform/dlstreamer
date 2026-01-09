/*******************************************************************************
 * Copyright (C) 2025-2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#include "gvamotiondetect.h"
#include <glib.h>
#include <gst/base/gstbasetransform.h>
#include <gst/gst.h>
#include <gst/video/video.h>

// VAAPI + OpenCV includes (Windows implementation resides in separate file)
#include <dlfcn.h>
#include <gst/va/gstvadisplay.h>
#include <gst/va/gstvautils.h>
#include <opencv2/core/va_intel.hpp>
#include <va/va.h>
#include <va/va_vpp.h>

#include <gst/video/video.h>
#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>
// Explicit OpenCL headers removed: element now uses only generic OpenCV UMat (which may internally use OpenCL).
#include <algorithm>
#include <cmath> // for std::lround
#include <vector>

#include <dlstreamer/gst/videoanalytics/video_frame.h> // analytics meta types (GstAnalyticsRelationMeta, GstAnalyticsODMtd)
#include <string>

// Removed G_BEGIN_DECLS / G_END_DECLS to avoid forcing C linkage on C++ helper functions

GST_DEBUG_CATEGORY_STATIC(gst_gva_motion_detect_debug);
#define GST_CAT_DEFAULT gst_gva_motion_detect_debug

struct MotionRect {
    gint x;
    gint y;
    gint w;
    gint h;
};

// Compact coordinate rounding helper: limit normalized values to 3 decimal places
// to reduce JSON payload size without materially impacting downstream logic.
static inline double md_round_coord(double v) {
    return std::floor(v * 1000.0 + 0.5) / 1000.0; // round half up to 0.001 precision
}

/* Property identifiers */
enum {
    PROP_0,
    PROP_BLOCK_SIZE,
    PROP_MOTION_THRESHOLD,
    PROP_MIN_PERSISTENCE,
    PROP_MAX_MISS,
    PROP_IOU_THRESHOLD,
    PROP_SMOOTH_ALPHA,
    PROP_CONFIRM_FRAMES,
    PROP_PIXEL_DIFF_THRESHOLD,
    PROP_MIN_REL_AREA
};

struct _GstGvaMotionDetect {
    GstBaseTransform parent;
    GstVideoInfo vinfo;
    gboolean caps_is_va; // retained for compatibility

    // Common (both platforms)
    int blur_kernel;      // odd size (used for potential future smoothing)
    double blur_sigma;    // gaussian sigma
    uint64_t frame_index; // running frame counter

    VADisplay va_dpy;
    GstVaDisplay *va_display;
    cv::UMat scratch;
    cv::Mat overlay_cpu;  // host-side drawing buffer (BGRA)
    cv::UMat overlay_gpu; // device-side buffer used for blending
    bool overlay_ready;
    std::string last_text;
    VASurfaceID prev_sid; // simple 1‑frame history
    // Scaled working surface (hardware downscale target). Created on-demand.
    VASurfaceID scaled_sid;
    int scaled_w;
    int scaled_h;

    /* Motion detection previous frame state */
    cv::UMat prev_small_gray;
    cv::UMat prev_luma;

    /* Grid detection parameters (properties) */
    int block_size;
    double motion_threshold;

    // Stability (temporal smoothing) configuration
    int min_persistence;      // frames required before ROI published
    int max_miss;             // grace frames allowed after disappearance
    double iou_threshold;     // ROI tracking match threshold
    double smooth_alpha;      // EMA smoothing factor for coordinates
    int confirm_frames;       // consecutive frames required to confirm motion (1=single-frame)
    int pixel_diff_threshold; // per-pixel absolute luma difference threshold (1..255)
    double min_rel_area;      // minimum relative area (0..1) for a motion rectangle to be considered

    // Block agreement state (grid of counters 0..2 for consecutive active frames)
    cv::Mat block_state; // CV_8U
    int block_state_w;   // columns (blocks)
    int block_state_h;   // rows (blocks)

    // Tracked ROI list for temporal persistence and smoothing
    struct TrackedROI {
        int x, y, w, h;        // last raw box
        double sx, sy, sw, sh; // smoothed coords
        int age;               // consecutive frames seen
        int misses;            // consecutive frames not matched
    };
    std::vector<TrackedROI> tracked_rois;

    /* Debug controls */
    gboolean debug_enabled;
    guint debug_interval;
    guint64 last_debug_frame;
    gboolean tried_va_query;

    /* Concurrency guard for metadata operations */
    GMutex meta_mutex;
};

/* Forward declarations of property handlers */
static void gst_gva_motion_detect_set_property(GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec);
static void gst_gva_motion_detect_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec);
// Forward declaration for tracking attach helper used in transform_ip
static void gst_gva_motion_detect_process_and_attach(GstGvaMotionDetect *self, GstBuffer *buf,
                                                     const std::vector<MotionRect> &raw_rois, int width, int height);

static void gst_gva_motion_detect_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec) {
    GstGvaMotionDetect *self = GST_GVA_MOTION_DETECT(object);
    switch (prop_id) {
    case PROP_BLOCK_SIZE:
        g_value_set_int(value, self->block_size);
        break;
    case PROP_MOTION_THRESHOLD:
        g_value_set_double(value, self->motion_threshold);
        break;
    case PROP_MIN_PERSISTENCE:
        g_value_set_int(value, self->min_persistence);
        break;
    case PROP_MAX_MISS:
        g_value_set_int(value, self->max_miss);
        break;
    case PROP_IOU_THRESHOLD:
        g_value_set_double(value, self->iou_threshold);
        break;
    case PROP_SMOOTH_ALPHA:
        g_value_set_double(value, self->smooth_alpha);
        break;
    case PROP_CONFIRM_FRAMES:
        g_value_set_int(value, self->confirm_frames);
        break;
    case PROP_PIXEL_DIFF_THRESHOLD:
        g_value_set_int(value, self->pixel_diff_threshold);
        break;
    case PROP_MIN_REL_AREA:
        g_value_set_double(value, self->min_rel_area);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
    }
}

static void gst_gva_motion_detect_set_property(GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec) {
    GstGvaMotionDetect *self = GST_GVA_MOTION_DETECT(object);
    switch (prop_id) {
    case PROP_BLOCK_SIZE:
        self->block_size = g_value_get_int(value);
        break;
    case PROP_MOTION_THRESHOLD:
        self->motion_threshold = g_value_get_double(value);
        break;
    case PROP_MIN_PERSISTENCE:
        self->min_persistence = std::max(1, g_value_get_int(value));
        break;
    case PROP_MAX_MISS:
        self->max_miss = std::max(0, g_value_get_int(value));
        break;
    case PROP_IOU_THRESHOLD:
        self->iou_threshold = std::clamp(g_value_get_double(value), 0.0, 1.0);
        break;
    case PROP_SMOOTH_ALPHA: {
        double a = g_value_get_double(value);
        self->smooth_alpha = (a < 0.0) ? 0.0 : (a > 1.0 ? 1.0 : a);
        break;
    }
    case PROP_CONFIRM_FRAMES: {
        int cf = g_value_get_int(value);
        self->confirm_frames = std::max(1, std::min(cf, 10));
        // Optionally shrink existing block_state values if they exceed new threshold
        if (!self->block_state.empty() && self->confirm_frames < 2) {
            // No temporal confirmation needed; counters can be cleared
            self->block_state.setTo(cv::Scalar(0));
        }
        break;
    }
    case PROP_PIXEL_DIFF_THRESHOLD: {
        int thr = g_value_get_int(value);
        self->pixel_diff_threshold = std::max(1, std::min(thr, 255));
        break;
    }
    case PROP_MIN_REL_AREA: {
        double mra = g_value_get_double(value);
        if (mra < 0.0)
            mra = 0.0;
        if (mra > 0.25)
            mra = 0.25; // cap to 25% of frame
        self->min_rel_area = mra;
        break;
    }
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
    }
}

/* Define class struct prior to G_DEFINE_TYPE so sizeof works */
struct _GstGvaMotionDetectClass {
    GstBaseTransformClass parent_class;
};

G_DEFINE_TYPE(GstGvaMotionDetect, gst_gva_motion_detect, GST_TYPE_BASE_TRANSFORM)

// Support both VA GPU memory and system memory; runtime property enforces restriction.
static GstStaticPadTemplate sink_templ =
    GST_STATIC_PAD_TEMPLATE("sink", GST_PAD_SINK, GST_PAD_ALWAYS,
                            GST_STATIC_CAPS("video/x-raw(memory:VAMemory), format=NV12; "
                                            "video/x-raw, format=NV12"));
static GstStaticPadTemplate src_templ =
    GST_STATIC_PAD_TEMPLATE("src", GST_PAD_SRC, GST_PAD_ALWAYS,
                            GST_STATIC_CAPS("video/x-raw(memory:VAMemory), format=NV12; "
                                            "video/x-raw, format=NV12"));

static void gst_gva_motion_detect_set_context(GstElement *elem, GstContext *context) {
    GstGvaMotionDetect *self = GST_GVA_MOTION_DETECT(elem);
    const gchar *ctype = gst_context_get_context_type(context);
    const GstStructure *st = gst_context_get_structure(context);
    if (g_strcmp0(ctype, "gst.va.display.handle") == 0 && !self->va_dpy) {
        if (gst_structure_has_field(st, "va-display")) {
            self->va_dpy = (VADisplay)g_value_get_pointer(gst_structure_get_value(st, "va-display"));
        } else if (gst_structure_has_field(st, "gst-display")) {
            GstObject *obj = nullptr;
            if (gst_structure_get(st, "gst-display", GST_TYPE_OBJECT, &obj, nullptr) && obj) {
                self->va_dpy = (VADisplay)gst_va_display_get_va_dpy(GST_VA_DISPLAY(obj));
                gst_object_unref(obj);
            }
        }
    }
    GST_ELEMENT_CLASS(gst_gva_motion_detect_parent_class)->set_context(elem, context);
}

static gboolean gst_gva_motion_detect_query(GstElement *elem, GstQuery *query) {
    GstGvaMotionDetect *self = GST_GVA_MOTION_DETECT(elem);
    if (gst_va_handle_context_query(elem, query, self->va_display))
        return TRUE;
    return GST_ELEMENT_CLASS(gst_gva_motion_detect_parent_class)->query(elem, query);
}

static GstCaps *gst_gva_motion_detect_transform_caps(GstBaseTransform *, GstPadDirection /*direction*/, GstCaps *caps,
                                                     GstCaps *filter) {
    GstCaps *ret = gst_caps_ref(caps);
    if (filter) {
        GstCaps *intersect = gst_caps_intersect_full(filter, ret, GST_CAPS_INTERSECT_FIRST);
        gst_caps_unref(ret);
        ret = intersect;
    }
    return ret;
}

// -----------------------------------------------------------------------------
// VA API helper functions
// Map GST buffer to VA surface (using mapper + fallback) and return VASurfaceID
static VASurfaceID gva_motion_detect_get_surface([[maybe_unused]] GstGvaMotionDetect *self, GstBuffer *buf) {
    if (!buf)
        return VA_INVALID_SURFACE;
    VASurfaceID sid = gst_va_buffer_get_surface(buf);
    if (sid != VA_INVALID_SURFACE)
        return sid;
    guint n = gst_buffer_n_memory(buf);
    for (guint i = 0; i < n; ++i) {
        GstMemory *m = gst_buffer_peek_memory(buf, i);
        if (m && gst_memory_is_type(m, "VAMemory")) {
            sid = gst_va_memory_get_surface(m);
            if (sid != VA_INVALID_SURFACE)
                return sid;
        }
    }
    return VA_INVALID_SURFACE;
}

// Convert VA surface to cv::UMat (GPU backed); returns false on failure
static bool gva_motion_detect_convert_from_surface(GstGvaMotionDetect *self, VASurfaceID sid, int width, int height,
                                                   cv::UMat &out) {
    if (sid == VA_INVALID_SURFACE || !self->va_dpy)
        return false;
    try {
        cv::va_intel::convertFromVASurface(self->va_dpy, sid, cv::Size(width, height), out);
        return true;
    } catch (const cv::Exception &e) {
        GST_WARNING_OBJECT(self, "convertFromVASurface failed: %s", e.what());
        return false;
    }
}

// Write cv::UMat back into the VA surface; returns false on failure
[[maybe_unused]] static bool gva_motion_detect_write_to_surface(GstGvaMotionDetect *self, const cv::UMat &src,
                                                                VASurfaceID sid, int width, int height) {
    if (sid == VA_INVALID_SURFACE || !self->va_dpy)
        return false;
    try {
        cv::va_intel::convertToVASurface(self->va_dpy, src, sid, cv::Size(width, height));
        return true;
    } catch (const cv::Exception &e) {
        GST_WARNING_OBJECT(self, "convertToVASurface failed: %s", e.what());
        return false;
    }
}

// Map only the luma (Y) plane of an NV12/YUV420 VA surface into a cv::UMat (single-channel) to avoid full color
// conversion. Returns true on success.
static bool gva_motion_detect_map_luma(GstGvaMotionDetect *self, VASurfaceID sid, int width, int height,
                                       cv::UMat &out_luma) {
    if (sid == VA_INVALID_SURFACE || !self->va_dpy || width <= 0 || height <= 0)
        return false;
    VAImage image;
    memset(&image, 0, sizeof(image));
    VAStatus st = vaDeriveImage(self->va_dpy, sid, &image);
    if (st != VA_STATUS_SUCCESS) {
        GST_DEBUG_OBJECT(self, "vaDeriveImage failed status=%d (%s)", (int)st, vaErrorStr(st));
        return false; // keep it simple; fallback path will handle full conversion
    }
    void *data = nullptr;
    if (vaMapBuffer(self->va_dpy, image.buf, &data) != VA_STATUS_SUCCESS) {
        vaDestroyImage(self->va_dpy, image.image_id);
        return false;
    }
    // Supported formats where plane 0 is luma
    if (image.format.fourcc != VA_FOURCC_NV12 && image.format.fourcc != VA_FOURCC_I420 &&
        image.format.fourcc != VA_FOURCC_YV12) {
        vaUnmapBuffer(self->va_dpy, image.buf);
        vaDestroyImage(self->va_dpy, image.image_id);
        GST_DEBUG_OBJECT(self, "Unsupported fourcc %4.4s for luma mapping", (char *)&image.format.fourcc);
        return false;
    }
    uint8_t *base = static_cast<uint8_t *>(data);
    uint8_t *y_ptr = base + image.offsets[0];
    int y_pitch = image.pitches[0];
    cv::Mat y_mat(height, width, CV_8UC1, y_ptr, y_pitch);
    try {
        y_mat.copyTo(out_luma); // upload to UMat
    } catch (const cv::Exception &e) {
        vaUnmapBuffer(self->va_dpy, image.buf);
        vaDestroyImage(self->va_dpy, image.image_id);
        GST_WARNING_OBJECT(self, "Luma copyTo(UMat) failed: %s", e.what());
        return false;
    }
    vaUnmapBuffer(self->va_dpy, image.buf);
    vaDestroyImage(self->va_dpy, image.image_id);
    return true;
}

// Ensure (create or reuse) a scaled VA surface of requested size; returns valid sid or VA_INVALID_SURFACE.
static VASurfaceID gva_motion_detect_ensure_scaled_surface(GstGvaMotionDetect *self, int w, int h) {
    if (!self->va_dpy || w <= 0 || h <= 0)
        return VA_INVALID_SURFACE;
    if (self->scaled_sid != VA_INVALID_SURFACE && self->scaled_w == w && self->scaled_h == h)
        return self->scaled_sid;
    // Recreate if size changed
    if (self->scaled_sid != VA_INVALID_SURFACE) {
        vaDestroySurfaces(self->va_dpy, &self->scaled_sid, 1);
        self->scaled_sid = VA_INVALID_SURFACE;
    }
    // Assumption: source is NV12/YUV420; create a generic YUV420 surface.
    VASurfaceID newsid = VA_INVALID_SURFACE;
    VAStatus st = vaCreateSurfaces(self->va_dpy, VA_RT_FORMAT_YUV420, w, h, &newsid, 1, nullptr, 0);
    if (st != VA_STATUS_SUCCESS) {
        GST_WARNING_OBJECT(self, "vaCreateSurfaces (scaled) failed %d (%s)", (int)st, vaErrorStr(st));
        return VA_INVALID_SURFACE;
    }
    self->scaled_sid = newsid;
    self->scaled_w = w;
    self->scaled_h = h;
    GST_LOG_OBJECT(self, "Allocated scaled VA surface sid=%u size=%dx%d", newsid, w, h);
    return newsid;
}

// Hardware downscale via vaBlitSurface into cached scaled surface. Returns true on success and outputs sid.
static bool gst_gva_motion_detect_va_downscale(GstGvaMotionDetect *self, VASurfaceID src_sid, int src_w, int src_h,
                                               int dst_w, int dst_h, VASurfaceID &out_sid) {
    out_sid = VA_INVALID_SURFACE;
    if (!self->va_dpy || src_sid == VA_INVALID_SURFACE)
        return false;
    if (dst_w <= 0 || dst_h <= 0 || src_w <= 0 || src_h <= 0)
        return false;
    VASurfaceID dst_sid = gva_motion_detect_ensure_scaled_surface(self, dst_w, dst_h);
    if (dst_sid == VA_INVALID_SURFACE)
        return false;

    // Try to locate vaBlitSurface dynamically (may be absent in older libva versions).
    typedef VAStatus (*PFN_vaBlitSurface)(VADisplay, VASurfaceID, VASurfaceID, const VARectangle *, const VARectangle *,
                                          const VARectangle *, uint32_t);
    static PFN_vaBlitSurface p_vaBlitSurface = nullptr;
    if (!p_vaBlitSurface) {
        void *sym = dlsym(RTLD_DEFAULT, "vaBlitSurface");
        p_vaBlitSurface = reinterpret_cast<PFN_vaBlitSurface>(sym);
        if (!p_vaBlitSurface) {
            GST_LOG_OBJECT(self, "vaBlitSurface symbol not found; falling back to software resize");
            return false;
        }
    }

    // Prepare rectangles safely (avoid narrowing warnings by explicit assignment & clamping)
    VARectangle src_rect;
    src_rect.x = 0;
    src_rect.y = 0;
    src_rect.width = (uint16_t)std::min(src_w, 0xFFFF);
    src_rect.height = (uint16_t)std::min(src_h, 0xFFFF);
    VARectangle dst_rect;
    dst_rect.x = 0;
    dst_rect.y = 0;
    dst_rect.width = (uint16_t)std::min(dst_w, 0xFFFF);
    dst_rect.height = (uint16_t)std::min(dst_h, 0xFFFF);
    VAStatus st = p_vaBlitSurface(self->va_dpy, dst_sid, src_sid, &src_rect, &dst_rect, nullptr, 0);
    if (st != VA_STATUS_SUCCESS) {
        GST_DEBUG_OBJECT(self, "vaBlitSurface unavailable/failed -> software resize path (status=%d %s)", (int)st,
                         vaErrorStr(st));
        return false;
    }
    st = vaSyncSurface(self->va_dpy, dst_sid);
    if (st != VA_STATUS_SUCCESS) {
        GST_WARNING_OBJECT(self, "vaSyncSurface (scaled) failed %d (%s)", (int)st, vaErrorStr(st));
        return false;
    }
    out_sid = dst_sid;
    return true;
}

static gboolean gst_gva_motion_detect_start(GstBaseTransform *trans) {
    GstGvaMotionDetect *self = GST_GVA_MOTION_DETECT(trans);
    gst_video_info_init(&self->vinfo);
    self->caps_is_va = FALSE;
    self->frame_index = 0;
    self->tried_va_query = FALSE;
    self->va_dpy = nullptr;
    self->va_display = nullptr;
    gst_element_post_message(GST_ELEMENT(self),
                             gst_message_new_need_context(GST_OBJECT(self), "gst.va.display.handle"));
    return TRUE;
}

static gboolean gst_gva_motion_detect_set_caps(GstBaseTransform *trans, GstCaps *incaps,
                                               [[maybe_unused]] GstCaps *outcaps) {
    GstGvaMotionDetect *self = GST_GVA_MOTION_DETECT(trans);
    if (!gst_video_info_from_caps(&self->vinfo, incaps))
        return FALSE;
    gchar *caps_str = gst_caps_to_string(incaps);
    gboolean is_va = (caps_str && strstr(caps_str, "memory:VAMemory"));
    g_free(caps_str);
    self->caps_is_va = is_va ? TRUE : FALSE; // record negotiated memory type
    // Reset tracking state on caps change (resolution may differ)
    self->tracked_rois.clear();
    self->block_state.release();
    return TRUE;
}
// Helper to attach motion ROIs and associated analytics metadata (aggregated in a single relation meta)
// Attach motion results to the buffer using TWO complementary metadata layers:
// 1. GstAnalyticsRelationMeta ("relation" meta): an aggregate container that holds one object
//    detection metadata (ODMtd) entry per motion ROI. This is a compact, machine-readable
//    summary used by higher-level analytics components. Each ODMtd stores integer pixel
//    coordinates and a confidence (always 1.0 for binary motion presence here).
// 2. GstVideoRegionOfInterestMeta (ROI meta): a traditional per-region video meta providing
//    label + rectangle + arbitrary parameters. We add a "detection" GstStructure with
//    normalized coordinates (x_min/x_max/y_min/y_max) rounded to three decimals to reduce
//    payload noise. The ROI meta 'id' is set to the corresponding ODMtd id to allow consumers
//    to cross-reference both representations if they prefer one format over the other.
//
// Rationale:
// - Relation meta enables unified iteration of all analytic objects in a frame (motion, detections, etc.).
// - ROI meta preserves compatibility with existing GStreamer video analytics / downstream plugins expecting ROIs.
// - Normalized, rounded coordinates facilitate lightweight serialization while integer pixel coordinates preserve
// fidelity. If relation meta cannot be obtained/created, we skip attaching any motion ROIs to avoid partially
// inconsistent state.
static void gst_gva_motion_detect_attach_rois(GstGvaMotionDetect *self, GstBuffer *buf,
                                              const std::vector<MotionRect> &rois, int width, int height) {
    if (rois.empty())
        return;
    if (!gst_buffer_is_writable(buf)) {
        GstBuffer *writable = gst_buffer_make_writable(buf);
        if (writable != buf)
            buf = writable;
    }
    if (!gst_buffer_is_writable(buf)) {
        GST_WARNING_OBJECT(self, "Buffer not writable; skipping motion ROI attachment");
        return;
    }
    // Obtain (or create) the aggregate relation meta that will hold all motion ODMtd entries.
    GstAnalyticsRelationMeta *relation_meta = gst_buffer_get_analytics_relation_meta(buf);
    if (!relation_meta) {
        relation_meta = gst_buffer_add_analytics_relation_meta(buf);
        GST_LOG_OBJECT(self, "Added new GstAnalyticsRelationMeta %p", relation_meta);
    } else {
        GST_LOG_OBJECT(self, "Reusing existing GstAnalyticsRelationMeta %p", relation_meta);
    }
    if (!relation_meta) {
        GST_WARNING_OBJECT(self, "Failed to add/get GstAnalyticsRelationMeta; skipping ROIs");
        return;
    }
    size_t attached = 0;
    for (const auto &r : rois) {
        double x = (double)r.x / (double)width;
        double y = (double)r.y / (double)height;
        double w = (double)r.w / (double)width;
        double h = (double)r.h / (double)height;
        if (!(x >= 0 && y >= 0 && w >= 0 && h >= 0 && x + w <= 1 && y + h <= 1)) {
            x = (x < 0) ? 0 : (x > 1 ? 1 : x);
            y = (y < 0) ? 0 : (y > 1 ? 1 : y);
            w = (w < 0) ? 0 : (w > 1 - x ? 1 - x : w);
            h = (h < 0) ? 0 : (h > 1 - y ? 1 - y : h);
        }
        double _x = x * width + 0.5;
        double _y = y * height + 0.5;
        double _w = w * width + 0.5;
        double _h = h * height + 0.5;
        // Apply precision reduction to normalized coordinates (rounded to 0.001) for compactness.
        double x_min_r = md_round_coord(x);
        double x_max_r = md_round_coord(x + w);
        double y_min_r = md_round_coord(y);
        double y_max_r = md_round_coord(y + h);
        // Create per-ROI auxiliary structure carrying normalized box + confidence for ROI meta.
        GstStructure *detection = gst_structure_new("detection", "x_min", G_TYPE_DOUBLE, x_min_r, "x_max",
                                                    G_TYPE_DOUBLE, x_max_r, "y_min", G_TYPE_DOUBLE, y_min_r, "y_max",
                                                    G_TYPE_DOUBLE, y_max_r, "confidence", G_TYPE_DOUBLE, 1.0, NULL);
        // Atomic pairing requirement: either BOTH metadata types (ROI meta + ODMtd) are attached for this motion
        // rectangle or NONE. Strategy:
        // 1. Create ROI meta first; if that fails, skip entirely (no ODMtd added).
        // 2. Attempt ODMtd addition; if that fails, remove the ROI meta we just added to avoid orphan ROI.
        // 3. Only after both succeed do we link ids and add the detection structure.
        // This prevents orphan ODMtd entries and orphan ROI metas.
        GstVideoRegionOfInterestMeta *roi_meta =
            gst_buffer_add_video_region_of_interest_meta(buf, "motion", (guint)std::lround(_x), (guint)std::lround(_y),
                                                         (guint)std::lround(_w), (guint)std::lround(_h));
        if (!roi_meta) {
            GST_WARNING_OBJECT(self, "Failed to add ROI meta for motion ROI (atomic pair) -> skipping");
            gst_structure_free(detection);
            continue;
        }
        GstAnalyticsODMtd od_mtd;
        if (!gst_analytics_relation_meta_add_od_mtd(relation_meta, g_quark_from_string("motion"), (int)std::lround(_x),
                                                    (int)std::lround(_y), (int)std::lround(_w), (int)std::lround(_h),
                                                    1.0, &od_mtd)) {
            GST_WARNING_OBJECT(self, "Failed to add OD metadata for motion ROI (atomic pair) -> rolling back ROI meta");
            // Roll back ROI meta to maintain all-or-nothing invariant.
            gst_buffer_remove_meta(buf, (GstMeta *)roi_meta);
            gst_structure_free(detection);
            continue;
        }
        // Link ROI meta to its corresponding ODMtd entry by copying the generated id, then attach auxiliary params.
        roi_meta->id = od_mtd.id;
        gst_video_region_of_interest_meta_add_param(roi_meta, detection);
        GST_LOG_OBJECT(self, "Attached motion ROI id=%d rect=[%d,%d %dx%d] (atomic pair)", od_mtd.id,
                       (int)std::lround(_x), (int)std::lround(_y), (int)std::lround(_w), (int)std::lround(_h));
        attached++;
    }
    // Enumerate OD metadata for debug correlation
    gpointer state_iter = nullptr;
    GstAnalyticsODMtd od_iter;
    size_t od_count = 0;
    while (gst_analytics_relation_meta_iterate(relation_meta, &state_iter, gst_analytics_od_mtd_get_mtd_type(),
                                               &od_iter)) {
        ++od_count;
    }
    GST_LOG_OBJECT(self, "Total OD metadata after attachment: %zu", od_count);
    GST_INFO_OBJECT(self, "Motion ROIs attached: %zu", attached);
    if (self->debug_enabled) {
        g_print("[gvamotiondetect] frame=%" G_GUINT64_FORMAT " ROIs=%zu (relation-meta aggregate)\n", self->frame_index,
                attached);
        fflush(stdout);
    }
}

// Helper to merge overlapping motion rectangles in-place (simple O(n^2))
static void gst_gva_motion_detect_merge_rois(std::vector<MotionRect> &rois) {
    if (rois.empty())
        return;
    bool merged_any = true;
    while (merged_any) {
        merged_any = false;
        std::vector<MotionRect> out;
        std::vector<bool> used(rois.size(), false);
        for (size_t i = 0; i < rois.size(); ++i) {
            if (used[i])
                continue;
            MotionRect a = rois[i];
            for (size_t j = i + 1; j < rois.size(); ++j) {
                if (used[j])
                    continue;
                MotionRect b = rois[j];
                int ax2 = a.x + a.w, ay2 = a.y + a.h;
                int bx2 = b.x + b.w, by2 = b.y + b.h;
                bool overlap = !(bx2 < a.x || ax2 < b.x || by2 < a.y || ay2 < b.y);
                if (overlap) {
                    int nx = std::min(a.x, b.x);
                    int ny = std::min(a.y, b.y);
                    int nw = std::max(ax2, bx2) - nx;
                    int nh = std::max(ay2, by2) - ny;
                    a = MotionRect{nx, ny, nw, nh};
                    used[j] = true;
                    merged_any = true;
                }
            }
            out.push_back(a);
        }
        rois.swap(out);
    }
}

// ------------------- Motion Mask & Block Scan Helpers -------------------
// Build motion mask (absdiff -> blur -> threshold -> morphology) from current small frame and previous small frame.
static void md_build_motion_mask(const cv::UMat &curr_small, const cv::UMat &prev_small_gray, cv::UMat &morph_small,
                                 int pixel_diff_threshold) {
    int PIXEL_DIFF_THR = std::max(1, std::min(pixel_diff_threshold, 255));
    cv::UMat diff_small;
    cv::absdiff(curr_small, prev_small_gray, diff_small);
    cv::UMat blurred_small;
    cv::GaussianBlur(diff_small, blurred_small, cv::Size(3, 3), 0);
    cv::UMat thresh_small;
    cv::threshold(blurred_small, thresh_small, PIXEL_DIFF_THR, 255, cv::THRESH_BINARY);
    cv::UMat tmp;
    cv::Mat ksmall = cv::getStructuringElement(cv::MORPH_RECT, cv::Size(3, 3));
    cv::morphologyEx(thresh_small, tmp, cv::MORPH_OPEN, ksmall);
    cv::dilate(tmp, morph_small, ksmall, cv::Point(-1, -1), 1);
}

// Scan blocks with temporal agreement counters (used for CPU/system-memory path). Populates rois.
// Unified block scan honoring confirm_frames property.
static void md_scan_blocks(GstGvaMotionDetect *self, const cv::UMat &morph_small, int width, int height, int small_w,
                           int small_h, std::vector<MotionRect> &rois) {
    double min_rel_area = self->min_rel_area;
    if (min_rel_area < 0.0)
        min_rel_area = 0.0;
    if (min_rel_area > 0.25)
        min_rel_area = 0.25;
    double full_area = (double)width * (double)height;
    double scale_x = (double)width / (double)small_w;
    double scale_y = (double)height / (double)small_h;
    int block_full = std::max(16, self->block_size);
    int block_small_w = std::max(4, (int)std::round(block_full / scale_x));
    int block_small_h = std::max(4, (int)std::round(block_full / scale_y));
    double change_thr = std::max(0.0, std::min(1.0, self->motion_threshold));
    int required = std::max(1, self->confirm_frames);
    cv::Mat morph_cpu = morph_small.getMat(cv::ACCESS_READ);
    if (required > 1) {
        int grid_rows = (small_h + block_small_h - 1) / block_small_h;
        int grid_cols = (small_w + block_small_w - 1) / block_small_w;
        if (self->block_state.empty() || self->block_state.rows != grid_rows || self->block_state.cols != grid_cols)
            self->block_state = cv::Mat(grid_rows, grid_cols, CV_8U, cv::Scalar(0));
        for (int by = 0, gy = 0; by < small_h; by += block_small_h, ++gy) {
            int h_small = std::min(block_small_h, small_h - by);
            if (h_small < 4)
                break;
            for (int bx = 0, gx = 0; bx < small_w; bx += block_small_w, ++gx) {
                int w_small = std::min(block_small_w, small_w - bx);
                if (w_small < 4)
                    break;
                cv::Rect r_small(bx, by, w_small, h_small);
                cv::Mat sub = morph_cpu(r_small);
                int changed = cv::countNonZero(sub);
                double ratio = (double)changed / (double)(r_small.width * r_small.height);
                unsigned char &state = self->block_state.at<unsigned char>(gy, gx);
                if (ratio >= change_thr) {
                    if (state < required)
                        state++;
                } else {
                    if (state > 0)
                        state--;
                }
                if (state < required)
                    continue;
                int fx = (int)std::round(r_small.x * scale_x);
                int fy = (int)std::round(r_small.y * scale_y);
                int fw = (int)std::round(r_small.width * scale_x);
                int fh = (int)std::round(r_small.height * scale_y);
                double area_full = (double)fw * (double)fh;
                if (area_full / full_area < min_rel_area)
                    continue;
                const int PAD = 4;
                fx = std::max(0, fx - PAD);
                fy = std::max(0, fy - PAD);
                fw = std::min(width - fx, fw + 2 * PAD);
                fh = std::min(height - fy, fh + 2 * PAD);
                if (fx + fw > width)
                    fw = width - fx;
                if (fy + fh > height)
                    fh = height - fy;
                rois.push_back(MotionRect{fx, fy, fw, fh});
            }
        }
    } else { // single-frame immediate logic
        for (int by = 0; by < small_h; by += block_small_h) {
            int h_small = std::min(block_small_h, small_h - by);
            if (h_small < 4)
                break;
            for (int bx = 0; bx < small_w; bx += block_small_w) {
                int w_small = std::min(block_small_w, small_w - bx);
                if (w_small < 4)
                    break;
                cv::Rect r_small(bx, by, w_small, h_small);
                cv::Mat sub = morph_cpu(r_small);
                int changed = cv::countNonZero(sub);
                double ratio = (double)changed / (double)(r_small.width * r_small.height);
                if (ratio < change_thr)
                    continue;
                int fx = (int)std::round(r_small.x * scale_x);
                int fy = (int)std::round(r_small.y * scale_y);
                int fw = (int)std::round(r_small.width * scale_x);
                int fh = (int)std::round(r_small.height * scale_y);
                double area_full = (double)fw * (double)fh;
                if (area_full / full_area < min_rel_area)
                    continue;
                const int PAD = 4;
                fx = std::max(0, fx - PAD);
                fy = std::max(0, fy - PAD);
                fw = std::min(width - fx, fw + 2 * PAD);
                fh = std::min(height - fy, fh + 2 * PAD);
                if (fx + fw > width)
                    fw = width - fx;
                if (fy + fh > height)
                    fh = height - fy;
                rois.push_back(MotionRect{fx, fy, fw, fh});
            }
        }
    }
}

// In-place processing: get VASurfaceID via mapper
static GstFlowReturn gst_gva_motion_detect_transform_ip(GstBaseTransform *trans, GstBuffer *buf) {
    GstGvaMotionDetect *self = GST_GVA_MOTION_DETECT(trans);
    // CPU path when system memory negotiated
    if (!self->caps_is_va) {
        ++self->frame_index;
        int width = GST_VIDEO_INFO_WIDTH(&self->vinfo);
        int height = GST_VIDEO_INFO_HEIGHT(&self->vinfo);
        if (!width || !height)
            return GST_FLOW_OK;
        // Map Y plane (system memory) or fallback to VA luma if buffer is VAMemory
        GstVideoFrame vframe;
        cv::UMat curr_luma;
        gboolean mapped = gst_video_frame_map(&vframe, &self->vinfo, buf, GST_MAP_READ);
        if (mapped) {
            guint8 *y_data = (guint8 *)GST_VIDEO_FRAME_PLANE_DATA(&vframe, 0);
            int y_stride = GST_VIDEO_FRAME_PLANE_STRIDE(&vframe, 0);
            cv::Mat y_mat(height, width, CV_8UC1, y_data, y_stride);
            try {
                y_mat.copyTo(curr_luma);
            } catch (...) {
                gst_video_frame_unmap(&vframe);
                return GST_FLOW_OK;
            }
            gst_video_frame_unmap(&vframe);
        } else {
            VASurfaceID sid_cpu = gva_motion_detect_get_surface(self, buf);
            if (sid_cpu == VA_INVALID_SURFACE || !gva_motion_detect_map_luma(self, sid_cpu, width, height, curr_luma)) {
                GST_DEBUG_OBJECT(self, "CPU mode: unable to map frame (system or VA); skipping frame");
                return GST_FLOW_OK;
            }
        }
        // Downscale (software) to working size ~320 wide
        int target_w = std::min(320, width);
        double scale = (double)target_w / (double)width;
        int small_w = target_w;
        int small_h = std::max(1, (int)std::lround(height * scale));
        cv::UMat curr_small;
        cv::resize(curr_luma, curr_small, cv::Size(small_w, small_h), 0, 0, cv::INTER_LINEAR);
        if (self->prev_small_gray.empty()) {
            curr_small.copyTo(self->prev_small_gray);
            curr_luma.copyTo(self->prev_luma);
            return GST_FLOW_OK;
        }
        cv::UMat morph_small;
        md_build_motion_mask(curr_small, self->prev_small_gray, morph_small, self->pixel_diff_threshold);
        std::vector<MotionRect> rois;
        md_scan_blocks(self, morph_small, width, height, small_w, small_h, rois);
        if (!rois.empty()) {
            gst_gva_motion_detect_merge_rois(rois);
            gst_gva_motion_detect_process_and_attach(self, buf, rois, width, height);
        }
        curr_small.copyTo(self->prev_small_gray);
        curr_luma.copyTo(self->prev_luma);
        return GST_FLOW_OK;
    }
    // Acquire VA display via peer query if not yet set.
    if (!self->va_dpy) {
        if (!self->tried_va_query) {
            self->tried_va_query = TRUE;
            GstQuery *q = gst_query_new_context("gst.va.display.handle");
            if (gst_pad_peer_query(GST_BASE_TRANSFORM_SINK_PAD(trans), q)) {
                GstContext *ctx = nullptr;
                gst_query_parse_context(q, &ctx);
                if (ctx) {
                    GST_LOG_OBJECT(self, "Obtained VA context via peer query");
                    gst_gva_motion_detect_set_context(GST_ELEMENT(self), ctx);
                }
            }
            gst_query_unref(q);
        }
        if (!self->va_dpy) {
            GST_DEBUG_OBJECT(self, "No VADisplay (after peer query); pass-through frame=%" G_GUINT64_FORMAT,
                             self->frame_index);
            ++self->frame_index;
            return GST_FLOW_OK;
        }
    }

    // Map buffer to VA surface
    VASurfaceID sid = gva_motion_detect_get_surface(self, buf);
    if (sid == VA_INVALID_SURFACE) {
        GST_DEBUG_OBJECT(self, "Invalid VA surface; pass-through frame=%" G_GUINT64_FORMAT, self->frame_index);
        ++self->frame_index;
        return GST_FLOW_OK;
    }

    // Ensure surface ready
    VAStatus sync_st = vaSyncSurface(self->va_dpy, sid);
    if (sync_st != VA_STATUS_SUCCESS) {
        GST_WARNING_OBJECT(self, "vaSyncSurface failed sid=%u status=%d (%s)", sid, (int)sync_st, vaErrorStr(sync_st));
        ++self->frame_index;
        self->prev_sid = sid;
        return GST_FLOW_OK; // skip detection this frame
    }

    int width = GST_VIDEO_INFO_WIDTH(&self->vinfo);
    int height = GST_VIDEO_INFO_HEIGHT(&self->vinfo);
    if (!width || !height) {
        ++self->frame_index;
        self->prev_sid = sid;
        return GST_FLOW_OK;
    }

    // Map only luma plane; fallback to full conversion if it fails
    cv::UMat curr_luma;
    bool have_luma = gva_motion_detect_map_luma(self, sid, width, height, curr_luma);
    if (!have_luma) {
        GST_DEBUG_OBJECT(self, "Luma mapping failed; fallback to convertFromVASurface + cvtColor");
        cv::UMat frame_gpu;
        if (!gva_motion_detect_convert_from_surface(self, sid, width, height, frame_gpu)) {
            ++self->frame_index;
            self->prev_sid = sid;
            return GST_FLOW_OK;
        }
        try {
            cv::cvtColor(frame_gpu, curr_luma, cv::COLOR_BGR2GRAY);
        } catch (const cv::Exception &e) {
            GST_WARNING_OBJECT(self, "cvtColor (fallback) failed: %s", e.what());
            ++self->frame_index;
            self->prev_sid = sid;
            return GST_FLOW_OK;
        }
    }

    // Downscale to small working resolution (keep aspect ratio). Target width ~320.
    int target_w = std::min(320, width);
    double scale = (double)target_w / (double)width;
    int small_w = target_w;
    int small_h = std::max(1, (int)std::lround(height * scale));
    cv::UMat curr_small;
    bool va_scaled = false;
    {
        VASurfaceID scaled_sid = VA_INVALID_SURFACE;
        if (gst_gva_motion_detect_va_downscale(self, sid, width, height, small_w, small_h, scaled_sid)) {
            cv::UMat scaled_luma;
            if (gva_motion_detect_map_luma(self, scaled_sid, small_w, small_h, scaled_luma)) {
                scaled_luma.copyTo(curr_small);
                va_scaled = true;
            }
        }
    }
    if (!va_scaled) {
        // Software resize of luma
        cv::resize(curr_luma, curr_small, cv::Size(small_w, small_h), 0, 0, cv::INTER_LINEAR);
    }

    // If first frame, store and exit
    if (self->prev_small_gray.empty()) {
        curr_small.copyTo(self->prev_small_gray);
        curr_luma.copyTo(self->prev_luma);
        self->prev_sid = sid;
        ++self->frame_index;
        return GST_FLOW_OK;
    }

    cv::UMat morph_small;
    md_build_motion_mask(curr_small, self->prev_small_gray, morph_small, self->pixel_diff_threshold);
    std::vector<MotionRect> rois;
    md_scan_blocks(self, morph_small, width, height, small_w, small_h, rois);
    if (!rois.empty()) {
        gst_gva_motion_detect_merge_rois(rois);
        gst_gva_motion_detect_process_and_attach(self, buf, rois, width, height);
    }

    // Update previous frames
    curr_small.copyTo(self->prev_small_gray);
    curr_luma.copyTo(self->prev_luma);
    self->prev_sid = sid;
    ++self->frame_index;
    return GST_FLOW_OK;
}

static void gst_gva_motion_detect_finalize(GObject *obj) {
    GstGvaMotionDetect *self = GST_GVA_MOTION_DETECT(obj);
    if (self->scaled_sid != VA_INVALID_SURFACE) {
        vaDestroySurfaces(self->va_dpy, &self->scaled_sid, 1);
        self->scaled_sid = VA_INVALID_SURFACE;
    }
    g_mutex_clear(&self->meta_mutex);
    G_OBJECT_CLASS(gst_gva_motion_detect_parent_class)->finalize(obj);
}

static void gst_gva_motion_detect_class_init(GstGvaMotionDetectClass *klass) {
    GstElementClass *eclass = GST_ELEMENT_CLASS(klass);
    GstBaseTransformClass *bclass = GST_BASE_TRANSFORM_CLASS(klass);
    GObjectClass *oclass = G_OBJECT_CLASS(klass);

    GST_DEBUG_CATEGORY_INIT(gst_gva_motion_detect_debug, "gvamotiondetect", 0, "GVA motion detect filter");

    gst_element_class_set_static_metadata(
        eclass, "Motion detect (auto GPU/CPU)", "Filter/Video",
        "Automatically uses VA surface path when VAMemory caps negotiated; otherwise system memory path", "dlstreamer");

    gst_element_class_add_static_pad_template(eclass, &sink_templ);
    gst_element_class_add_static_pad_template(eclass, &src_templ);

    eclass->set_context = gst_gva_motion_detect_set_context;
    eclass->query = gst_gva_motion_detect_query;

    bclass->start = gst_gva_motion_detect_start;
    bclass->set_caps = gst_gva_motion_detect_set_caps;
    bclass->transform_caps = gst_gva_motion_detect_transform_caps;
    bclass->transform_ip = gst_gva_motion_detect_transform_ip;
    oclass->finalize = gst_gva_motion_detect_finalize;

    // Set property handlers before installing properties (required by GObject)
    oclass->set_property = gst_gva_motion_detect_set_property;
    oclass->get_property = gst_gva_motion_detect_get_property;

    g_object_class_install_property(
        oclass, PROP_BLOCK_SIZE,
        g_param_spec_int("block-size", "Block Size",
                         "Full-resolution block size (pixels) used for grid motion detection", 16, 512, 64,
                         (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(oclass, PROP_MOTION_THRESHOLD,
                                    g_param_spec_double("motion-threshold", "Motion Threshold",
                                                        "Per-block changed pixel ratio required to flag motion (0..1)",
                                                        0.0, 1.0, 0.05,
                                                        (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(oclass, PROP_MIN_PERSISTENCE,
                                    g_param_spec_int("min-persistence", "Min Persistence",
                                                     "Frames an ROI must persist before being emitted", 1, 30, 2,
                                                     (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
    g_object_class_install_property(oclass, PROP_MAX_MISS,
                                    g_param_spec_int("max-miss", "Max Miss",
                                                     "Grace frames after last match before ROI is dropped", 0, 30, 1,
                                                     (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
    g_object_class_install_property(
        oclass, PROP_IOU_THRESHOLD,
        g_param_spec_double("iou-threshold", "IoU Threshold", "IoU threshold for matching ROIs frame-to-frame (0..1)",
                            0.0, 1.0, 0.3, (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
    g_object_class_install_property(
        oclass, PROP_SMOOTH_ALPHA,
        g_param_spec_double("smooth-alpha", "Smooth Alpha", "EMA smoothing factor for ROI coordinates (0..1)", 0.0, 1.0,
                            0.5, (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(
        oclass, PROP_CONFIRM_FRAMES,
        g_param_spec_int("confirm-frames", "Confirm Frames",
                         "Consecutive frames required to confirm motion block (1=single-frame immediate)", 1, 10, 1,
                         (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(
        oclass, PROP_PIXEL_DIFF_THRESHOLD,
        g_param_spec_int(
            "pixel-diff-threshold", "Pixel Diff Threshold",
            "Per-pixel absolute luma difference used before blur+threshold (1..255). Lower = more sensitive", 1, 255,
            15, (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(oclass, PROP_MIN_REL_AREA,
                                    g_param_spec_double("min-rel-area", "Min Relative Area",
                                                        "Minimum relative frame area (0..0.25) required for a motion "
                                                        "rectangle before merging/tracking (filters tiny noise boxes)",
                                                        0.0, 0.25, 0.0005,
                                                        (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
}

static void gst_gva_motion_detect_init(GstGvaMotionDetect *self) {
    gst_base_transform_set_in_place(GST_BASE_TRANSFORM(self), TRUE);
    gst_base_transform_set_passthrough(GST_BASE_TRANSFORM(self), FALSE);
    self->blur_kernel = 21;
    self->blur_sigma = 5.0;
    self->frame_index = 0;
    self->block_size = 64;         // default
    self->motion_threshold = 0.05; // default changed pixel ratio
    self->min_persistence = 2;
    self->max_miss = 1;
    self->iou_threshold = 0.3;
    self->smooth_alpha = 0.5;
    // Use single-frame immediate detection by default (matches original VA path sensitivity).
    // Users can raise to >1 to require temporal confirmation.
    self->confirm_frames = 1;
    self->pixel_diff_threshold = 15; // default per-pixel difference threshold
    self->min_rel_area = 0.0005;     // default minimum relative area (0.05% of frame)
    self->block_state_w = 0;
    self->block_state_h = 0;
    self->va_dpy = nullptr;
    self->va_display = nullptr;
    self->prev_sid = VA_INVALID_SURFACE;
    self->scaled_sid = VA_INVALID_SURFACE;
    self->scaled_w = 0;
    self->scaled_h = 0;
    // Debug environment parsing (simple, no logging here to avoid early flood)
    const gchar *env_dbg = g_getenv("GVA_MD_PRINT");
    self->debug_enabled = (env_dbg && env_dbg[0] != '\0' && g_strcmp0(env_dbg, "0") != 0);
    const gchar *env_int = g_getenv("GVA_MD_PRINT_INTERVAL");
    self->debug_interval = 30;
    if (env_int && env_int[0] != '\0') {
        gchar *endp = nullptr;
        unsigned long long v = g_ascii_strtoull(env_int, &endp, 10);
        if (endp && *endp == '\0' && v > 0 && v < 1000000UL)
            self->debug_interval = (guint)v;
    }
    self->last_debug_frame = (guint64)-1;
    self->tried_va_query = FALSE;
    g_mutex_init(&self->meta_mutex);
}

// ---------------- Tracking & Smoothing Helpers ----------------
static inline double md_iou(const MotionRect &a, const MotionRect &b) {
    int x1 = std::max(a.x, b.x);
    int y1 = std::max(a.y, b.y);
    int x2 = std::min(a.x + a.w, b.x + b.w);
    int y2 = std::min(a.y + a.h, b.y + b.h);
    int iw = std::max(0, x2 - x1);
    int ih = std::max(0, y2 - y1);
    int inter = iw * ih;
    int areaA = a.w * a.h;
    int areaB = b.w * b.h;
    if (inter == 0)
        return 0.0;
    return (double)inter / (double)(areaA + areaB - inter);
}

static void gst_gva_motion_detect_process_and_attach(GstGvaMotionDetect *self, GstBuffer *buf,
                                                     const std::vector<MotionRect> &raw_rois, int width, int height) {
    std::vector<bool> matched(raw_rois.size(), false);
    for (auto &t : self->tracked_rois)
        t.misses++;
    for (size_t i = 0; i < raw_rois.size(); ++i) {
        const MotionRect &r = raw_rois[i];
        double best_iou = 0.0;
        int best_idx = -1;
        for (size_t j = 0; j < self->tracked_rois.size(); ++j) {
            double iou = md_iou(r, {self->tracked_rois[j].x, self->tracked_rois[j].y, self->tracked_rois[j].w,
                                    self->tracked_rois[j].h});
            if (iou > best_iou) {
                best_iou = iou;
                best_idx = (int)j;
            }
        }
        if (best_idx >= 0 && best_iou >= self->iou_threshold) {
            auto &t = self->tracked_rois[best_idx];
            t.x = r.x;
            t.y = r.y;
            t.w = r.w;
            t.h = r.h;
            double a = self->smooth_alpha;
            t.sx = a * r.x + (1 - a) * t.sx;
            t.sy = a * r.y + (1 - a) * t.sy;
            t.sw = a * r.w + (1 - a) * t.sw;
            t.sh = a * r.h + (1 - a) * t.sh;
            t.age++;
            t.misses = 0;
            matched[i] = true;
        }
    }
    for (size_t i = 0; i < raw_rois.size(); ++i)
        if (!matched[i]) {
            const MotionRect &r = raw_rois[i];
            GstGvaMotionDetect::TrackedROI t{r.x,         r.y,         r.w,         r.h, (double)r.x,
                                             (double)r.y, (double)r.w, (double)r.h, 1,   0};
            self->tracked_rois.push_back(t);
        }
    std::vector<MotionRect> stable;
    for (auto &t : self->tracked_rois)
        if (t.age >= self->min_persistence && t.misses == 0) {
            MotionRect out{(int)std::lround(t.sx), (int)std::lround(t.sy), (int)std::lround(t.sw),
                           (int)std::lround(t.sh)};
            if (out.x < 0)
                out.x = 0;
            if (out.y < 0)
                out.y = 0;
            if (out.x + out.w > width)
                out.w = width - out.x;
            if (out.y + out.h > height)
                out.h = height - out.y;
            stable.push_back(out);
        }
    self->tracked_rois.erase(
        std::remove_if(self->tracked_rois.begin(), self->tracked_rois.end(),
                       [self](const GstGvaMotionDetect::TrackedROI &t) { return t.misses > self->max_miss; }),
        self->tracked_rois.end());
    if (!stable.empty())
        gst_gva_motion_detect_attach_rois(self, buf, stable, width, height);
}
