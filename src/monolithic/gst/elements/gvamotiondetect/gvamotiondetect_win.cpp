/*******************************************************************************
 * Copyright (C) 2025-2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#include "gvamotiondetect.h"
#include <algorithm>
#include <cmath>
#include <glib.h>
#include <gst/base/gstbasetransform.h>
#include <gst/gst.h>
#include <gst/video/video.h>
#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>
#include <vector>

#define GST_USE_UNSTABLE_API
#include <gst/d3d11/gstd3d11.h>
#include <opencv2/core/directx.hpp>

#include <dlstreamer/gst/videoanalytics/video_frame.h>

// Linux parity helper: round normalized coordinates to 3 decimal places to reduce metadata verbosity.
static inline double md_round_coord(double v) {
    return std::floor(v * 1000.0 + 0.5) / 1000.0;
}

GST_DEBUG_CATEGORY_STATIC(gst_gva_motion_detect_debug_win);
#define GST_CAT_DEFAULT gst_gva_motion_detect_debug_win

struct MotionRectWin {
    int x, y, w, h;
};

// Build motion mask (software path) analogous to Linux md_build_motion_mask.
// Inputs: current small-scale grayscale frame and previous frame; Output: morph (opened+dilated) binary mask.
static void md_build_motion_mask(const cv::UMat &curr_small, const cv::UMat &prev_small_gray, cv::UMat &morph,
                                 int pixel_diff_threshold) {
    cv::UMat diff, blur, thr;
    cv::absdiff(curr_small, prev_small_gray, diff);
    cv::GaussianBlur(diff, blur, cv::Size(3, 3), 0);
    cv::threshold(blur, thr, std::max(1, std::min(255, pixel_diff_threshold)), 255, cv::THRESH_BINARY);
    cv::UMat tmp;
    cv::Mat k = cv::getStructuringElement(cv::MORPH_RECT, cv::Size(3, 3));
    cv::morphologyEx(thr, tmp, cv::MORPH_OPEN, k);
    cv::dilate(tmp, morph, k);
}
// (md_scan_blocks & merge helpers moved below struct for MSVC complete type availability)

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
    gboolean caps_is_d3d11; // set when upstream negotiated memory:D3D11Memory
    int block_size;
    double motion_threshold;
    int min_persistence;
    int max_miss;
    double iou_threshold;
    double smooth_alpha;
    int confirm_frames;       // consecutive frames required (1=immediate)
    int pixel_diff_threshold; // per-pixel luma diff threshold (1..255)
    double min_rel_area;      // minimum relative area (0..0.25) for a motion rectangle
    cv::UMat prev_small_gray;
    cv::UMat prev_luma;  // retained for CPU path only
    cv::Mat block_state; // CV_8U agreement counters
    struct Track {
        int x, y, w, h;
        double sx, sy, sw, sh;
        int age;
        int miss;
    };
    std::vector<Track> tracks;
    uint64_t frame_index;
    GMutex meta_mutex; // protect metadata writes

    // D3D11 GPU path state
    GstD3D11Device *d3d11_device;  // obtained via gst.d3d11.device.handle context
    GstD3D11Converter *d3d11_conv; // lazy: (re)built when small size changes
    GstBuffer *small_buf;          // holds the pooled small NV12 D3D11 GstMemory
    int small_w;
    int small_h;
    gboolean cv_ocl_ready;           // cv::directx OpenCL context initialized from this device
    gboolean tried_d3d11_peer_query; // one-shot peer query on first frame
};

// Scan blocks to produce raw motion rectangles (parity with Linux md_scan_blocks, adapted to Windows struct & members)
static void md_scan_blocks(GstGvaMotionDetect *self, const cv::UMat &morph, int width, int height, int small_w,
                           int small_h, std::vector<MotionRectWin> &raw) {
    double scale_x = (double)width / (double)small_w;
    double scale_y = (double)height / (double)small_h;
    double full_area = (double)width * height;
    double min_rel = std::clamp(self->min_rel_area, 0.0, 0.25);
    int block_full = std::max(16, self->block_size);
    int bs_w = std::max(4, (int)std::round(block_full / scale_x));
    int bs_h = std::max(4, (int)std::round(block_full / scale_y));
    double CHANGE_THR = std::clamp(self->motion_threshold, 0.0, 1.0);
    cv::Mat m_cpu = morph.getMat(cv::ACCESS_READ);
    int required = std::max(1, self->confirm_frames);
    if (required > 1) {
        int rows = (small_h + bs_h - 1) / bs_h;
        int cols = (small_w + bs_w - 1) / bs_w;
        if (self->block_state.empty() || self->block_state.rows != rows || self->block_state.cols != cols)
            self->block_state = cv::Mat(rows, cols, CV_8U, cv::Scalar(0));
        for (int by = 0, gy = 0; by < small_h; by += bs_h, ++gy) {
            int h_small = std::min(bs_h, small_h - by);
            if (h_small < 4)
                break;
            for (int bx = 0, gx = 0; bx < small_w; bx += bs_w, ++gx) {
                int w_small = std::min(bs_w, small_w - bx);
                if (w_small < 4)
                    break;
                cv::Rect r(bx, by, w_small, h_small);
                cv::Mat sub = m_cpu(r);
                int changed = cv::countNonZero(sub);
                double ratio = (double)changed / (double)(r.width * r.height);
                unsigned char &state = self->block_state.at<unsigned char>(gy, gx);
                if (ratio >= CHANGE_THR) {
                    if (state < required)
                        state++;
                } else {
                    if (state > 0)
                        state--;
                }
                if (state < required)
                    continue;
                int fx = (int)std::round(r.x * scale_x);
                int fy = (int)std::round(r.y * scale_y);
                int fw = (int)std::round(r.width * scale_x);
                int fh = (int)std::round(r.height * scale_y);
                double area_full = (double)fw * fh;
                if (area_full / full_area < min_rel)
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
                raw.push_back({fx, fy, fw, fh});
            }
        }
    } else { // single-frame immediate logic
        for (int by = 0; by < small_h; by += bs_h) {
            int h_small = std::min(bs_h, small_h - by);
            if (h_small < 4)
                break;
            for (int bx = 0; bx < small_w; bx += bs_w) {
                int w_small = std::min(bs_w, small_w - bx);
                if (w_small < 4)
                    break;
                cv::Rect r(bx, by, w_small, h_small);
                cv::Mat sub = m_cpu(r);
                int changed = cv::countNonZero(sub);
                double ratio = (double)changed / (double)(r.width * r.height);
                if (ratio < CHANGE_THR)
                    continue;
                int fx = (int)std::round(r.x * scale_x);
                int fy = (int)std::round(r.y * scale_y);
                int fw = (int)std::round(r.width * scale_x);
                int fh = (int)std::round(r.height * scale_y);
                double area_full = (double)fw * fh;
                if (area_full / full_area < min_rel)
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
                raw.push_back({fx, fy, fw, fh});
            }
        }
    }
}

// Merge overlapping motion rectangles (parity with Linux gst_gva_motion_detect_merge_rois)
static void gst_gva_motion_detect_merge_rois(std::vector<MotionRectWin> &raw) {
    bool merged = true;
    while (merged) {
        merged = false;
        std::vector<MotionRectWin> out;
        std::vector<char> used(raw.size(), 0);
        for (size_t i = 0; i < raw.size(); ++i) {
            if (used[i])
                continue;
            MotionRectWin a = raw[i];
            for (size_t j = i + 1; j < raw.size(); ++j) {
                if (used[j])
                    continue;
                MotionRectWin b = raw[j];
                bool overlap = !(b.x + b.w < a.x || a.x + a.w < b.x || b.y + b.h < a.y || a.y + a.h < b.y);
                if (overlap) {
                    int nx = std::min(a.x, b.x);
                    int ny = std::min(a.y, b.y);
                    int nw = std::max(a.x + a.w, b.x + b.w) - nx;
                    int nh = std::max(a.y + a.h, b.y + b.h) - ny;
                    a = {nx, ny, nw, nh};
                    used[j] = 1;
                    merged = true;
                }
            }
            out.push_back(a);
        }
        raw.swap(out);
    }
}

struct _GstGvaMotionDetectClass {
    GstBaseTransformClass parent_class;
};

G_DEFINE_TYPE(GstGvaMotionDetect, gst_gva_motion_detect, GST_TYPE_BASE_TRANSFORM)

static void gst_gva_motion_detect_set_property(GObject *obj, guint id, const GValue *val, GParamSpec *pspec) {
    GstGvaMotionDetect *self = GST_GVA_MOTION_DETECT(obj);
    switch (id) {
    case PROP_BLOCK_SIZE:
        self->block_size = g_value_get_int(val);
        break;
    case PROP_MOTION_THRESHOLD:
        self->motion_threshold = g_value_get_double(val);
        break;
    case PROP_MIN_PERSISTENCE:
        self->min_persistence = std::max(1, g_value_get_int(val));
        break;
    case PROP_MAX_MISS:
        self->max_miss = std::max(0, g_value_get_int(val));
        break;
    case PROP_IOU_THRESHOLD:
        self->iou_threshold = std::clamp(g_value_get_double(val), 0.0, 1.0);
        break;
    case PROP_SMOOTH_ALPHA: {
        double a = g_value_get_double(val);
        self->smooth_alpha = a < 0 ? 0 : (a > 1 ? 1 : a);
        break;
    }
    case PROP_CONFIRM_FRAMES:
        self->confirm_frames = std::max(1, g_value_get_int(val));
        break;
    case PROP_PIXEL_DIFF_THRESHOLD:
        self->pixel_diff_threshold = std::max(1, std::min(255, g_value_get_int(val)));
        break;
    case PROP_MIN_REL_AREA: {
        double mra = g_value_get_double(val);
        if (mra < 0.0)
            mra = 0.0;
        if (mra > 0.25)
            mra = 0.25;
        self->min_rel_area = mra;
        break;
    }
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(obj, id, pspec);
    }
}
static void gst_gva_motion_detect_get_property(GObject *obj, guint id, GValue *val, GParamSpec *pspec) {
    GstGvaMotionDetect *self = GST_GVA_MOTION_DETECT(obj);
    switch (id) {
    case PROP_BLOCK_SIZE:
        g_value_set_int(val, self->block_size);
        break;
    case PROP_MOTION_THRESHOLD:
        g_value_set_double(val, self->motion_threshold);
        break;
    case PROP_MIN_PERSISTENCE:
        g_value_set_int(val, self->min_persistence);
        break;
    case PROP_MAX_MISS:
        g_value_set_int(val, self->max_miss);
        break;
    case PROP_IOU_THRESHOLD:
        g_value_set_double(val, self->iou_threshold);
        break;
    case PROP_SMOOTH_ALPHA:
        g_value_set_double(val, self->smooth_alpha);
        break;
    case PROP_CONFIRM_FRAMES:
        g_value_set_int(val, self->confirm_frames);
        break;
    case PROP_PIXEL_DIFF_THRESHOLD:
        g_value_set_int(val, self->pixel_diff_threshold);
        break;
    case PROP_MIN_REL_AREA:
        g_value_set_double(val, self->min_rel_area);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(obj, id, pspec);
    }
}

static inline double md_iou(const MotionRectWin &a, const MotionRectWin &b) {
    int x1 = std::max(a.x, b.x);
    int y1 = std::max(a.y, b.y);
    int x2 = std::min(a.x + a.w, b.x + b.w);
    int y2 = std::min(a.y + a.h, b.y + b.h);
    int iw = std::max(0, x2 - x1);
    int ih = std::max(0, y2 - y1);
    int inter = iw * ih;
    if (!inter)
        return 0.0;
    return (double)inter / (double)(a.w * a.h + b.w * b.h - inter);
}

// ---------------- D3D11 context negotiation ----------------
static void gst_gva_motion_detect_set_context(GstElement *elem, GstContext *context) {
    GstGvaMotionDetect *self = GST_GVA_MOTION_DETECT(elem);
    const gchar *ctype = gst_context_get_context_type(context);
    if (g_strcmp0(ctype, "gst.d3d11.device.handle") == 0 && !self->d3d11_device) {
        const GstStructure *s = gst_context_get_structure(context);
        GstD3D11Device *dev = nullptr;
        if (gst_structure_get(s, "device", GST_TYPE_D3D11_DEVICE, &dev, NULL) && dev) {
            self->d3d11_device = dev; // transfer ref
            GST_INFO_OBJECT(self, "Obtained GstD3D11Device %p", dev);
        }
    }
    GST_ELEMENT_CLASS(gst_gva_motion_detect_parent_class)->set_context(elem, context);
}

// ---------------- D3D11 GPU path helpers ----------------

// RAII guard for gst_d3d11_device_lock / gst_d3d11_device_unlock.
namespace {
struct D3D11DeviceLockGuard {
    GstD3D11Device *dev;
    explicit D3D11DeviceLockGuard(GstD3D11Device *d) : dev(d) {
        gst_d3d11_device_lock(dev);
    }
    ~D3D11DeviceLockGuard() {
        gst_d3d11_device_unlock(dev);
    }
    D3D11DeviceLockGuard(const D3D11DeviceLockGuard &) = delete;
    D3D11DeviceLockGuard &operator=(const D3D11DeviceLockGuard &) = delete;
};
} // namespace

// Release cached D3D11 converter + small destination buffer (safe to call when not allocated).
static void gst_gva_motion_detect_release_d3d11_small(GstGvaMotionDetect *self) {
    if (self->d3d11_conv) {
        gst_object_unref(self->d3d11_conv);
        self->d3d11_conv = nullptr;
    }
    if (self->small_buf) {
        gst_buffer_unref(self->small_buf);
        self->small_buf = nullptr;
    }
    self->small_w = 0;
    self->small_h = 0;
}

// Ensure lazy-built converter + small destination buffer matching requested dimensions.
// Returns TRUE on success.
static gboolean gst_gva_motion_detect_ensure_d3d11_converter(GstGvaMotionDetect *self, int src_w, int src_h, int dst_w,
                                                             int dst_h) {
    if (self->d3d11_conv && self->small_buf && self->small_w == dst_w && self->small_h == dst_h)
        return TRUE;
    gst_gva_motion_detect_release_d3d11_small(self);
    if (!self->d3d11_device)
        return FALSE;

    // Allocate small NV12 destination texture via the element's device.
    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width = (UINT)dst_w;
    desc.Height = (UINT)dst_h;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_NV12;
    desc.SampleDesc.Count = 1;
    desc.SampleDesc.Quality = 0;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
    desc.CPUAccessFlags = 0;
    desc.MiscFlags = 0;

    auto *allocator = GST_D3D11_ALLOCATOR(g_object_new(gst_d3d11_allocator_get_type(), nullptr));
    GstMemory *mem = gst_d3d11_allocator_alloc(allocator, self->d3d11_device, &desc);
    gst_object_unref(allocator);
    if (!mem) {
        GST_WARNING_OBJECT(self, "gst_d3d11_allocator_alloc failed (%dx%d NV12)", dst_w, dst_h);
        return FALSE;
    }
    GstBuffer *buf = gst_buffer_new();
    gst_buffer_append_memory(buf, mem);
    self->small_buf = buf;

    GstVideoInfo in_info, out_info;
    gst_video_info_set_format(&in_info, GST_VIDEO_FORMAT_NV12, src_w, src_h);
    gst_video_info_set_format(&out_info, GST_VIDEO_FORMAT_NV12, dst_w, dst_h);

    GstStructure *config =
        gst_structure_new("converter-config", GST_D3D11_CONVERTER_OPT_BACKEND, GST_TYPE_D3D11_CONVERTER_BACKEND,
                          (guint)GST_D3D11_CONVERTER_BACKEND_VIDEO_PROCESSOR, NULL);
    GstD3D11Converter *conv = gst_d3d11_converter_new(self->d3d11_device, &in_info, &out_info, config);
    if (!conv) {
        GST_WARNING_OBJECT(self, "gst_d3d11_converter_new failed");
        gst_gva_motion_detect_release_d3d11_small(self);
        return FALSE;
    }
    g_object_set(conv, "src-x", 0, "src-y", 0, "src-width", src_w, "src-height", src_h, "dest-x", 0, "dest-y", 0,
                 "dest-width", dst_w, "dest-height", dst_h, NULL);
    self->d3d11_conv = conv;
    self->small_w = dst_w;
    self->small_h = dst_h;
    GST_INFO_OBJECT(self, "D3D11 converter ready: %dx%d -> %dx%d (NV12)", src_w, src_h, dst_w, dst_h);
    return TRUE;
}

// Map the D3D11 buffer's texture -> downscale -> OpenCV D3D11 interop -> GRAY cv::UMat.
// Returns TRUE on success, FALSE to request CPU fallback.
static gboolean gst_gva_motion_detect_d3d11_to_small_gray(GstGvaMotionDetect *self, GstBuffer *buf, int width,
                                                          int height, int small_w, int small_h, cv::UMat &curr_small) {
    GstMemory *src_mem = gst_buffer_peek_memory(buf, 0);
    if (!src_mem || !gst_is_d3d11_memory(src_mem))
        return FALSE;

    if (!gst_gva_motion_detect_ensure_d3d11_converter(self, width, height, small_w, small_h))
        return FALSE;

    // GPU downscale: driver handles device lock internally.
    if (!gst_d3d11_converter_convert_buffer(self->d3d11_conv, buf, self->small_buf)) {
        GST_WARNING_OBJECT(self, "gst_d3d11_converter_convert_buffer failed; falling back to CPU");
        return FALSE;
    }

    GstMemory *dst_mem = gst_buffer_peek_memory(self->small_buf, 0);
    ID3D11Texture2D *small_tex =
        (ID3D11Texture2D *)gst_d3d11_memory_get_resource_handle(GST_D3D11_MEMORY_CAST(dst_mem));
    if (!small_tex)
        return FALSE;

    // One-shot OpenCV OpenCL<->D3D11 context init from the element's device.
    if (!self->cv_ocl_ready) {
        ID3D11Device *id3d = gst_d3d11_device_get_device_handle(self->d3d11_device);
        if (!id3d)
            return FALSE;
        try {
            cv::directx::ocl::initializeContextFromD3D11Device(id3d);
            self->cv_ocl_ready = TRUE;
            GST_INFO_OBJECT(self, "cv::directx OpenCL context initialized from D3D11 device");
        } catch (const cv::Exception &e) {
            GST_WARNING_OBJECT(self, "initializeContextFromD3D11Device failed: %s", e.what());
            return FALSE;
        }
    }

    // Import the small NV12 texture into OpenCV; driver interop requires holding the device lock.
    D3D11DeviceLockGuard lock_guard(self->d3d11_device);
    try {
        cv::UMat small_bgr;
        cv::directx::convertFromD3D11Texture2D(small_tex, small_bgr); // NV12 -> BGR on GPU
        cv::cvtColor(small_bgr, curr_small, cv::COLOR_BGR2GRAY);      // BGR -> GRAY on GPU
    } catch (const cv::Exception &e) {
        GST_WARNING_OBJECT(self, "cv::directx::convertFromD3D11Texture2D failed: %s; falling back to CPU", e.what());
        return FALSE;
    }
    return TRUE;
}

static gboolean gst_gva_motion_detect_start(GstBaseTransform *t) {
    GstGvaMotionDetect *self = GST_GVA_MOTION_DETECT(t);
    self->caps_is_d3d11 = FALSE;
    self->frame_index = 0;
    self->tried_d3d11_peer_query = FALSE;
    // Invite upstream to provide a D3D11 device context (non-blocking; handled via set_context).
    gst_element_post_message(GST_ELEMENT(self),
                             gst_message_new_need_context(GST_OBJECT(self), "gst.d3d11.device.handle"));
    return TRUE;
}

static gboolean gst_gva_motion_detect_set_caps(GstBaseTransform *t, GstCaps *in, GstCaps *out) {
    GstGvaMotionDetect *self = GST_GVA_MOTION_DETECT(t);
    if (!gst_video_info_from_caps(&self->vinfo, in))
        return FALSE;

    gboolean is_d3d11 = FALSE;
    guint caps_size = gst_caps_get_size(in);
    for (guint i = 0; i < caps_size; ++i) {
        const GstCapsFeatures *features = gst_caps_get_features(in, i);
        if (features && gst_caps_features_contains(features, GST_CAPS_FEATURE_MEMORY_D3D11_MEMORY)) {
            is_d3d11 = TRUE;
            break;
        }
    }

    self->caps_is_d3d11 = is_d3d11;
    // Reset tracking + algorithm state on caps change (resolution may differ).
    self->tracks.clear();
    self->block_state.release();
    self->prev_small_gray.release();
    self->prev_luma.release();
    gst_gva_motion_detect_release_d3d11_small(self);
    return TRUE;
}

// Attach motion metadata using an atomic pairing strategy identical to Linux implementation: for each published track
// either BOTH metadata types (relation ODMtd + ROI meta with detection structure) are added or NONE. If relation meta
// creation fails, attachment for all motion ROIs is skipped to avoid inconsistent dual-layer state.
// Helper to attach motion ROIs and analytics metadata (Windows parity with Linux):
// Attaches TWO metadata layers per motion ROI:
// 1. GstAnalyticsRelationMeta (aggregate ODMtd entries holding integer pixel coords + confidence)
// 2. GstVideoRegionOfInterestMeta (per-ROI meta with "detection" structure containing normalized, rounded coords)
// Atomic per-ROI: either both are added or none. If relation meta cannot be obtained, all motion ROIs are skipped.
static void gst_gva_motion_detect_attach_metadata(GstGvaMotionDetect *self, GstBuffer *buf, int width, int height) {
    std::vector<_GstGvaMotionDetect::Track> publish;
    publish.reserve(self->tracks.size());
    for (auto &tr : self->tracks) {
        if (tr.age >= self->min_persistence && tr.miss == 0)
            publish.push_back(tr);
    }
    if (publish.empty())
        return;
    if (!gst_buffer_is_writable(buf)) {
        GstBuffer *w = gst_buffer_make_writable(buf);
        if (w != buf)
            buf = w;
    }
    if (!gst_buffer_is_writable(buf))
        return;
    g_mutex_lock(&self->meta_mutex);
    // Obtain or create relation meta; if unavailable skip all attachments (all-or-nothing frame-level constraint).
    GstAnalyticsRelationMeta *relation_meta = gst_buffer_get_analytics_relation_meta(buf);
    if (!relation_meta) {
        relation_meta = gst_buffer_add_analytics_relation_meta(buf);
        if (relation_meta)
            GST_LOG_OBJECT(self, "Added new GstAnalyticsRelationMeta %p (Windows)", relation_meta);
    }
    if (!relation_meta) {
        GST_WARNING_OBJECT(
            self, "Failed to obtain/create GstAnalyticsRelationMeta; skipping motion metadata (Windows atomic)");
        g_mutex_unlock(&self->meta_mutex);
        return;
    }
    for (auto &tr : publish) {
        double nx = std::clamp(tr.sx / (double)width, 0.0, 1.0);
        double ny = std::clamp(tr.sy / (double)height, 0.0, 1.0);
        double nw = std::clamp(tr.sw / (double)width, 0.0, 1.0);
        double nh = std::clamp(tr.sh / (double)height, 0.0, 1.0);
        double x_min_r = md_round_coord(nx);
        double y_min_r = md_round_coord(ny);
        double x_max_r = md_round_coord(std::min(1.0, nx + nw));
        double y_max_r = md_round_coord(std::min(1.0, ny + nh));
        double _x = nx * width + 0.5;
        double _y = ny * height + 0.5;
        double _w = nw * width + 0.5;
        double _h = nh * height + 0.5;
        GstStructure *detection = gst_structure_new("detection", "x_min", G_TYPE_DOUBLE, x_min_r, "x_max",
                                                    G_TYPE_DOUBLE, x_max_r, "y_min", G_TYPE_DOUBLE, y_min_r, "y_max",
                                                    G_TYPE_DOUBLE, y_max_r, "confidence", G_TYPE_DOUBLE, 1.0, NULL);
        // Atomic pairing per ROI: create ROI meta first, then add ODMtd; on ODMtd failure roll back ROI meta.
        GstVideoRegionOfInterestMeta *roi_meta =
            gst_buffer_add_video_region_of_interest_meta(buf, "motion", (guint)std::lround(_x), (guint)std::lround(_y),
                                                         (guint)std::lround(_w), (guint)std::lround(_h));
        if (!roi_meta) {
            GST_WARNING_OBJECT(self, "Failed to add ROI meta (Windows atomic) -> skipping ROI");
            gst_structure_free(detection);
            continue;
        }
        GstAnalyticsODMtd od_mtd;
        if (!gst_analytics_relation_meta_add_od_mtd(relation_meta, g_quark_from_string("motion"), (int)std::lround(_x),
                                                    (int)std::lround(_y), (int)std::lround(_w), (int)std::lround(_h),
                                                    1.0, &od_mtd)) {
            GST_WARNING_OBJECT(self, "Failed to add ODMtd (Windows atomic) -> rolling back ROI meta");
            gst_buffer_remove_meta(buf, (GstMeta *)roi_meta);
            gst_structure_free(detection);
            continue;
        }
        roi_meta->id = od_mtd.id;
        gst_video_region_of_interest_meta_add_param(roi_meta, detection);
        GST_LOG_OBJECT(self, "Attached motion ROI id=%d rect=[%d,%d %dx%d] (Windows atomic)", od_mtd.id,
                       (int)std::lround(_x), (int)std::lround(_y), (int)std::lround(_w), (int)std::lround(_h));
    }
    g_mutex_unlock(&self->meta_mutex);
}

// CPU path: map Y plane via GstVideoFrame and resize on host. Populates curr_small (GRAY).
// Returns TRUE on success.
static gboolean gst_gva_motion_detect_cpu_to_small_gray(GstGvaMotionDetect *self, GstBuffer *buf, int width, int height,
                                                        int small_w, int small_h, cv::UMat &curr_luma_out,
                                                        cv::UMat &curr_small) {
    GstVideoFrame vframe;
    if (!gst_video_frame_map(&vframe, &self->vinfo, buf, GST_MAP_READ))
        return FALSE;
    guint8 *y = (guint8 *)GST_VIDEO_FRAME_PLANE_DATA(&vframe, 0);
    int stride = GST_VIDEO_FRAME_PLANE_STRIDE(&vframe, 0);
    cv::Mat y_mat(height, width, CV_8UC1, y, stride);
    y_mat.copyTo(curr_luma_out);
    gst_video_frame_unmap(&vframe);
    cv::resize(curr_luma_out, curr_small, cv::Size(small_w, small_h));
    return TRUE;
}

static GstFlowReturn gst_gva_motion_detect_transform_ip(GstBaseTransform *t, GstBuffer *buf) {
    GstGvaMotionDetect *self = GST_GVA_MOTION_DETECT(t);
    ++self->frame_index;
    int width = GST_VIDEO_INFO_WIDTH(&self->vinfo);
    int height = GST_VIDEO_INFO_HEIGHT(&self->vinfo);
    if (!width || !height)
        return GST_FLOW_OK;

    int target_w = std::min(320, width);
    double scale = (double)target_w / (double)width;
    int small_w = target_w;
    int small_h = std::max(1, (int)std::lround(height * scale));

    // Lazy one-shot peer query for the D3D11 device if set_context hasn't fired yet.
    if (self->caps_is_d3d11 && !self->d3d11_device && !self->tried_d3d11_peer_query) {
        self->tried_d3d11_peer_query = TRUE;
        GstQuery *q = gst_query_new_context("gst.d3d11.device.handle");
        if (gst_pad_peer_query(GST_BASE_TRANSFORM_SINK_PAD(t), q)) {
            GstContext *ctx = nullptr;
            gst_query_parse_context(q, &ctx);
            if (ctx)
                gst_gva_motion_detect_set_context(GST_ELEMENT(self), ctx);
        }
        gst_query_unref(q);
    }

    cv::UMat curr_luma; // only populated on CPU path
    cv::UMat curr_small;

    gboolean small_ready = FALSE;
    if (self->caps_is_d3d11 && self->d3d11_device) {
        small_ready = gst_gva_motion_detect_d3d11_to_small_gray(self, buf, width, height, small_w, small_h, curr_small);
    }
    if (!small_ready) {
        if (!gst_gva_motion_detect_cpu_to_small_gray(self, buf, width, height, small_w, small_h, curr_luma, curr_small))
            return GST_FLOW_OK;
    }

    if (self->prev_small_gray.empty()) {
        curr_small.copyTo(self->prev_small_gray);
        if (!curr_luma.empty())
            curr_luma.copyTo(self->prev_luma);
        return GST_FLOW_OK;
    }
    // Build motion mask via helper (parity with Linux pipeline)
    cv::UMat morph;
    md_build_motion_mask(curr_small, self->prev_small_gray, morph, self->pixel_diff_threshold);
    std::vector<MotionRectWin> raw;
    md_scan_blocks(self, morph, width, height, small_w, small_h, raw);
    gst_gva_motion_detect_merge_rois(raw);
    // Tracking (parity with Linux logic)
    std::vector<char> matched(raw.size(), 0);
    for (auto &tr : self->tracks)
        tr.miss++;
    for (size_t i = 0; i < raw.size(); ++i) {
        auto &r = raw[i];
        double best = 0;
        int bi = -1;
        for (size_t j = 0; j < self->tracks.size(); ++j) {
            MotionRectWin tr{self->tracks[j].x, self->tracks[j].y, self->tracks[j].w, self->tracks[j].h};
            double iou = md_iou(r, tr);
            if (iou > best) {
                best = iou;
                bi = (int)j;
            }
        }
        if (bi >= 0 && best >= self->iou_threshold) {
            auto &tr = self->tracks[bi];
            tr.x = r.x;
            tr.y = r.y;
            tr.w = r.w;
            tr.h = r.h;
            double a = self->smooth_alpha;
            tr.sx = a * r.x + (1 - a) * tr.sx;
            tr.sy = a * r.y + (1 - a) * tr.sy;
            tr.sw = a * r.w + (1 - a) * tr.sw;
            tr.sh = a * r.h + (1 - a) * tr.sh;
            tr.age++;
            tr.miss = 0;
            matched[i] = 1;
        }
    }
    for (size_t i = 0; i < raw.size(); ++i)
        if (!matched[i]) {
            auto &r = raw[i];
            self->tracks.push_back({r.x, r.y, r.w, r.h, (double)r.x, (double)r.y, (double)r.w, (double)r.h, 1, 0});
        }
    // Remove stale tracks exceeding max_miss (Linux parity)
    if (self->max_miss >= 0) {
        self->tracks.erase(
            std::remove_if(self->tracks.begin(), self->tracks.end(),
                           [self](const _GstGvaMotionDetect::Track &tr) { return tr.miss > self->max_miss; }),
            self->tracks.end());
    }
    // Attach metadata now that tracks updated
    gst_gva_motion_detect_attach_metadata(self, buf, width, height);
    curr_small.copyTo(self->prev_small_gray);
    if (!curr_luma.empty())
        curr_luma.copyTo(self->prev_luma);
    return GST_FLOW_OK;
}

static void gst_gva_motion_detect_finalize(GObject *obj) {
    GstGvaMotionDetect *self = GST_GVA_MOTION_DETECT(obj);
    gst_gva_motion_detect_release_d3d11_small(self);
    if (self->d3d11_device) {
        gst_object_unref(self->d3d11_device);
        self->d3d11_device = nullptr;
    }
    g_mutex_clear(&self->meta_mutex);
    G_OBJECT_CLASS(gst_gva_motion_detect_parent_class)->finalize(obj);
}

static void gst_gva_motion_detect_class_init(GstGvaMotionDetectClass *klass) {
    GstElementClass *eclass = GST_ELEMENT_CLASS(klass);
    GstBaseTransformClass *bclass = GST_BASE_TRANSFORM_CLASS(klass);
    GObjectClass *oclass = G_OBJECT_CLASS(klass);
    GST_DEBUG_CATEGORY_INIT(gst_gva_motion_detect_debug_win, "gvamotiondetect", 0, "Motion detect (Windows)");
    gst_element_class_set_static_metadata(
        eclass, "Motion detect (auto GPU/CPU)", "Filter/Video",
        "Windows motion detection: D3D11 GPU path when negotiated, software otherwise", "dlstreamer");
    static GstStaticPadTemplate sink_templ =
        GST_STATIC_PAD_TEMPLATE("sink", GST_PAD_SINK, GST_PAD_ALWAYS,
                                GST_STATIC_CAPS("video/x-raw(memory:D3D11Memory), format=NV12; "
                                                "video/x-raw, format=NV12"));
    static GstStaticPadTemplate src_templ =
        GST_STATIC_PAD_TEMPLATE("src", GST_PAD_SRC, GST_PAD_ALWAYS,
                                GST_STATIC_CAPS("video/x-raw(memory:D3D11Memory), format=NV12; "
                                                "video/x-raw, format=NV12"));
    gst_element_class_add_static_pad_template(eclass, &sink_templ);
    gst_element_class_add_static_pad_template(eclass, &src_templ);
    eclass->set_context = gst_gva_motion_detect_set_context;
    bclass->start = gst_gva_motion_detect_start;
    bclass->set_caps = gst_gva_motion_detect_set_caps;
    bclass->transform_ip = gst_gva_motion_detect_transform_ip;
    oclass->finalize = gst_gva_motion_detect_finalize;
    oclass->set_property = gst_gva_motion_detect_set_property;
    oclass->get_property = gst_gva_motion_detect_get_property;
    g_object_class_install_property(
        oclass, PROP_BLOCK_SIZE,
        g_param_spec_int("block-size", "Block Size",
                         "Full-resolution block size (pixels) used for grid motion detection", 16, 512, 64,
                         G_PARAM_READWRITE));
    g_object_class_install_property(oclass, PROP_MOTION_THRESHOLD,
                                    g_param_spec_double("motion-threshold", "Motion Threshold",
                                                        "Per-block changed pixel ratio required to flag motion (0..1)",
                                                        0.0, 1.0, 0.05, G_PARAM_READWRITE));
    g_object_class_install_property(oclass, PROP_MIN_PERSISTENCE,
                                    g_param_spec_int("min-persistence", "Min Persistence",
                                                     "Frames an ROI must persist before being emitted", 1, 30, 2,
                                                     G_PARAM_READWRITE));
    g_object_class_install_property(oclass, PROP_MAX_MISS,
                                    g_param_spec_int("max-miss", "Max Miss",
                                                     "Grace frames after last match before ROI is dropped", 0, 30, 1,
                                                     G_PARAM_READWRITE));
    g_object_class_install_property(oclass, PROP_IOU_THRESHOLD,
                                    g_param_spec_double("iou-threshold", "IoU Threshold",
                                                        "IoU threshold for matching ROIs frame-to-frame (0..1)", 0.0,
                                                        1.0, 0.3, G_PARAM_READWRITE));
    g_object_class_install_property(oclass, PROP_SMOOTH_ALPHA,
                                    g_param_spec_double("smooth-alpha", "Smooth Alpha",
                                                        "EMA smoothing factor for ROI coordinates (0..1)", 0.0, 1.0,
                                                        0.5, G_PARAM_READWRITE));
    g_object_class_install_property(
        oclass, PROP_PIXEL_DIFF_THRESHOLD,
        g_param_spec_int(
            "pixel-diff-threshold", "Pixel Diff Threshold",
            "Per-pixel absolute luma difference used before blur+threshold (1..255). Lower = more sensitive", 1, 255,
            15, G_PARAM_READWRITE));
    g_object_class_install_property(
        oclass, PROP_CONFIRM_FRAMES,
        g_param_spec_int("confirm-frames", "Confirm Frames",
                         "Consecutive frames required to confirm motion block (1=single-frame immediate)", 1, 10, 1,
                         G_PARAM_READWRITE));
    g_object_class_install_property(oclass, PROP_MIN_REL_AREA,
                                    g_param_spec_double("min-rel-area", "Min Relative Area",
                                                        "Minimum relative frame area (0..0.25) required for a motion "
                                                        "rectangle before merging/tracking (filters tiny noise boxes)",
                                                        0.0, 0.25, 0.0005, G_PARAM_READWRITE));
}

static void gst_gva_motion_detect_init(GstGvaMotionDetect *self) {
    self->block_size = 64;
    self->motion_threshold = 0.05;
    self->min_persistence = 2;
    self->max_miss = 1;
    self->iou_threshold = 0.3;
    self->smooth_alpha = 0.5;
    self->pixel_diff_threshold = 15;
    self->confirm_frames = 1;    // Linux parity: immediate single-frame confirmation
    self->min_rel_area = 0.0005; // default minimum relative area (0.05% of frame)
    self->frame_index = 0;
    self->caps_is_d3d11 = FALSE;
    self->d3d11_device = nullptr;
    self->d3d11_conv = nullptr;
    self->small_buf = nullptr;
    self->small_w = 0;
    self->small_h = 0;
    self->cv_ocl_ready = FALSE;
    self->tried_d3d11_peer_query = FALSE;
    g_mutex_init(&self->meta_mutex);
}
