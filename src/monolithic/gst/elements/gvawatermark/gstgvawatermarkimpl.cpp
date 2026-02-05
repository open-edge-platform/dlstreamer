/*******************************************************************************
 * Copyright (C) 2018-2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#include "gstgvawatermarkimpl.h"
#include "gvawatermarkcaps.h"

#ifndef _WIN32
#include <dlstreamer/image_info.h>
#include <gmodule.h>
#endif

#include <gst/allocators/gstdmabuf.h>
#include <gst/base/gstbasetransform.h>
#include <gst/gst.h>
#include <gst/video/video-color.h>
#include <gst/video/video-info.h>

#include <inference_backend/logger.h>
#include <safe_arithmetic.hpp>

#include "gva_caps.h"
#include "inference_backend/buffer_mapper.h"
#include "so_loader.h"
#include "utils.h"
#include "video_frame.h"

#ifndef _WIN32
#ifdef ENABLE_VAAPI
#include <dlstreamer/vaapi/mappers/vaapi_to_dma.h>
#endif
#else
#include <dlstreamer/gst/mappers/gst_to_d3d11.h>
#endif

#include "renderer/color_converter.h"
#include "renderer/cpu/create_renderer.h"

#include <exception>
#include <string>
#include <typeinfo>
#if !defined(ENABLE_VAAPI) || defined(_WIN32)
// VAAPI disabled: provide minimal stub types/constants to satisfy signatures without libva.
#ifndef VA_INVALID_SURFACE
#define VA_INVALID_SURFACE (-1)
#endif
typedef int VASurfaceID; // simple integral placeholder
typedef void *VADisplay; // opaque pointer placeholder
#endif

#define ELEMENT_LONG_NAME "Implementation for detection/classification/recognition results labeling"
#define ELEMENT_DESCRIPTION "Implements gstgvawatermark element functionality."

GST_DEBUG_CATEGORY_STATIC(gst_gva_watermark_impl_debug_category);
#define GST_CAT_DEFAULT gst_gva_watermark_impl_debug_category

#define DEFAULT_DEVICE nullptr
#define DEFAULT_THICKNESS (2)
#define DEFAULT_TEXT_SCALE (1.0)
#define DEFAULT_COLOR_IDX (-1)

typedef enum { DEVICE_CPU, DEVICE_GPU, DEVICE_GPU_AUTOSELECTED } DEVICE_SELECTOR;

namespace {

const std::vector<Color> color_table = {Color(255, 0, 0),   Color(0, 255, 0),   Color(0, 0, 255),   Color(255, 255, 0),
                                        Color(0, 255, 255), Color(255, 0, 255), Color(255, 170, 0), Color(255, 0, 170),
                                        Color(0, 255, 170), Color(170, 255, 0), Color(170, 0, 255), Color(0, 170, 255),
                                        Color(255, 85, 0),  Color(85, 255, 0),  Color(0, 255, 85),  Color(0, 85, 255),
                                        Color(85, 0, 255),  Color(255, 0, 85)};

Color indexToColor(size_t index) {
    return color_table[index % color_table.size()];
}

void clip_rect(double &x, double &y, double &w, double &h, GstVideoInfo *info) {
    x = (x < 0) ? 0 : (x > info->width) ? (info->width - 1) : x;
    y = (y < 0) ? 0 : (y > info->height) ? (info->height - 1) : y;
    w = (w < 0) ? 0 : (x + w > info->width) ? (info->width - 1) - x : w;
    h = (h < 0) ? 0 : (y + h > info->height) ? (info->height - 1) - y : h;
}

void appendStr(std::ostringstream &oss, const std::string &s, char delim = ' ') {
    if (!s.empty()) {
        oss << s << delim;
    }
}

InferenceBackend::MemoryType memoryTypeFromCaps(GstCaps *caps) {
    const auto caps_feature = get_caps_feature(caps);
    switch (caps_feature) {
    case SYSTEM_MEMORY_CAPS_FEATURE:
        return InferenceBackend::MemoryType::SYSTEM;

    case D3D11_MEMORY_CAPS_FEATURE:
        return InferenceBackend::MemoryType::D3D11;

    case VA_SURFACE_CAPS_FEATURE:
    case VA_MEMORY_CAPS_FEATURE:
        return InferenceBackend::MemoryType::VAAPI;

    case DMA_BUF_CAPS_FEATURE:
        return InferenceBackend::MemoryType::DMA_BUFFER;

    default:
        GST_ERROR("Unknown memory caps property: '%s'", gst_caps_to_string(caps));
        return InferenceBackend::MemoryType::ANY;
    }
}

} // namespace

struct Impl {
    Impl(GstVideoInfo *info, InferenceBackend::MemoryType mem_type, GstElement *element, bool displ_avgfps,
         gchar *displ_cfg);
    bool extract_primitives(GstBuffer *buffer);
    int get_num_primitives() const;
    bool render(GstBuffer *buffer);
    bool render_va(cv::Mat *buffer);
    const std::string &getBackendType() const {
        return _backend_type;
    }

  private:
    void preparePrimsForRoi(GVA::RegionOfInterest &roi, std::vector<render::Prim> &prims) const;
    void preparePrimsForTensor(const GVA::Tensor &tensor, GVA::Rect<double> rect, std::vector<render::Prim> &prims,
                               size_t color_index = 0) const;
    void preparePrimsForKeypoints(const GVA::Tensor &tensor, GVA::Rect<double> rectangle,
                                  std::vector<render::Prim> &prims) const;
    void preparePrimsForKeypointConnections(GstStructure *s, const std::vector<float> &keypoints_data,
                                            const std::vector<uint32_t> &dims, const std::vector<float> &confidence,
                                            const GVA::Rect<double> &rectangle, std::vector<render::Prim> &prims) const;
    void find_gvafpscounter_element();
    void parse_displ_config();
    std::unique_ptr<Renderer> createRenderer(std::shared_ptr<ColorConverter> converter);

    std::unique_ptr<Renderer> createGPURenderer(dlstreamer::ImageFormat format,
                                                std::shared_ptr<ColorConverter> converter,
                                                InferenceBackend::MemoryType mem_type,
                                                dlstreamer::ContextPtr vaapi_context);

    std::unique_ptr<Renderer> createOpenCVRenderer(std::shared_ptr<ColorConverter> converter);

    GstVideoInfo *_vinfo;
    GstElement *_element;
    GstElement *_gvafpscounter_element = nullptr;
    std::string _backend_type;
    InferenceBackend::MemoryType _mem_type;

    SharedObject::Ptr _gpurenderer_loader;
    std::unique_ptr<Renderer> _renderer;
    std::unique_ptr<Renderer> _renderer_opencv;

    std::vector<render::Prim> prims;

    const double _radius_multiplier = 0.0025;
    Color _default_color = indexToColor(1);
    // Position for full-frame text
    const cv::Point2f _ff_text_position = cv::Point2f(0, 25.f);
    struct FontCfg {
        const int type = cv::FONT_HERSHEY_TRIPLEX;
        const double scale = 1.0;
    } _font;
    const bool _obb = false;
    bool _displ_avgfps = false;
    gchar *_displ_cfg = nullptr;

    struct DisplCfg {
        bool show_labels = true;
        bool draw_text_background = false;
        int color_idx = DEFAULT_COLOR_IDX;
        uint thickness = DEFAULT_THICKNESS;
        double text_scale = DEFAULT_TEXT_SCALE;
    } _displCfg;
};

G_DEFINE_TYPE_WITH_CODE(GstGvaWatermarkImpl, gst_gva_watermark_impl, GST_TYPE_BASE_TRANSFORM,
                        GST_DEBUG_CATEGORY_INIT(gst_gva_watermark_impl_debug_category, "gvawatermarkimpl", 0,
                                                "debug category for gvawatermark element"));

static void gst_gva_watermark_impl_init(GstGvaWatermarkImpl *gvawatermark) {
    gvawatermark->device = DEFAULT_DEVICE;
    gvawatermark->obb = false;
    gvawatermark->displ_avgfps = false;
    gvawatermark->displ_cfg = nullptr;
}

void gst_gva_watermark_impl_set_property(GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec) {
    GstGvaWatermarkImpl *gvawatermark = GST_GVA_WATERMARK_IMPL(object);

    GST_DEBUG_OBJECT(gvawatermark, "set_property");

    switch (prop_id) {
    case PROP_DEVICE:
        g_free(gvawatermark->device);
        gvawatermark->device = g_value_dup_string(value);
        break;
    case PROP_OBB:
        gvawatermark->obb = g_value_get_boolean(value);
        break;
    case PROP_DISPL_AVGFPS:
        gvawatermark->displ_avgfps = g_value_get_boolean(value);
        break;
    case PROP_DISPL_CFG:
        g_free(gvawatermark->displ_cfg);
        gvawatermark->displ_cfg = g_value_dup_string(value);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
        break;
    }
}

void gst_gva_watermark_impl_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec) {
    GstGvaWatermarkImpl *self = GST_GVA_WATERMARK_IMPL(object);

    GST_DEBUG_OBJECT(self, "get_property");

    switch (prop_id) {
    case PROP_DEVICE:
        if (self->impl) {
            g_value_set_string(value, self->impl->getBackendType().c_str());
        } else {
            g_value_set_string(value, self->device);
        }
        break;
    case PROP_OBB:
        g_value_set_boolean(value, self->obb);
        break;
    case PROP_DISPL_AVGFPS:
        g_value_set_boolean(value, self->displ_avgfps);
        break;
    case PROP_DISPL_CFG:
        g_value_set_string(value, self->displ_cfg);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
        break;
    }
}

void gst_gva_watermark_impl_dispose(GObject *object) {
    GstGvaWatermarkImpl *gvawatermark = GST_GVA_WATERMARK_IMPL(object);

    GST_DEBUG_OBJECT(gvawatermark, "dispose");

    /* clean up as possible.  may be called multiple times */

    G_OBJECT_CLASS(gst_gva_watermark_impl_parent_class)->dispose(object);
}

void gst_gva_watermark_impl_finalize(GObject *object) {
    GstGvaWatermarkImpl *gvawatermark = GST_GVA_WATERMARK_IMPL(object);

    GST_DEBUG_OBJECT(gvawatermark, "finalize");

    gvawatermark->impl.reset();

    g_free(gvawatermark->device);
    gvawatermark->device = nullptr;

    G_OBJECT_CLASS(gst_gva_watermark_impl_parent_class)->finalize(object);
}

static gboolean gst_gva_watermark_impl_start(GstBaseTransform *trans) {
    auto *self = GST_GVA_WATERMARK_IMPL(trans);
    GST_DEBUG_OBJECT(self, "start");
#ifdef ENABLE_VAAPI
    // Always request VA display early (idempotent)
    gst_element_post_message(GST_ELEMENT(self),
                             gst_message_new_need_context(GST_OBJECT(self), "gst.va.display.handle"));
#endif
    GST_INFO_OBJECT(self, "%s parameters:\n -- Device: %s\n", GST_ELEMENT_NAME(GST_ELEMENT_CAST(self)), self->device);
    return TRUE;
}

static gboolean gst_gva_watermark_impl_stop(GstBaseTransform *trans) {
    GstGvaWatermarkImpl *gvawatermark = GST_GVA_WATERMARK_IMPL(trans);

    GST_DEBUG_OBJECT(gvawatermark, "stop");

    return true;
}

static gboolean gst_gva_watermark_impl_set_caps(GstBaseTransform *trans, GstCaps *incaps, GstCaps *outcaps) {
    using namespace InferenceBackend;
    UNUSED(outcaps);

    GstGvaWatermarkImpl *gvawatermark = GST_GVA_WATERMARK_IMPL(trans);

    GST_DEBUG_OBJECT(gvawatermark, "set_caps");

    gst_video_info_from_caps(&gvawatermark->info, incaps);
    const auto mem_type = memoryTypeFromCaps(incaps);
    gvawatermark->negotiated_mem_type = mem_type;

    if (!gvawatermark->device) {
        switch (mem_type) {
        // For now use CPU renderer for d3d11
        case MemoryType::D3D11:
        case MemoryType::SYSTEM:
            break;
        case MemoryType::VAAPI:
        case MemoryType::DMA_BUFFER:
            break;
        default:
            GST_ERROR_OBJECT(gvawatermark, "Unsupported memory type: %d", static_cast<int>(mem_type));
            return false;
        }
    } else {
        if (std::string(gvawatermark->device) == "GPU") {
            if (get_caps_feature(incaps) == SYSTEM_MEMORY_CAPS_FEATURE) {
                GST_ELEMENT_ERROR(
                    gvawatermark, CORE, FAILED,
                    ("Device %s is incompatible with System Memory type."
                     " Please, set CPU device or use another type of memory in a pipeline (VASurface or DMABuf).",
                     gvawatermark->device),
                    (NULL));
                return false;
            }
        } else if (std::string(gvawatermark->device) == "CPU") {
        } else {
            GST_ELEMENT_ERROR(gvawatermark, CORE, FAILED, ("Unsupported 'device' property name"),
                              ("Device with %s name is not supported in the gvawatermark", gvawatermark->device));
            return false;
        }
    }

    gvawatermark->impl.reset();

#if defined(ENABLE_VAAPI) && !defined(_WIN32)
    VaApiDisplayPtr va_dpy;
    if (mem_type == MemoryType::VAAPI) {
        // Prefer context obtained via set_context
        if (gvawatermark->vaapi_ctx) {
            va_dpy = gvawatermark->vaapi_ctx;
        } else {
            try {
                va_dpy = std::make_shared<dlstreamer::GSTContextQuery>(
                    trans, (get_caps_feature(incaps) == VA_MEMORY_CAPS_FEATURE) ? dlstreamer::MemoryType::VA
                                                                                : dlstreamer::MemoryType::VAAPI);
            } catch (const std::exception &e) {
                GST_ELEMENT_ERROR(gvawatermark, CORE, FAILED, ("Could not create VAAPI context"),
                                  ("Cannot create watermark instance. %s", Utils::createNestedErrorMsg(e).c_str()));
            }
        }
    }
#endif

    try {
        gvawatermark->impl = std::make_shared<Impl>(&gvawatermark->info, mem_type, GST_ELEMENT(trans),
                                                    gvawatermark->displ_avgfps, gvawatermark->displ_cfg);
    } catch (const std::exception &e) {
        GST_ELEMENT_ERROR(gvawatermark, CORE, FAILED, ("Could not initialize"),
                          ("Cannot create watermark instance. %s", Utils::createNestedErrorMsg(e).c_str()));
    }

    if (!gvawatermark->impl)
        return false;

    GST_INFO_OBJECT(gvawatermark, "Watermark configuration:");
    GST_INFO_OBJECT(gvawatermark, "device: %s", gvawatermark->impl->getBackendType().c_str());

    return true;
}

// Resolve VA display only when VAAPI enabled (Linux path); otherwise stub.
#ifdef ENABLE_VAAPI
#ifndef _WIN32
static VADisplay resolve_va_display_from_gst_display(GstObject *gst_display_obj) {
    if (!gst_display_obj)
        return nullptr;
    static GModule *mod = nullptr;
    if (!mod) {
        const char *sons[] = {"libgstva-1.0.so.0", "libgstva-1.0.so"};
        for (auto son : sons) {
            mod = g_module_open(son, G_MODULE_BIND_LAZY);
            if (mod)
                break;
        }
    }
    if (!mod)
        return nullptr;
    using GetVaFn = VADisplay (*)(gpointer);
    GetVaFn get_va = nullptr;
    const char *syms[] = {"gst_va_display_get_va_display", "gst_va_display_get_va_dpy"};
    for (auto s : syms) {
        if (g_module_symbol(mod, s, (gpointer *)&get_va) && get_va)
            break;
    }
    return get_va ? get_va((gpointer)gst_display_obj) : nullptr;
}
#else
static VADisplay resolve_va_display_from_gst_display(GstObject *) {
    return nullptr;
}
#endif
#else
static VADisplay resolve_va_display_from_gst_display(GstObject *) {
    return nullptr;
}
#endif

// Grab VADisplay from GstContext (handles both pointer and object cases)
static void gst_gva_watermark_impl_set_context(GstElement *elem, GstContext *context) {
    auto *self = GST_GVA_WATERMARK_IMPL(elem);
    const gchar *ctx_type = gst_context_get_context_type(context);
    const GstStructure *s = gst_context_get_structure(context);

#if defined(ENABLE_VAAPI) && !defined(_WIN32)
    if (!self->gst_ctx)
        self->gst_ctx = std::make_shared<dlstreamer::GSTContext>(GST_ELEMENT(self));

    if (!self->va_dpy && g_strcmp0(ctx_type, "gst.va.display.handle") == 0) {
        if (gst_structure_has_field(s, "va-display")) {
            self->va_dpy = (VADisplay)g_value_get_pointer(gst_structure_get_value(s, "va-display"));
            GST_INFO_OBJECT(self, "Acquired VADisplay pointer: %p", self->va_dpy);
        } else if (gst_structure_has_field(s, "gst-display")) {
            GstObject *gst_disp = nullptr;
            if (!gst_structure_get(s, "gst-display", GST_TYPE_OBJECT, &gst_disp, NULL) || !gst_disp) {
                GST_WARNING_OBJECT(self, "Failed to retrieve 'gst-display' from GstContext structure");
            } else {
                self->va_dpy = resolve_va_display_from_gst_display(gst_disp);
                GST_INFO_OBJECT(self, "Acquired VADisplay from GstVaDisplay: %p", self->va_dpy);
                gst_object_unref(gst_disp);
            }
        }
    }

    if (self->va_dpy && (!self->vaapi_ctx || !self->gst_to_vaapi)) {
        self->vaapi_ctx = std::make_shared<dlstreamer::VAAPIContext>(self->va_dpy);
        self->gst_to_vaapi = std::make_shared<dlstreamer::MemoryMapperGSTToVAAPI>(self->gst_ctx, self->vaapi_ctx);
        GST_INFO_OBJECT(self, "Initialized VAAPI context and GST->VAAPI mapper");

        static bool ocl_ctx_inited = false;
        if (!ocl_ctx_inited && !g_getenv("VA_GPU_DISABLE_VA_OCL_INIT")) {
            try {
                cv::va_intel::ocl::initializeContextFromVA(self->va_dpy, /*interop*/ true);
                GST_INFO_OBJECT(self, "OpenCV VA/OpenCL context initialized (zero-copy requested)");
                ocl_ctx_inited = true;

                if (cv::ocl::useOpenCL()) {
                    cv::ocl::Device dev = cv::ocl::Device::getDefault();
#if defined(CV_VERSION_MAJOR)
                    GST_INFO_OBJECT(self, "OpenCL device: %s (vendor=%s version=%s driver=%s)", dev.name().c_str(),
                                    dev.vendorName().c_str(), dev.version().c_str(), dev.driverVersion().c_str());
#else
                    GST_INFO_OBJECT(self, "OpenCL device: %s (vendorID=%d version=%s)", dev.name().c_str(),
                                    dev.vendorID(), dev.version().c_str());
#endif
                } else {
                    GST_WARNING_OBJECT(self, "OpenCL not active after initializeContextFromVA");
                }
            } catch (const cv::Exception &e) {
                GST_WARNING_OBJECT(self, "initializeContextFromVA failed: %s (will use fallback copies if any)",
                                   e.what());
            }
        }
    }
#endif // ENABLE_VAAPI && !_WIN32

    GST_ELEMENT_CLASS(gst_gva_watermark_impl_parent_class)->set_context(elem, context);
}

static bool buffer_has_va(GstBuffer *buf) {
    if (!buf)
        return false;
    guint n = gst_buffer_n_memory(buf);
    for (guint i = 0; i < n; ++i) {
        GstMemory *m = gst_buffer_peek_memory(buf, i);
        if (!m)
            continue;
        if (gst_is_dmabuf_memory(m))
            return true;
        if (gst_memory_is_type(m, "VAMemory"))
            return true;
    }
    return false;
}

// Minimal fallback: resolve VASurfaceID from GstVA at runtime (no unstable headers)
#ifdef ENABLE_VAAPI
#ifdef _WIN32
static VASurfaceID get_surface_from_buffer(GstBuffer *) {
    return VA_INVALID_SURFACE;
}
#else
static VASurfaceID get_surface_from_buffer(GstBuffer *buf) {
    if (!buf)
        return VA_INVALID_SURFACE;

    static GModule *mod = nullptr;
    if (!mod) {
        const char *sons[] = {"libgstva-1.0.so.0", "libgstva-1.0.so"};
        for (auto son : sons) {
            mod = g_module_open(son, G_MODULE_BIND_LAZY);
            if (mod)
                break;
        }
    }
    if (!mod)
        return VA_INVALID_SURFACE;

    using GetSurfFromBuf = VASurfaceID (*)(GstBuffer *);
    using GetSurfFromMem = VASurfaceID (*)(GstMemory *);

    GetSurfFromBuf get_from_buf = nullptr;
    GetSurfFromMem get_from_mem = nullptr;
    g_module_symbol(mod, "gst_va_buffer_get_surface", (gpointer *)&get_from_buf);
    g_module_symbol(mod, "gst_va_memory_get_surface", (gpointer *)&get_from_mem);

    if (get_from_buf) {
        VASurfaceID s = get_from_buf(buf);
        if (s != VA_INVALID_SURFACE)
            return s;
    }
    if (get_from_mem) {
        guint n = gst_buffer_n_memory(buf);
        for (guint i = 0; i < n; ++i) {
            GstMemory *m = gst_buffer_peek_memory(buf, i);
            if (m && gst_memory_is_type(m, "VAMemory")) {
                VASurfaceID s = get_from_mem(m);
                if (s != VA_INVALID_SURFACE)
                    return s;
            }
        }
    }
    return VA_INVALID_SURFACE;
}
#endif
#else
static VASurfaceID get_surface_from_buffer(GstBuffer *) {
    return VA_INVALID_SURFACE;
}
#endif

static GstFlowReturn gst_gva_watermark_impl_transform_ip(GstBaseTransform *trans, GstBuffer *buf) {
    GstGvaWatermarkImpl *gvawatermark = GST_GVA_WATERMARK_IMPL(trans);

    GST_DEBUG_OBJECT(gvawatermark, "transform_ip");

    if (!gst_pad_is_linked(GST_BASE_TRANSFORM_SRC_PAD(trans))) {
        return GST_BASE_TRANSFORM_FLOW_DROPPED;
    }

    InferenceBackend::MemoryType mt = gvawatermark->negotiated_mem_type;
    bool negotiated_is_va =
        (mt == InferenceBackend::MemoryType::VAAPI || mt == InferenceBackend::MemoryType::DMA_BUFFER);
    bool buffer_is_va_like = negotiated_is_va && buffer_has_va(buf);
#if defined(ENABLE_VAAPI) && !defined(_WIN32)
    bool have_va_context = (gvawatermark->vaapi_ctx || gvawatermark->va_dpy);
#else
    bool have_va_context = false;
#endif
    bool force_cpu = (gvawatermark->device && g_strcmp0(gvawatermark->device, "CPU") == 0);
    bool use_gpu_path = have_va_context && buffer_is_va_like && !force_cpu;
// Disable GPU/VA render path when VAAPI feature disabled at build time.
#ifndef ENABLE_VAAPI
    use_gpu_path = false;
#endif

    try {
        if (!gvawatermark->impl)
            throw std::invalid_argument("Watermark is not set");

        gvawatermark->impl->extract_primitives(buf);

        // return if there is nothing to render
        if (gvawatermark->impl->get_num_primitives() == 0) {
            GST_DEBUG_OBJECT(gvawatermark, "No primitives to render");
            return GST_FLOW_OK;
        }

// VA/GPU render path only when VAAPI enabled
#if defined(ENABLE_VAAPI) && !defined(_WIN32)
        if (use_gpu_path) {
            GST_TRACE_OBJECT(gvawatermark, "Using VA/GPU render path");

            VASurfaceID sid = VA_INVALID_SURFACE;
            sid = get_surface_from_buffer(buf);
            if (sid == VA_INVALID_SURFACE) {
                GST_WARNING_OBJECT(gvawatermark,
                                   "Mapped frame is not VAAPIFrame and GstVA fallback failed; pass-through");
                use_gpu_path = false;
            } else {
                GST_INFO_OBJECT(gvawatermark, "Using GstVA, VASurfaceID=%d", (int)sid);
                // At this point you have valid VADisplay (self->va_dpy) and VASurfaceID (sid)
                GST_LOG_OBJECT(gvawatermark, "Got VADisplay=%p, VASurfaceID=%d", gvawatermark->va_dpy, (int)sid);

                const int width = GST_VIDEO_INFO_WIDTH(&gvawatermark->info);
                const int height = GST_VIDEO_INFO_HEIGHT(&gvawatermark->info);

                if (!gvawatermark->overlay_ready || gvawatermark->overlay_cpu.empty() ||
                    gvawatermark->overlay_cpu.cols != width || gvawatermark->overlay_cpu.rows != height ||
                    gvawatermark->overlay_cpu.type() != CV_8UC3) {

                    gvawatermark->overlay_cpu.create(height, width, CV_8UC3);
                    gvawatermark->overlay_ready = true;
                    GST_INFO_OBJECT(gvawatermark, "Allocated CPU overlay (%dx%d CV_8UC4)", width, height);
                }

                // Clear only once per frame (fast memset on host)
                gvawatermark->overlay_cpu.setTo(cv::Scalar(0, 0, 0, 0));

                cv::UMat u;
                cv::va_intel::convertFromVASurface(gvawatermark->va_dpy, sid, cv::Size(width, height), u);

                gvawatermark->impl->render_va(&(gvawatermark->overlay_cpu));

                // Upload once (host -> UMat). OpenCL runtime can keep it on device afterward.
                gvawatermark->overlay_cpu.copyTo(gvawatermark->overlay_gpu);

                if (cv::ocl::useOpenCL()) {
                    cv::UMat gray, mask;
                    cv::cvtColor(gvawatermark->overlay_gpu, gray, cv::COLOR_BGR2GRAY);
                    cv::threshold(gray, mask, 0, 255, cv::THRESH_BINARY);
                    gvawatermark->overlay_gpu.copyTo(u, mask);

                    cv::va_intel::convertToVASurface(gvawatermark->va_dpy, u, sid, cv::Size(width, height));
                }
            }
        }
#endif // ENABLE_VAAPI

        if (!use_gpu_path) {
            GST_TRACE_OBJECT(gvawatermark, "Using CPU/System render path");
            gvawatermark->impl->render(buf);
        }
    } catch (const std::exception &e) {
        const std::string msg = Utils::createNestedErrorMsg(e);
        GST_ELEMENT_ERROR(gvawatermark, STREAM, FAILED, ("gvawatermark has failed to process frame."),
                          ("%s", msg.c_str()));
        return GST_FLOW_ERROR;
    }

    return GST_FLOW_OK;
}

gboolean gst_gva_watermark_impl_propose_allocation(GstBaseTransform *trans, GstQuery *decide_query, GstQuery *query) {
    UNUSED(decide_query);
    UNUSED(trans);
    if (query) {
        gst_query_add_allocation_meta(query, GST_VIDEO_META_API_TYPE, nullptr);
        return true;
    }
    return false;
}

static void gst_gva_watermark_impl_class_init(GstGvaWatermarkImplClass *klass) {
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
    GstBaseTransformClass *base_transform_class = GST_BASE_TRANSFORM_CLASS(klass);
    GstElementClass *element_class = GST_ELEMENT_CLASS(klass);

    /* Setting up pads and setting metadata should be moved to
       base_class_init if you intend to subclass this class. */
    gst_element_class_add_pad_template(element_class, gst_pad_template_new("src", GST_PAD_SRC, GST_PAD_ALWAYS,
                                                                           gst_caps_from_string(WATERMARK_ALL_CAPS)));
    gst_element_class_add_pad_template(element_class, gst_pad_template_new("sink", GST_PAD_SINK, GST_PAD_ALWAYS,
                                                                           gst_caps_from_string(WATERMARK_ALL_CAPS)));

    gst_element_class_set_static_metadata(element_class, ELEMENT_LONG_NAME, "Video", ELEMENT_DESCRIPTION,
                                          "Intel Corporation");

    element_class->set_context = GST_DEBUG_FUNCPTR(gst_gva_watermark_impl_set_context);

    gobject_class->set_property = gst_gva_watermark_impl_set_property;
    gobject_class->get_property = gst_gva_watermark_impl_get_property;
    gobject_class->dispose = gst_gva_watermark_impl_dispose;
    gobject_class->finalize = gst_gva_watermark_impl_finalize;
    base_transform_class->start = GST_DEBUG_FUNCPTR(gst_gva_watermark_impl_start);
    base_transform_class->stop = GST_DEBUG_FUNCPTR(gst_gva_watermark_impl_stop);
    base_transform_class->set_caps = GST_DEBUG_FUNCPTR(gst_gva_watermark_impl_set_caps);
    base_transform_class->transform = nullptr;
    base_transform_class->transform_ip = GST_DEBUG_FUNCPTR(gst_gva_watermark_impl_transform_ip);
    base_transform_class->propose_allocation = GST_DEBUG_FUNCPTR(gst_gva_watermark_impl_propose_allocation);

    const auto kDefaultGParamFlags =
        static_cast<GParamFlags>(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_CONSTRUCT);

    g_object_class_install_property(
        gobject_class, PROP_DEVICE,
        g_param_spec_string(
            "device", "Target device",
            "Supported devices are CPU and GPU. Default is CPU on system memory and GPU on video memory",
            DEFAULT_DEVICE, kDefaultGParamFlags));

    g_object_class_install_property(gobject_class, PROP_OBB,
                                    g_param_spec_boolean("obb", "Oriented Bounding Box",
                                                         "If true, draw oriented bounding box instead of object mask",
                                                         false, kDefaultGParamFlags));

    g_object_class_install_property(gobject_class, PROP_DISPL_AVGFPS,
                                    g_param_spec_boolean("displ-avgfps", "Display Average FPS",
                                                         DISPL_AVGFPS_DESCRIPTION, false, kDefaultGParamFlags));

    g_object_class_install_property(gobject_class, PROP_DISPL_CFG,
                                    g_param_spec_string("displ-cfg", "Gvawatermark element display configuration",
                                                        DISPL_CFG_DESCRIPTION, nullptr, kDefaultGParamFlags));
}

Impl::Impl(GstVideoInfo *info, InferenceBackend::MemoryType mem_type, GstElement *element, bool displ_avgfps,
           gchar *displ_cfg)
    : _vinfo(info), _element(element), _mem_type(mem_type), _displ_avgfps(displ_avgfps), _displ_cfg(displ_cfg) {
    assert(_vinfo);
    if (GST_VIDEO_INFO_COLORIMETRY(_vinfo).matrix == GstVideoColorMatrix::GST_VIDEO_COLOR_MATRIX_UNKNOWN)
        throw std::runtime_error("GST_VIDEO_COLOR_MATRIX_UNKNOWN");

    double Kb = 0, Kr = 0;
    GstVideoColorimetry colorimetry = GST_VIDEO_INFO_COLORIMETRY(_vinfo);
    gst_video_color_matrix_get_Kr_Kb(colorimetry.matrix, &Kr, &Kb);

    dlstreamer::ImageFormat format = dlstreamer::gst_format_to_video_format(GST_VIDEO_INFO_FORMAT(_vinfo));
    std::shared_ptr<ColorConverter> converter = create_color_converter(format, color_table, Kr, Kb);
    std::shared_ptr<ColorConverter> converterBGR =
        create_color_converter(dlstreamer::ImageFormat::BGR, color_table, Kr, Kb);

    _renderer = createRenderer(std::move(converter));
    _renderer_opencv = createOpenCVRenderer(std::move(converterBGR));

    // Find gvafpscounter element in the pipeline to put avg-fps on output video
    if (_displ_avgfps)
        find_gvafpscounter_element();

    // Parse display configuration
    if (_displ_cfg) {
        parse_displ_config();
        if (_displCfg.draw_text_background) {
            _renderer->enable_draw_txt_bg(true);
            _renderer_opencv->enable_draw_txt_bg(true);
        }
    }
}

size_t get_keypoint_index_by_name(const gchar *target_name, GValueArray *names) {
    if (names == nullptr or target_name == nullptr) {
        throw std::invalid_argument("get_keypoint_index_by_name: Got nullptrs.");
    }

    for (size_t i = 0; i < names->n_values; ++i) {
        const gchar *name = g_value_get_string(names->values + i);
        if (g_strcmp0(name, target_name) == 0) {
            return i;
        }
    }

    return names->n_values;
}

void Impl::find_gvafpscounter_element() {

    if (!_element)
        return;

    GstObject *parent = GST_OBJECT_PARENT(GST_ELEMENT(_element));
    GstElement *pipeline = nullptr;
    // Traverse up the hierarchy to find the pipeline
    while (parent && !GST_IS_PIPELINE(parent)) {
        parent = GST_OBJECT_PARENT(parent);
    }
    if (parent && GST_IS_PIPELINE(parent)) {
        pipeline = GST_ELEMENT(parent);
    }
    if (pipeline) {
        GstIterator *it = gst_bin_iterate_elements(GST_BIN(pipeline));
        GValue value = G_VALUE_INIT;
        gboolean done = FALSE;
        while (!done) {
            switch (gst_iterator_next(it, &value)) {
            case GST_ITERATOR_OK: {
                GstElement *element = GST_ELEMENT(g_value_get_object(&value));
                const gchar *factory_name = GST_OBJECT_NAME(gst_element_get_factory(element));
                if (g_str_has_prefix(factory_name, "gvafpscounter")) {
                    _gvafpscounter_element = element;
                    // No need to continue searching
                    done = TRUE;
                }
                g_value_reset(&value);
                break;
            }
            case GST_ITERATOR_RESYNC:
                gst_iterator_resync(it);
                break;
            case GST_ITERATOR_ERROR:
            case GST_ITERATOR_DONE:
                done = TRUE;
                break;
            }
        }
        g_value_unset(&value);
        gst_iterator_free(it);
    }

    if (_gvafpscounter_element == nullptr) {
        GST_WARNING_OBJECT(
            _element, "displ-avgfps property is set to true, but gvafpscounter element is not found in the pipeline. "
                      "Average FPS will not be displayed on output video.");
        _displ_avgfps = false;
    }
}

bool Impl::extract_primitives(GstBuffer *buffer) {
    ITT_TASK(__FUNCTION__);

    GVA::VideoFrame video_frame(buffer, _vinfo);
    auto video_frame_rois = video_frame.regions();

    prims.clear();
    prims.reserve(video_frame_rois.size());
    // Prepare primitives for all ROIs
    for (auto &roi : video_frame_rois) {
        preparePrimsForRoi(roi, prims);
    }

    // Tensor metas, attached to the frame, should be related to full-frame inference
    const GVA::Rect<double> ff_rect{0, 0, safe_convert<double>(_vinfo->width), safe_convert<double>(_vinfo->height)};
    std::ostringstream ff_text;

    for (auto &tensor : video_frame.tensors()) {
        if (tensor.is_detection())
            continue;
        preparePrimsForTensor(tensor, ff_rect, prims);
        if (tensor.label().size() > 1) {
            appendStr(ff_text, tensor.label());
            if (tensor.confidence() > 0.0) {
                ff_text << int(tensor.confidence() * 100) << "%";
            }
        }
    }

    if (ff_text.tellp() != 0)
        prims.emplace_back(render::Text(ff_text.str(), _ff_text_position, _font.type, _font.scale, _default_color));

    return true;
}

int Impl::get_num_primitives() const {
    return static_cast<int>(prims.size());
}

bool Impl::render(GstBuffer *buffer) {
    ITT_TASK(__FUNCTION__);

    // For D3D11 input, map to system memory temporarily for rendering
    GstMapInfo map_info;
    bool mapped = false;

    if (_mem_type == InferenceBackend::MemoryType::D3D11) {
        // Map D3D11 buffer to system memory for CPU rendering
        if (!gst_buffer_map(buffer, &map_info, GST_MAP_READWRITE)) {
            return false;
        }
        mapped = true;
    }

    // Skip render if there are no primitives to draw
    if (!prims.empty()) {
        auto gstbuffer = std::make_shared<dlstreamer::GSTFrame>(buffer, _vinfo);
        _renderer->draw(gstbuffer, prims);
    }

    // Unmap D3D11 buffer if it was mapped
    if (mapped) {
        gst_buffer_unmap(buffer, &map_info);
    }

    return true;
}

bool Impl::render_va(cv::Mat *buffer) {
    ITT_TASK(__FUNCTION__);

    // Skip render if there are no primitives to draw
    if (!prims.empty()) {
        _renderer_opencv->draw_va(*buffer, prims);
    }

    return true;
}

void Impl::preparePrimsForRoi(GVA::RegionOfInterest &roi, std::vector<render::Prim> &prims) const {
    size_t color_index = roi.label_id();

    auto rect_u32 = roi.rect();
    GVA::Rect<double> rect = {safe_convert<double>(rect_u32.x), safe_convert<double>(rect_u32.y),
                              safe_convert<double>(rect_u32.w), safe_convert<double>(rect_u32.h)};

    clip_rect(rect.x, rect.y, rect.w, rect.h, _vinfo);

    std::ostringstream text;
    const int object_id = roi.object_id();
    if (object_id > 0) {
        text << object_id << ": ";
        color_index = object_id;
    }

    if (roi.label().size() > 1) {
        appendStr(text, roi.label());
        text << int(roi.confidence() * 100) << "%";
    }

    // Prepare primitives for tensors
    for (auto &tensor : roi.tensors()) {
        preparePrimsForTensor(tensor, rect, prims, color_index);
        if (!tensor.is_detection()) {
            appendStr(text, tensor.label());
        }
    }

    // set color
    Color color = indexToColor(color_index);
    if (_displCfg.color_idx != DEFAULT_COLOR_IDX)
        color = _default_color;

    // put rectangle
    cv::Rect bbox_rect(rect.x, rect.y, rect.w, rect.h);
    if (!_obb)
        prims.emplace_back(render::Rect(bbox_rect, color, _displCfg.thickness, roi.rotation()));

    // put text
    if (_displCfg.show_labels)
        if (text.str().size() != 0) {
            cv::Point2f pos(rect.x, rect.y - 5.f);
            if (pos.y < 0)
                pos.y = rect.y + 30.f;
            double scale = (_displCfg.text_scale != _font.scale) ? _displCfg.text_scale : _font.scale;
            prims.emplace_back(render::Text(text.str(), pos, _font.type, scale, color));
        }

    // put avg-fps from gvafpscounter element
    if (_displ_avgfps) {
        std::ostringstream fpstext;
        gfloat avg_fps = 0.0f;
        g_object_get(_gvafpscounter_element, "avg-fps", &avg_fps, NULL);
        if (avg_fps > 0.0f) {
            fpstext << "[avg " << std::fixed << std::setprecision(1) << avg_fps << " FPS]";
            if (fpstext.str().size() != 0) {
                cv::Point2f pos(_vinfo->width * 0.7, _vinfo->height - 20.f);
                prims.emplace_back(render::Text(fpstext.str(), pos, _font.type, _font.scale * 0.7, indexToColor(1)));
            }
        }
    }
}

void Impl::preparePrimsForTensor(const GVA::Tensor &tensor, GVA::Rect<double> rect, std::vector<render::Prim> &prims,
                                 size_t color_index) const {
    // landmarks rendering
    if (tensor.model_name().find("landmarks") != std::string::npos || tensor.format() == "landmark_points") {
        std::vector<float> data = tensor.data<float>();
        for (size_t i = 0; i < data.size() / 2; i++) {
            Color color = indexToColor(i);
            int x_lm = safe_convert<int>(rect.x + rect.w * data[2 * i]);
            int y_lm = safe_convert<int>(rect.y + rect.h * data[2 * i + 1]);
            size_t radius = safe_convert<size_t>(1 + _radius_multiplier * rect.w);
            prims.emplace_back(render::Circle(cv::Point2i(x_lm, y_lm), radius, color, cv::FILLED));
        }
    }

    if (tensor.format() == "contour_points") {
        std::vector<float> data = tensor.data<float>();
        for (size_t i = 0; i < data.size(); i += 2) {
            int x = safe_convert<int>(rect.x + rect.w * data[i]);
            int y = safe_convert<int>(rect.y + rect.h * data[i + 1]);
            int x2, y2;
            if (i + 2 < data.size()) {
                x2 = safe_convert<int>(rect.x + rect.w * data[i + 2]);
                y2 = safe_convert<int>(rect.y + rect.h * data[i + 3]);
            } else {
                x2 = safe_convert<int>(rect.x + rect.w * data[0]);
                y2 = safe_convert<int>(rect.y + rect.h * data[1]);
            }
            prims.emplace_back(
                render::Line(cv::Point2i(x, y), cv::Point2i(x2, y2), _default_color, _displCfg.thickness));
        }
    }

    if (tensor.format() == "segmentation_mask") {
        std::vector<float> mask = tensor.data<float>();
        std::vector<guint> dims = tensor.dims();
        assert(dims.size() == 2);
        const cv::Size &mask_size{int(dims[0]), int(dims[1])};
        cv::Rect2f box(rect.x, rect.y, rect.w, rect.h);
        Color color = indexToColor(color_index);

        if (!_obb) {
            // overlay mask on top of image pixels
            prims.emplace_back(render::InstanceSegmantationMask(mask, mask_size, color, box));
        } else {
            // resize mask to non-rotated bounding box and convert to binary
            cv::Mat mask_resized, mask_converted;
            cv::resize(cv::Mat{mask_size, CV_32F, mask.data()}, mask_resized, cv::Size(rect.w, rect.h));
            cv::threshold(mask_resized, mask_resized, 0.5f, 1.0f, cv::THRESH_BINARY);
            mask_resized.convertTo(mask_converted, CV_8UC1);
            // find contours in binary mask and derive minimal bounding box
            std::vector<std::vector<cv::Point>> contours;
            cv::findContours(mask_converted, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);
            cv::RotatedRect rotated = cv::minAreaRect(contours[0]);
            // shift minimal rotated box to original box position and draw
            rotated.center = rotated.center + cv::Point2f(rect.x, rect.y);
            cv::Point2f vertices2f[4];
            rotated.points(vertices2f);
            for (int i = 0; i < 4; i++)
                prims.emplace_back(render::Line(vertices2f[i], vertices2f[(i + 1) % 4], color, _displCfg.thickness));
        }
    }

    if (tensor.format() == "semantic_mask") {
        assert(tensor.precision() == GVA::Tensor::Precision::I64);
        std::vector<int64_t> mask = tensor.data<int64_t>();
        std::vector<guint> dims = tensor.dims();
        const cv::Size &mask_size{int(dims[1]), int(dims[2])};
        cv::Rect2f box(rect.x, rect.y, rect.w, rect.h);
        prims.emplace_back(render::SemanticSegmantationMask(mask, mask_size, box));
    }

    preparePrimsForKeypoints(tensor, rect, prims);
}

/**
 * Prepares primitives for key points and their conections using given tensor's info.
 */
void Impl::preparePrimsForKeypoints(const GVA::Tensor &tensor, GVA::Rect<double> rectangle,
                                    std::vector<render::Prim> &prims) const {
    if (tensor.format() != "keypoints")
        return;

    const auto keypoints_data = tensor.data<float>();
    const auto confidence = tensor.get_vector<float>("confidence");

    if (keypoints_data.empty())
        throw std::runtime_error("Keypoints array is empty.");

    const auto &dimensions = tensor.dims();
    size_t points_num = dimensions[0];
    size_t point_dimension = dimensions[1];

    if (keypoints_data.size() != points_num * point_dimension)
        throw std::logic_error("The size of the keypoints data does not match the dimension: Size=" +
                               std::to_string(keypoints_data.size()) + " Dimension=[" + std::to_string(dimensions[0]) +
                               "," + std::to_string(dimensions[1]) + "].");

    for (size_t i = 0; i < points_num; ++i) {

        if ((confidence.size() > 0) && (confidence[i] < 0.5))
            continue;

        float x_real = keypoints_data[point_dimension * i];
        float y_real = keypoints_data[point_dimension * i + 1];

        if (x_real == -1.0f and y_real == -1.0f)
            continue;

        int x_lm = safe_convert<int>(rectangle.x + rectangle.w * x_real);
        int y_lm = safe_convert<int>(rectangle.y + rectangle.h * y_real);
        size_t radius = safe_convert<size_t>(1 + _radius_multiplier * (rectangle.w + rectangle.h));

        Color color = indexToColor(i);
        prims.emplace_back(render::Circle(cv::Point2i(x_lm, y_lm), radius, color, cv::FILLED));
    }

    preparePrimsForKeypointConnections(tensor.gst_structure(), keypoints_data, dimensions, confidence, rectangle,
                                       prims);
}

void Impl::preparePrimsForKeypointConnections(GstStructure *s, const std::vector<float> &keypoints_data,
                                              const std::vector<uint32_t> &dims, const std::vector<float> &confidence,
                                              const GVA::Rect<double> &rectangle,
                                              std::vector<render::Prim> &prims) const {
    if (not(gst_structure_has_field(s, "point_names") and gst_structure_has_field(s, "point_connections")))
        return;

    GValueArray *point_connections = nullptr;
    gst_structure_get_array(s, "point_connections", &point_connections);

    if (point_connections == nullptr)
        throw std::runtime_error("Arrays with point connections information is nullptr.");
    if (point_connections->n_values == 0)
        throw std::runtime_error("Arrays with point connections is empty.");

    GValueArray *point_names = nullptr;
    gst_structure_get_array(s, "point_names", &point_names);

    if (point_names == nullptr)
        throw std::runtime_error("Arrays with point names information is nullptr.");
    if (point_names->n_values == 0)
        throw std::runtime_error("Arrays with point names is empty.");

    size_t point_dimension = dims[1];
    if (point_names->n_values * point_dimension != keypoints_data.size())
        throw std::logic_error("Number of point names must be equal to number of keypoints.");

    if (point_connections->n_values % 2 != 0)
        throw std::logic_error("Expected even amount of point connections.");

    for (size_t i = 0; i < point_connections->n_values; i += 2) {
        const gchar *point_name_1 = g_value_get_string(point_connections->values + i);
        const gchar *point_name_2 = g_value_get_string(point_connections->values + i + 1);

        size_t index_1 = get_keypoint_index_by_name(point_name_1, point_names);
        size_t index_2 = get_keypoint_index_by_name(point_name_2, point_names);

        if (index_1 == point_names->n_values)
            throw std::runtime_error("Point name \"" + std::string(point_name_1) +
                                     "\" has not been found in point connections.");

        if (index_2 == point_names->n_values)
            throw std::runtime_error("Point name \"" + std::string(point_name_2) +
                                     "\" has not been found in point connections.");

        if (index_1 == index_2)
            throw std::logic_error("Point names in connection are the same: " + std::string(point_name_1) + " / " +
                                   std::string(point_name_2));

        if ((confidence.size() > 0) && ((confidence[index_1] < 0.5) || confidence[index_2] < 0.5))
            continue;

        index_1 = safe_mul(point_dimension, index_1);
        index_2 = safe_mul(point_dimension, index_2);

        float x1_real = keypoints_data[index_1];
        float y1_real = keypoints_data[index_1 + 1];
        float x2_real = keypoints_data[index_2];
        float y2_real = keypoints_data[index_2 + 1];

        if ((x1_real == -1.0f and y1_real == -1.0f) or (x2_real == -1.0f and y2_real == -1.0f))
            continue;

        int x1 = safe_convert<int>(rectangle.x + rectangle.w * x1_real);
        int y1 = safe_convert<int>(rectangle.y + rectangle.h * y1_real);
        int x2 = safe_convert<int>(rectangle.x + rectangle.w * x2_real);
        int y2 = safe_convert<int>(rectangle.y + rectangle.h * y2_real);

        prims.emplace_back(render::Line(cv::Point2i(x1, y1), cv::Point2i(x2, y2), _default_color, _displCfg.thickness));
    }

    g_value_array_free(point_connections);
    g_value_array_free(point_names);
}

void Impl::parse_displ_config() {
    // Parse watermark display configuration
    std::string displcfg_str = std::string(_displ_cfg);
    auto cfg = Utils::stringToMap(displcfg_str);
    auto iter = cfg.end();
    try {
        iter = cfg.find("text-scale");
        if (iter != cfg.end()) {
            _displCfg.text_scale = std::stof(iter->second);
            if (_displCfg.text_scale > 2.0 || _displCfg.text_scale < 0.1) {
                _displCfg.text_scale = 1.0;
                GST_WARNING(
                    "[gvawatermarkimpl] 'text-scale' parameter value is out of range (0.1, 2.0], set to default 1.0");
            }
            cfg.erase(iter);
        }
        iter = cfg.find("draw-txt-bg");
        if (iter != cfg.end()) {
            if (iter->second == "true" || iter->second == "false") {
                _displCfg.draw_text_background = (iter->second == "true");
            }
            cfg.erase(iter);
        }
        iter = cfg.find("show-labels");
        if (iter != cfg.end()) {
            if (iter->second == "true" || iter->second == "false") {
                _displCfg.show_labels = (iter->second == "true");
            }
            cfg.erase(iter);
        }
        iter = cfg.find("thickness");
        if (iter != cfg.end()) {
            _displCfg.thickness = std::stoi(iter->second);
            if (_displCfg.thickness < 1 || _displCfg.thickness >= 10) {
                _displCfg.thickness = DEFAULT_THICKNESS;
                GST_WARNING("[gvawatermarkimpl] 'thickness' parameter value is out of range [1, 10], set to default %d",
                            DEFAULT_THICKNESS);
            }
            cfg.erase(iter);
        }
        iter = cfg.find("color-idx");
        if (iter != cfg.end()) {
            int _color_idx = std::stoi(iter->second);
            if (_color_idx >= 0 && _color_idx <= 2) {
                if (_color_idx == 0)
                    _default_color = Color(255, 0, 0); // Red
                else if (_color_idx == 1)
                    _default_color = Color(0, 255, 0); // Green
                else if (_color_idx == 2)
                    _default_color = Color(0, 0, 255); // Blue

                _displCfg.color_idx = _color_idx;
            } else {
                _displCfg.color_idx = DEFAULT_COLOR_IDX;
                GST_WARNING("[gvawatermarkimpl] 'color-idx' parameter value is out of range [0, 2], using default "
                            "colors, set to default %d",
                            DEFAULT_COLOR_IDX);
            }
            cfg.erase(iter);
        }
    } catch (...) {
        if (iter == cfg.end())
            std::throw_with_nested(
                std::runtime_error("[gvawatermarkimpl] Error occured while parsing key/value parameters"));
        std::throw_with_nested(
            std::runtime_error("[gvawatermarkimpl] Invalid value provided for parameter: " + iter->first));
    }
}

std::unique_ptr<Renderer> Impl::createRenderer(std::shared_ptr<ColorConverter> converter) {

    dlstreamer::ImageFormat format = dlstreamer::gst_format_to_video_format(GST_VIDEO_INFO_FORMAT(_vinfo));
    auto buf_mapper = BufferMapperFactory::createMapper(InferenceBackend::MemoryType::SYSTEM);
    return create_cpu_renderer(format, converter, std::move(buf_mapper));
}

std::unique_ptr<Renderer> Impl::createOpenCVRenderer(std::shared_ptr<ColorConverter> converter) {

    auto buf_mapper = BufferMapperFactory::createMapper(InferenceBackend::MemoryType::SYSTEM);
    auto format = dlstreamer::ImageFormat::BGR;
    return create_cpu_renderer(format, converter, std::move(buf_mapper));
}

static gboolean plugin_init(GstPlugin *plugin) {
    if (!gst_element_register(plugin, "gvawatermarkimpl", GST_RANK_NONE, GST_TYPE_GVA_WATERMARK_IMPL))
        return FALSE;

    return TRUE;
}

GST_PLUGIN_DEFINE(GST_VERSION_MAJOR, GST_VERSION_MINOR, gvawatermarkimpl, PRODUCT_FULL_NAME " gvawatermarkimpl element",
                  plugin_init, PLUGIN_VERSION, PLUGIN_LICENSE, PACKAGE_NAME, GST_PACKAGE_ORIGIN)
