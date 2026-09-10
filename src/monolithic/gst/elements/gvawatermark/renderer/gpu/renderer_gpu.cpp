/*******************************************************************************
 * Copyright (C) 2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#ifdef ENABLE_GVAWATERMARK_GPU

#include "renderer_gpu.h"
#include "renderer_gpu_kernels.h"

#include <gst/gst.h>
#include <gst/video/video.h>
#include <opencv2/opencv.hpp>

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <mutex>

GST_DEBUG_CATEGORY_STATIC(gva_watermark_gpu_debug);
#define GST_CAT_DEFAULT gva_watermark_gpu_debug

namespace {

constexpr size_t ATLAS_MAX_BYTES = 4u << 20;
constexpr size_t ATLAS_MAX_ENTRIES = 1024;

/* Runs and single pixels are cheap individually but a dense segmentation
 * contour can emit a great many of them. Past this budget the frame goes to the
 * CPU renderer rather than growing the upload without bound; at 48 bytes a
 * record this caps the per-frame transfer at 48 MB. */
constexpr size_t RASTER_MAX_RECORDS = 1u << 20;

/* Segmentation coverage bitmaps are rebuilt every frame, so this only has to
 * bound one frame's worth: 64 MB is far past anything a detector emits. */
constexpr size_t BLOB_MAX_BYTES = 64u << 20;

/* Gaussian kernel lengths follow the ROI sizes, so a stream settles on a few of
 * them. The cap is only there to stop an adversarial stream of ever-changing
 * ROI sizes from growing the table without bound. */
constexpr size_t COEFF_MAX_ENTRIES = 256;

/* RendererYUV halves the line thickness on the U and V planes, and also uses
 * the halved thickness on the Y plane (drawing twice with a parity offset) so
 * the half-resolution chroma does not leave a shadow. Same rule as
 * calc_thick_for_u_v_planes() in renderer_cpu.cpp. */
int half_thickness(int thick) {
    if (thick <= 1)
        return thick;
    return thick / 2;
}

/* cv::rectangle(LINE_8, thickness) draws each edge as a band centred on the
 * edge and extending +-k pixels across it; measured against OpenCV for
 * thickness 1..8. Thickness 1 takes OpenCV's thin-line path, a single pixel. */
int band_half_width(int thick) {
    if (thick <= 1)
        return 0;
    return (thick + 1) / 2;
}

/* calc_point_for_u_v_planes(): cv::Point / 2, i.e. integer division that
 * truncates towards zero. Replicated verbatim so the GPU lands on the same
 * chroma pixel as the CPU renderer. */
int half_coord(int v) {
    return v / 2;
}

/* Plane colours come from the same conversion for a given render::Prim colour,
 * so exact float equality is the right test here: it answers "would these two
 * work items write the same byte", not "are these colours close". */
bool same_color(const cl_float4 &a, const cl_float4 &b) {
    return a.s[0] == b.s[0] && a.s[1] == b.s[1] && a.s[2] == b.s[2] && a.s[3] == b.s[3];
}

/* clamp() and expand_box() from renderer_cpu.cpp, which draw_instance_mask uses
 * to place the resampled mask. Copied rather than shared so the two renderers
 * cannot drift apart silently. */
template <typename T>
T clamp_to(T value, T low, T high) {
    return value < low ? low : (value > high ? high : value);
}

cv::Rect expand_box(const cv::Rect2f &box, float w_scale, float h_scale) {
    float w_half = box.width * 0.5f * w_scale, h_half = box.height * 0.5f * h_scale;
    const cv::Point2f &center = (box.tl() + box.br()) * 0.5f;
    return {cv::Point(int(center.x - w_half), int(center.y - h_half)),
            cv::Point(int(center.x + w_half), int(center.y + h_half))};
}

/* cv::getKernelType() restricted to the question createSeparableLinearFilter
 * actually asks of a smoothing kernel: is it exactly
 * KERNEL_SMOOTH + KERNEL_SYMMETRICAL, which is the gate on the integer
 * fixed-point path. A Gaussian normally is; a degenerate one-tap kernel is not,
 * because it is also KERNEL_INTEGER. */
bool is_smooth_symmetrical(const cv::Mat &kernel) {
    const int n = kernel.rows * kernel.cols;
    double sum = 0;
    bool all_integer = true;
    for (int i = 0; i < n; ++i) {
        const double a = kernel.at<float>(i), b = kernel.at<float>(n - i - 1);
        if (a != b || a < 0)
            return false;
        if (a != static_cast<double>(cv::saturate_cast<int>(a)))
            all_integer = false;
        sum += a;
    }
    if (all_integer)
        return false; // KERNEL_INTEGER would survive too, and the gate is ==
    return std::fabs(sum - 1) <= FLT_EPSILON * (std::fabs(sum) + 1);
}

/* createBitExactKernel_32S(kernel, ., 8): can every coefficient be replaced by
 * round(c * 256) without moving it more than the tolerance OpenCV allows? */
bool bit_exact_256(const cv::Mat &kernel, std::vector<float> &scaled) {
    const int n = kernel.rows * kernel.cols;
    const double eps = 10 * FLT_EPSILON * 256;
    scaled.resize(n);
    for (int i = 0; i < n; ++i) {
        const double approx = static_cast<double>(kernel.at<float>(i)) * 256;
        const int exact = cv::saturate_cast<int>(approx);
        if (std::fabs(approx - exact) > eps)
            return false;
        scaled[i] = static_cast<float>(exact);
    }
    return true;
}

bool cl_ok(cl_int err, const char *what) {
    if (err == CL_SUCCESS)
        return true;
    GST_WARNING("gvawatermark GPU renderer: %s failed with OpenCL error %d", what, static_cast<int>(err));
    return false;
}

} // namespace

// ---------------------------------------------------------------------------
// SharedContext
// ---------------------------------------------------------------------------

struct RendererGPU::SharedContext {
    cl_platform_id platform = nullptr;
    cl_device_id device = nullptr;
    cl_context context = nullptr;
    cl_program program = nullptr;

    clCreateFromVA_APIMediaSurfaceINTEL_fn create_from_va = nullptr;
    clEnqueueAcquireVA_APIMediaSurfacesINTEL_fn acquire = nullptr;
    clEnqueueReleaseVA_APIMediaSurfacesINTEL_fn release = nullptr;

    ~SharedContext() {
        if (program)
            clReleaseProgram(program);
        if (context)
            clReleaseContext(context);
    }

    static std::shared_ptr<SharedContext> get(VADisplay display);

  private:
    bool build(VADisplay display);
    bool build_program();
};

bool RendererGPU::SharedContext::build_program() {
    cl_int err = CL_SUCCESS;
    const char *source = renderer_gpu::KERNELS;
    cl_program prog = clCreateProgramWithSource(context, 1, &source, nullptr, &err);
    if (!prog || err != CL_SUCCESS) {
        cl_ok(err, "clCreateProgramWithSource");
        return false;
    }

    err = clBuildProgram(prog, 1, &device, "-cl-fast-relaxed-math", nullptr, nullptr);
    if (err != CL_SUCCESS) {
        size_t log_size = 0;
        clGetProgramBuildInfo(prog, device, CL_PROGRAM_BUILD_LOG, 0, nullptr, &log_size);
        std::string log(log_size, '\0');
        clGetProgramBuildInfo(prog, device, CL_PROGRAM_BUILD_LOG, log_size, log.data(), nullptr);
        GST_ERROR("gvawatermark GPU renderer: kernel build failed (%d): %s", static_cast<int>(err), log.c_str());
        clReleaseProgram(prog);
        return false;
    }

    program = prog;
    return true;
}

bool RendererGPU::SharedContext::build(VADisplay display) {
    cl_uint num_platforms = 0;
    if (!cl_ok(clGetPlatformIDs(0, nullptr, &num_platforms), "clGetPlatformIDs") || num_platforms == 0)
        return false;
    std::vector<cl_platform_id> platforms(num_platforms);
    if (!cl_ok(clGetPlatformIDs(num_platforms, platforms.data(), nullptr), "clGetPlatformIDs"))
        return false;

    /* cl_intel_va_api_media_sharing is the only usable route to a VA surface:
     * importing the surface's dma-buf as a flat OpenCL buffer gives a linear
     * byte view, which addresses the wrong pixels on the Tile4 surfaces that
     * the decoder and VPP produce. Probe once here and let the caller fall back
     * to the CPU renderer if the extension is missing. */
    for (cl_platform_id platform : platforms) {
        auto ext = [platform](const char *name) { return clGetExtensionFunctionAddressForPlatform(platform, name); };
        auto get_devices =
            reinterpret_cast<clGetDeviceIDsFromVA_APIMediaAdapterINTEL_fn>(ext("clGetDeviceIDsFromVA_APIMediaAdapterINTEL"));
        auto create_surface =
            reinterpret_cast<clCreateFromVA_APIMediaSurfaceINTEL_fn>(ext("clCreateFromVA_APIMediaSurfaceINTEL"));
        auto acquire_fn = reinterpret_cast<clEnqueueAcquireVA_APIMediaSurfacesINTEL_fn>(
            ext("clEnqueueAcquireVA_APIMediaSurfacesINTEL"));
        auto release_fn = reinterpret_cast<clEnqueueReleaseVA_APIMediaSurfacesINTEL_fn>(
            ext("clEnqueueReleaseVA_APIMediaSurfacesINTEL"));
        if (!get_devices || !create_surface || !acquire_fn || !release_fn)
            continue;

        cl_device_id dev = nullptr;
        cl_uint num_devices = 0;
        cl_int err = get_devices(platform, CL_VA_API_DISPLAY_INTEL, display, CL_PREFERRED_DEVICES_FOR_VA_API_INTEL, 1,
                                 &dev, &num_devices);
        if (err != CL_SUCCESS || num_devices == 0 || !dev)
            continue;

        /* CL_CONTEXT_INTEROP_USER_SYNC = CL_FALSE puts VA<->CL synchronization
         * on the driver, so the acquire/release pair around the kernels is all
         * the sync we need; no explicit vaSyncSurface. */
        cl_context_properties props[] = {CL_CONTEXT_VA_API_DISPLAY_INTEL,
                                         reinterpret_cast<cl_context_properties>(display),
                                         CL_CONTEXT_INTEROP_USER_SYNC,
                                         CL_FALSE,
                                         0};
        cl_context ctx = clCreateContext(props, 1, &dev, nullptr, nullptr, &err);
        if (!ctx || err != CL_SUCCESS) {
            cl_ok(err, "clCreateContext");
            continue;
        }

        this->platform = platform;
        this->device = dev;
        this->context = ctx;
        this->create_from_va = create_surface;
        this->acquire = acquire_fn;
        this->release = release_fn;

        /* Build right here rather than on the first frame: a source that does
         * not compile is a property of this platform, and the caller wants to
         * know now, while it can still pick the CPU renderer. */
        if (!build_program()) {
            this->platform = nullptr;
            this->device = nullptr;
            this->context = nullptr;
            clReleaseContext(ctx);
            continue;
        }

        char name[256] = {};
        clGetDeviceInfo(dev, CL_DEVICE_NAME, sizeof(name) - 1, name, nullptr);
        GST_INFO("gvawatermark GPU renderer: OpenCL device '%s' bound to VADisplay %p", name, display);
        return true;
    }

    GST_WARNING("gvawatermark GPU renderer: no OpenCL platform exposes cl_intel_va_api_media_sharing for "
                "VADisplay %p; falling back to the CPU renderer",
                display);
    return false;
}

std::shared_ptr<RendererGPU::SharedContext> RendererGPU::SharedContext::get(VADisplay display) {
    static std::mutex mutex;
    static std::map<VADisplay, std::weak_ptr<SharedContext>> cache;
    static std::map<VADisplay, bool> failed;

    std::lock_guard<std::mutex> lock(mutex);
    if (auto it = cache.find(display); it != cache.end()) {
        if (auto shared = it->second.lock())
            return shared;
    }
    if (failed.count(display))
        return nullptr;

    auto shared = std::shared_ptr<SharedContext>(new SharedContext());
    if (!shared->build(display)) {
        failed[display] = true;
        return nullptr;
    }
    cache[display] = shared;
    return shared;
}

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

RendererGPU::RendererGPU(std::shared_ptr<SharedContext> shared, std::shared_ptr<ColorConverter> color_converter,
                         int width, int height)
    : _shared(std::move(shared)), _color_converter(std::move(color_converter)), _width(width), _height(height) {
}

std::unique_ptr<RendererGPU> RendererGPU::create(VADisplay display, int width, int height,
                                                 std::shared_ptr<ColorConverter> color_converter) {
    static std::once_flag category_once;
    std::call_once(category_once, [] {
        GST_DEBUG_CATEGORY_INIT(gva_watermark_gpu_debug, "gvawatermarkgpu", 0,
                                "gvawatermark OpenCL plane-native renderer");
    });

    if (!display || width <= 1 || height <= 1 || !color_converter)
        return nullptr;

    auto shared = SharedContext::get(display);
    if (!shared)
        return nullptr;

    auto renderer =
        std::unique_ptr<RendererGPU>(new RendererGPU(std::move(shared), std::move(color_converter), width, height));
    if (!renderer->init())
        return nullptr;
    return renderer;
}

bool RendererGPU::init() {
    cl_program program = _shared->program;
    cl_int err = CL_SUCCESS;
    _queue = clCreateCommandQueueWithProperties(_shared->context, _shared->device, nullptr, &err);
    if (!_queue || !cl_ok(err, "clCreateCommandQueueWithProperties"))
        return false;

    _k_outlines = clCreateKernel(program, "wm_outlines", &err);
    if (!cl_ok(err, "clCreateKernel(wm_outlines)"))
        return false;
    _k_fills = clCreateKernel(program, "wm_fills", &err);
    if (!cl_ok(err, "clCreateKernel(wm_fills)"))
        return false;
    _k_masks = clCreateKernel(program, "wm_masks", &err);
    if (!cl_ok(err, "clCreateKernel(wm_masks)"))
        return false;
    _k_spans = clCreateKernel(program, "wm_spans", &err);
    if (!cl_ok(err, "clCreateKernel(wm_spans)"))
        return false;
    _k_blur_h = clCreateKernel(program, "wm_blur_h", &err);
    if (!cl_ok(err, "clCreateKernel(wm_blur_h)"))
        return false;
    _k_blur_v = clCreateKernel(program, "wm_blur_v", &err);
    if (!cl_ok(err, "clCreateKernel(wm_blur_v)"))
        return false;
    _k_blend_gather = clCreateKernel(program, "wm_blend_gather", &err);
    if (!cl_ok(err, "clCreateKernel(wm_blend_gather)"))
        return false;
    _k_blend_scatter = clCreateKernel(program, "wm_blend_scatter", &err);
    if (!cl_ok(err, "clCreateKernel(wm_blend_scatter)"))
        return false;

    for (int plane = 0; plane < 2; ++plane)
        _span_rows[plane].resize(plane_height(plane));
    calibrate_blur_rounding();
    return true;
}

RendererGPU::~RendererGPU() {
    for (auto &entry : _plane_cache) {
        for (cl_mem plane : entry.second) {
            if (plane)
                clReleaseMemObject(plane);
        }
    }
    _plane_cache.clear();

    if (_prim_buffer)
        clReleaseMemObject(_prim_buffer);
    if (_atlas_buffer)
        clReleaseMemObject(_atlas_buffer);
    if (_coeff_buffer)
        clReleaseMemObject(_coeff_buffer);
    if (_blob_buffer)
        clReleaseMemObject(_blob_buffer);
    if (_scratch_buffer)
        clReleaseMemObject(_scratch_buffer);
    if (_k_outlines)
        clReleaseKernel(_k_outlines);
    if (_k_fills)
        clReleaseKernel(_k_fills);
    if (_k_masks)
        clReleaseKernel(_k_masks);
    if (_k_spans)
        clReleaseKernel(_k_spans);
    if (_k_blur_h)
        clReleaseKernel(_k_blur_h);
    if (_k_blur_v)
        clReleaseKernel(_k_blur_v);
    if (_k_blend_gather)
        clReleaseKernel(_k_blend_gather);
    if (_k_blend_scatter)
        clReleaseKernel(_k_blend_scatter);
    if (_queue)
        clReleaseCommandQueue(_queue);
}

// ---------------------------------------------------------------------------
// Colours
// ---------------------------------------------------------------------------

cl_float4 RendererGPU::plane_color_y(const cv::Scalar &yuv) {
    cl_float4 c;
    c.s[0] = static_cast<float>(yuv[0]) / 255.f;
    c.s[1] = c.s[2] = c.s[3] = 0.f;
    return c;
}

cl_float4 RendererGPU::plane_color_uv(const cv::Scalar &yuv) {
    cl_float4 c;
    c.s[0] = static_cast<float>(yuv[1]) / 255.f;
    c.s[1] = static_cast<float>(yuv[2]) / 255.f;
    c.s[2] = c.s[3] = 0.f;
    return c;
}

// ---------------------------------------------------------------------------
// Flattening
// ---------------------------------------------------------------------------

RendererGPU::Group &RendererGPU::group_for(BatchId id, int x0, int y0, int x1, int y1, const cl_float4 &color,
                                           bool any_color) {
    const int plane = id & 1;
    std::vector<Group> &groups = _groups[plane];
    const size_t used = _group_count[plane];

    /* Scanned back to front for the last group this primitive must not race
     * with. Everything in the groups before that one is then covered too, so the
     * scan can stop at the first hit. */
    size_t after = 0;
    for (size_t i = used; i-- > 0;) {
        const Group &group = groups[i];
        bool conflict = false;
        for (const Group::Box &box : group.boxes) {
            if (x1 < box.x0 || x0 > box.x1 || y1 < box.y0 || y0 > box.y1)
                continue;
            /* Same colour means both work items write the same value, so letting
             * them race is harmless and does not need a group boundary. */
            if (!any_color && !box.any_color && same_color(box.color, color))
                continue;
            conflict = true;
            break;
        }
        if (conflict) {
            after = i + 1;
            break;
        }
    }

    /* Any group at or after that point running the right kernel will do - it
     * cannot itself hold a conflict, or the scan above would have stopped
     * there. Reusing one keeps the dispatch count at the length of the longest
     * overlap chain instead of the primitive count. */
    size_t target = used;
    for (size_t i = after; i < used; ++i) {
        if (groups[i].id == id && !groups[i].exclusive) {
            target = i;
            break;
        }
    }
    if (target == used) {
        if (groups.size() <= used)
            groups.emplace_back();
        Group &fresh = groups[used];
        fresh.id = id;
        fresh.prims.clear();
        fresh.boxes.clear();
        fresh.max_w = 0;
        fresh.max_h = 0;
        fresh.base = 0;
        fresh.exclusive = false;
        _group_count[plane] = used + 1;
    }

    Group &group = groups[target];
    group.boxes.push_back({x0, y0, x1, y1, color, any_color});
    return group;
}

RendererGPU::Group &RendererGPU::exclusive_group(BatchId id, int x0, int y0, int x1, int y1) {
    const int plane = id & 1;
    std::vector<Group> &groups = _groups[plane];
    const size_t used = _group_count[plane];
    if (groups.size() <= used)
        groups.emplace_back();

    /* Appended unconditionally, which orders it after every primitive of this
     * plane so far - a read-modify-write has to see all of them. Its box is
     * registered any_color, so whatever comes next is in turn ordered after it. */
    Group &group = groups[used];
    group.id = id;
    group.prims.clear();
    group.boxes.clear();
    group.max_w = 0;
    group.max_h = 0;
    group.base = 0;
    group.exclusive = true;
    _group_count[plane] = used + 1;

    cl_float4 unused{};
    group.boxes.push_back({x0, y0, x1, y1, unused, true});
    return group;
}

void RendererGPU::add_outline(BatchId id, int x0, int y0, int x1, int y1, int k, const cl_float4 &color) {
    if (x1 < x0)
        std::swap(x0, x1);
    if (y1 < y0)
        std::swap(y0, y1);

    /* The bands and the corner discs reach k pixels outside the rectangle. The
     * box is the outline's hull rather than just the painted ring, so two nested
     * rectangles are reported as conflicting even though they need not be; that
     * costs one dispatch, never correctness. */
    flush_spans_overlapping(id & 1, x0 - k, y0 - k, x1 + k, y1 + k);
    Group &group = group_for(id, x0 - k, y0 - k, x1 + k, y1 + k, color, false);
    Prim p{};
    p.a.s[0] = x0;
    p.a.s[1] = y0;
    p.a.s[2] = x1;
    p.a.s[3] = y1;
    p.b.s[0] = k;
    p.color = color;
    group.prims.push_back(p);

    // dim0 walks the longest edge, but also the 2k+1 columns of a corner disc;
    // dim1 walks the 2k+1 rows shared by the bands and the discs.
    group.max_w = std::max(group.max_w, std::max(std::max(x1 - x0, y1 - y0) + 1, 2 * k + 1));
    group.max_h = std::max(group.max_h, 2 * k + 1);
}

void RendererGPU::add_fill(BatchId id, int x0, int y0, int x1, int y1, const cl_float4 &color) {
    if (x1 < x0)
        std::swap(x0, x1);
    if (y1 < y0)
        std::swap(y0, y1);

    flush_spans_overlapping(id & 1, x0, y0, x1, y1);
    Group &group = group_for(id, x0, y0, x1, y1, color, false);
    Prim p{};
    p.a.s[0] = x0;
    p.a.s[1] = y0;
    p.a.s[2] = x1;
    p.a.s[3] = y1;
    p.color = color;
    group.prims.push_back(p);

    group.max_w = std::max(group.max_w, x1 - x0 + 1);
    group.max_h = std::max(group.max_h, y1 - y0 + 1);
}

void RendererGPU::add_mask(BatchId id, const AtlasEntry &entry, int dst_x, int dst_y, const cl_float4 &color) {
    if (entry.width <= 0 || entry.height <= 0)
        return;

    /* A glyph bitmap only covers part of its box, so two overlapping labels may
     * be split into separate groups without actually sharing a pixel. That costs
     * one extra dispatch, never correctness. */
    const int x1 = dst_x + entry.width - 1, y1 = dst_y + entry.height - 1;
    flush_spans_overlapping(id & 1, dst_x, dst_y, x1, y1);
    Group &group = group_for(id, dst_x, dst_y, x1, y1, color, false);
    Prim p{};
    p.a.s[0] = dst_x;
    p.a.s[1] = dst_y;
    p.a.s[2] = entry.width;
    p.a.s[3] = entry.height;
    p.b.s[0] = static_cast<cl_int>(entry.offset);
    p.b.s[1] = entry.width; // atlas rows are packed, stride == width
    p.color = color;
    group.prims.push_back(p);

    group.max_w = std::max(group.max_w, entry.width);
    group.max_h = std::max(group.max_h, entry.height);
}

const RendererGPU::CoeffEntry *RendererGPU::gaussian_coeffs(int ksize) {
    if (auto it = _coeff_index.find(ksize); it != _coeff_index.end())
        return &it->second;
    if (_coeff_index.size() >= COEFF_MAX_ENTRIES) {
        _coeff_index.clear();
        _coeff_host.clear();
        _coeff_uploaded = 0;
    }

    /* Exactly what createGaussianKernels() does for a CV_8U image with
     * sigma = 0: getGaussianKernel derives sigma from the length itself. */
    cv::Mat kernel;
    try {
        kernel = cv::getGaussianKernel(ksize, 0, CV_32F);
    } catch (const cv::Exception &e) {
        GST_WARNING("gvawatermark GPU renderer: getGaussianKernel(%d) failed: %s", ksize, e.what());
        return nullptr;
    }
    if (kernel.rows * kernel.cols != ksize || kernel.type() != CV_32F)
        return nullptr;

    CoeffEntry entry{};
    entry.float_offset = _coeff_host.size();
    for (int i = 0; i < ksize; ++i)
        _coeff_host.push_back(kernel.at<float>(i));

    std::vector<float> scaled;
    entry.int_valid = is_smooth_symmetrical(kernel) && bit_exact_256(kernel, scaled);
    if (entry.int_valid) {
        entry.int_offset = _coeff_host.size();
        _coeff_host.insert(_coeff_host.end(), scaled.begin(), scaled.end());
    }

    return &_coeff_index.emplace(ksize, entry).first->second;
}

int RendererGPU::blur_vector_elements(int n) const {
    if (_blur_vec_lanes <= 0)
        return 0;
    /* Transcribed from SymmColumnVec_32s8u::operator(): a run of whole
     * v_uint8-wide steps, then at most one v_uint16-wide one. */
    const int v8 = _blur_vec_lanes, v16 = _blur_vec_lanes / 2;
    int i = 0;
    for (; i <= n - v8; i += v8)
        ;
    if (i <= n - v16)
        i += v16;
    return i;
}

void RendererGPU::calibrate_blur_rounding() {
    /* Feeds cv::GaussianBlur an input for which every output element is an
     * exact .5 tie, so the boundary shows up directly as a step in the output.
     * With the length-7 kernel {8,28,56,72,56,28,8}/256 and rows that are
     * constant in x, a single centre row of 16 gives an accumulator of
     * 256*72*16 = 294912, i.e. 4.5: half to even writes 4, half up writes 5.
     *
     * Two widths are probed because one cannot tell a 64-lane vector path
     * (which skips a 24 wide row entirely) from a build with no vector path at
     * all (which skips both). */
    static const struct {
        int width;
        int expect[3]; // lanes 16, 32, 64
    } probes[] = {{24, {24, 16, 0}}, {100, {96, 96, 96}}};

    int measured[2] = {-1, -1};
    for (int p = 0; p < 2; ++p) {
        const int w = probes[p].width;
        cv::Mat plane = cv::Mat::zeros(16, w, CV_8UC1);
        const cv::Rect rect(0, 8, w, 1);
        plane.row(rect.y).setTo(cv::Scalar(16));
        try {
            cv::Mat sub = plane(rect);
            cv::GaussianBlur(sub, sub, cv::Size(7, 7), 0);
        } catch (const cv::Exception &e) {
            GST_WARNING("gvawatermark GPU renderer: blur rounding calibration failed: %s", e.what());
            return;
        }
        const uint8_t *out = plane.ptr<uint8_t>(rect.y);
        int boundary = w;
        for (int i = 0; i < w; ++i) {
            if (out[i] != 4 && out[i] != 5) {
                GST_WARNING("gvawatermark GPU renderer: blur rounding calibration got %d at %d, expected a .5 tie",
                            out[i], i);
                return;
            }
            if (out[i] == 5 && boundary == w) // first element the scalar tail took
                boundary = i;
        }
        measured[p] = boundary;
    }

    for (int lanes : {16, 32, 64}) {
        const int idx = lanes == 16 ? 0 : lanes == 32 ? 1 : 2;
        if (measured[0] == probes[0].expect[idx] && measured[1] == probes[1].expect[idx]) {
            _blur_vec_lanes = lanes;
            return;
        }
    }
    if (measured[0] == 0 && measured[1] == 0) {
        _blur_vec_lanes = 0; // no vector column filter in this OpenCV build
        return;
    }
    /* An OpenCV whose handover we cannot model. The blur still runs; small
     * rectangles may differ from the CPU path by one on tie pixels. */
    GST_WARNING("gvawatermark GPU renderer: unrecognized blur rounding handover (%d, %d); "
                "blurs may differ from the CPU renderer by one on tie pixels",
                measured[0], measured[1]);
    _blur_vec_lanes = 32;
}

void RendererGPU::add_blur(BatchId id, const cv::Rect &rect, int cn, int kw, int kh, const CoeffEntry &cx,
                           const CoeffEntry &cy, bool integer_regime) {
    /* The taps reach the kernel's half width outside the rectangle, and the
     * result depends on what is there, so the conflict box is the rectangle
     * grown by that much rather than the rectangle itself. */
    const int ax = kw / 2, ay = kh / 2;
    const int x0 = rect.x - ax, y0 = rect.y - ay;
    const int x1 = rect.x + rect.width - 1 + ax, y1 = rect.y + rect.height - 1 + ay;

    flush_spans_overlapping(id & 1, x0, y0, x1, y1);
    Group &group = exclusive_group(id, x0, y0, x1, y1);

    Prim p{};
    p.a.s[0] = rect.x;
    p.a.s[1] = rect.y;
    p.a.s[2] = rect.width;
    p.a.s[3] = rect.height;
    p.b.s[0] = kw;
    p.b.s[1] = kh;
    p.b.s[2] = static_cast<cl_int>(integer_regime ? cx.int_offset : cx.float_offset);
    p.b.s[3] = static_cast<cl_int>(integer_regime ? cy.int_offset : cy.float_offset);
    p.color.s[0] = integer_regime ? 1.f : 0.f;
    /* The handover is always a multiple of 16 elements, so with one or two
     * channels it never falls inside a pixel and can be passed as a column. */
    p.color.s[1] = static_cast<float>(blur_vector_elements(rect.width * cn) / cn);
    group.prims.push_back(p);

    group.max_w = rect.width;
    group.max_h = rect.height;
    _scratch_needed =
        std::max(_scratch_needed, static_cast<size_t>(rect.width) * static_cast<size_t>(rect.height + kh - 1));
}

bool RendererGPU::add_blend(BatchId id, const cv::Rect &rect, const cv::Mat &bitmap, const cl_float4 &color) {
    if (rect.width <= 0 || rect.height <= 0)
        return true;
    if (bitmap.cols != rect.width || bitmap.rows != rect.height || bitmap.type() != CV_8UC1)
        return false;

    const size_t bytes = static_cast<size_t>(rect.width) * rect.height;
    if (_blob_host.size() + bytes > BLOB_MAX_BYTES) {
        _last_unsupported = "segmentation masks exceed the per-frame budget";
        return false;
    }
    const size_t offset = _blob_host.size();
    _blob_host.resize(offset + bytes);
    for (int row = 0; row < rect.height; ++row)
        std::memcpy(_blob_host.data() + offset + static_cast<size_t>(row) * rect.width, bitmap.ptr<uint8_t>(row),
                    rect.width);

    const int x1 = rect.x + rect.width - 1, y1 = rect.y + rect.height - 1;
    flush_spans_overlapping(id & 1, rect.x, rect.y, x1, y1);
    Group &group = exclusive_group(id, rect.x, rect.y, x1, y1);

    Prim p{};
    p.a.s[0] = rect.x;
    p.a.s[1] = rect.y;
    p.a.s[2] = rect.width;
    p.a.s[3] = rect.height;
    p.b.s[0] = static_cast<cl_int>(offset);
    p.b.s[1] = rect.width; // blob rows are packed
    p.color = color;
    group.prims.push_back(p);

    group.max_w = rect.width;
    group.max_h = rect.height;
    _scratch_needed = std::max(_scratch_needed, bytes);
    return true;
}

void RendererGPU::insert_span(std::vector<SpanRec> &row, int a, int b, const cl_float4 &color) {
    for (size_t i = 0; i < row.size();) {
        const int x1 = row[i].x1, x2 = row[i].x2;
        if (x2 < a || x1 > b) {
            ++i;
            continue;
        }
        const bool left = x1 < a, right = x2 > b;
        if (left && right) {
            /* The old entry is split in two. Taking a copy of the colour first
             * matters: push_back may reallocate and invalidate row[i]. */
            const cl_float4 old = row[i].color;
            row[i].x2 = a - 1;
            row.push_back({b + 1, x2, old});
            ++i; // the remnant just appended cannot overlap [a, b]
        } else if (left) {
            row[i].x2 = a - 1;
            ++i;
        } else if (right) {
            row[i].x1 = b + 1;
            ++i;
        } else {
            /* Fully covered. Entries are unordered and disjoint, so the last one
             * can be swapped in - and must then be tested in turn. */
            row[i] = row.back();
            row.pop_back();
        }
    }
    row.push_back({a, b, color});
}

bool RendererGPU::add_raster(BatchId span_id, const renderer_gpu::Raster &raster, const cl_float4 &color) {
    const int plane = span_id & 1;
    std::vector<std::vector<SpanRec>> &rows = _span_rows[plane];
    const int w = plane_width(plane);

    /* The rasterizers emit a primitive as spans (FillConvexPoly's scanline
     * walk, disc rows) plus loose pixels (the Line2 edge strokes, thin
     * Bresenham lines). Coalescing them per row first is exact - every record of
     * one primitive carries the same colour, so overlapping and abutting
     * intervals can be fused without changing which pixels end up painted - and
     * it keeps both the number of insertions below and the kernel's work size
     * down to the longest run rather than one work item per loose pixel. */
    _merge.clear();
    _merge.reserve(raster.size());
    for (const renderer_gpu::Raster::Run &run : raster.runs)
        _merge.push_back({run.y, run.x1, run.x2});
    for (const cv::Point &pt : raster.points)
        _merge.push_back({pt.y, pt.x, pt.x});

    std::sort(_merge.begin(), _merge.end(), [](const Span &a, const Span &b) {
        return a.y != b.y ? a.y < b.y : a.x1 < b.x1;
    });

    /* Runs go into the per-row lists rather than straight into a group, because
     * work items of one NDRange are unordered: two records covering the same
     * pixel with different colours would race, and the frame would not even be
     * reproducible. insert_span() resolves the overlap on the host, in painter's
     * order, so what reaches the device is a disjoint set - one dispatch,
     * whatever pile of shapes went into it, matching the CPU renderer exactly. */
    for (size_t i = 0; i < _merge.size();) {
        Span cur = _merge[i++];
        while (i < _merge.size() && _merge[i].y == cur.y && _merge[i].x1 <= cur.x2 + 1)
            cur.x2 = std::max(cur.x2, _merge[i++].x2);

        if (cur.y < 0 || cur.y >= static_cast<int>(rows.size()) || cur.x2 < 0 || cur.x1 >= w)
            continue;
        cur.x1 = std::max(cur.x1, 0);
        cur.x2 = std::min(cur.x2, w - 1);

        // One insertion nets at most two records: the new one plus a split remnant.
        if (_span_records + 2 > RASTER_MAX_RECORDS) {
            _last_unsupported = "primitive rasterizes to too many runs";
            return false;
        }

        std::vector<SpanRec> &row = rows[cur.y];
        const size_t before = row.size();
        insert_span(row, cur.x1, cur.x2, color);
        const size_t after = row.size();
        if (_span_pending[plane] == 0)
            _span_box[plane] = {cur.x1, cur.y, cur.x2, cur.y};
        _span_pending[plane] = _span_pending[plane] + after - before;
        _span_records = _span_records + after - before;

        std::array<int, 4> &box = _span_box[plane];
        box[0] = std::min(box[0], cur.x1);
        box[1] = std::min(box[1], cur.y);
        box[2] = std::max(box[2], cur.x2);
        box[3] = std::max(box[3], cur.y);
    }
    return true;
}

void RendererGPU::flush_spans(int plane) {
    if (_span_pending[plane] == 0)
        return;

    const std::array<int, 4> &box = _span_box[plane];
    const BatchId id = plane == 0 ? SPAN_P0 : SPAN_P1;
    /* One conflict box for the whole set, flagged any_color: the runs carry many
     * colours, and a later primitive only needs to know that something was
     * painted under it. */
    cl_float4 unused{};
    Group &group = group_for(id, box[0], box[1], box[2], box[3], unused, true);

    std::vector<std::vector<SpanRec>> &rows = _span_rows[plane];
    for (size_t y = 0; y < rows.size(); ++y) {
        for (const SpanRec &rec : rows[y]) {
            Prim p{};
            p.a.s[0] = rec.x1;
            p.a.s[1] = rec.x2;
            p.a.s[2] = static_cast<cl_int>(y);
            p.color = rec.color;
            group.prims.push_back(p);
            group.max_w = std::max(group.max_w, rec.x2 - rec.x1 + 1);
            group.max_h = 1;
        }
        rows[y].clear();
    }
    _span_pending[plane] = 0;
}

void RendererGPU::flush_spans_overlapping(int plane, int x0, int y0, int x1, int y1) {
    if (_span_pending[plane] == 0)
        return;
    const std::array<int, 4> &box = _span_box[plane];
    if (x1 < box[0] || x0 > box[2] || y1 < box[1] || y0 > box[3])
        return;
    flush_spans(plane);
}

bool RendererGPU::flatten_rect(const render::Rect &rect) {
    if (rect.rotation != 0.0) {
        _last_unsupported = "rotated rectangle (obb=true)";
        return false;
    }
    if (rect.thick <= 0) {
        _last_unsupported = "filled rectangle";
        return false;
    }

    /* RendererI420/NV12::draw_rectangle aligns the bottom-right with
     * render::render by subtracting (1, 1). */
    const cv::Point tl = rect.rect.tl();
    const cv::Point br = rect.rect.br() - cv::Point(1, 1);
    const int thick = half_thickness(rect.thick);
    const int k = band_half_width(thick);

    add_outline(OUTLINE_P1, half_coord(tl.x), half_coord(tl.y), half_coord(br.x), half_coord(br.y), k,
                plane_color_uv(rect.color));

    /* draw_rect_y_plane(): every U/V pixel covers two Y pixels, so the Y plane
     * gets the same half-thickness outline twice, the second one nudged by the
     * coordinate parity, to avoid a chroma shadow. */
    const cl_float4 color_y = plane_color_y(rect.color);
    add_outline(OUTLINE_P0, tl.x, tl.y, br.x, br.y, k, color_y);
    add_outline(OUTLINE_P0, tl.x + (tl.x % 2 ? -1 : 1), tl.y + (tl.y % 2 ? -1 : 1), br.x + (br.x % 2 ? -1 : 1),
                br.y + (br.y % 2 ? -1 : 1), k, color_y);
    return true;
}

bool RendererGPU::flatten_text(const render::Text &text) {
    if (text.text.empty())
        return true;

    /* draw_text_bg fills the background with the primitive colour and draw_text
     * then writes the label in white, which is (255, 128, 128) in NV12. */
    if (text.draw_bg) {
        int baseline = 0;
        const cv::Size size = cv::getTextSize(text.text, text.fonttype, text.fontscale, text.thick, &baseline);
        const cv::Point bg_tl(text.org.x, text.org.y - size.height);
        const cv::Point bg_br(text.org.x + size.width, text.org.y + baseline);
        add_fill(FILL_P0, bg_tl.x, bg_tl.y, bg_br.x, bg_br.y, plane_color_y(text.color));
        add_fill(FILL_P1, half_coord(bg_tl.x), half_coord(bg_tl.y), half_coord(bg_br.x), half_coord(bg_br.y),
                 plane_color_uv(text.color));
    }

    const cv::Scalar color = text.draw_bg ? cv::Scalar(255, 128, 128) : text.color;

    const AtlasEntry *y_entry = rasterize_text(text.text, text.fonttype, text.fontscale, text.thick);
    if (!y_entry) {
        _last_unsupported = "text rasterization failed";
        return false;
    }
    add_mask(MASK_P0, *y_entry, text.org.x + y_entry->origin_x, text.org.y + y_entry->origin_y, plane_color_y(color));

    /* The U/V planes carry an independently rasterized label at half the font
     * scale and half the thickness, drawn at half the position. */
    const int uv_thick = half_thickness(text.thick);
    const AtlasEntry *uv_entry = rasterize_text(text.text, text.fonttype, text.fontscale / 2.0, uv_thick);
    if (!uv_entry) {
        _last_unsupported = "text rasterization failed";
        return false;
    }
    add_mask(MASK_P1, *uv_entry, half_coord(text.org.x) + uv_entry->origin_x,
             half_coord(text.org.y) + uv_entry->origin_y, plane_color_uv(color));
    return true;
}

bool RendererGPU::flatten_line(const render::Line &line) {
    /* cv::line asserts thickness > 0; the CPU renderer would throw too. */
    if (line.thick <= 0) {
        _last_unsupported = "line with non-positive thickness";
        return false;
    }

    /* RendererNV12::draw_line draws the Y plane at the *full* thickness and with
     * no parity double-draw - unlike draw_rectangle, which halves the thickness
     * and draws twice. Only the U/V plane halves coordinates and thickness.
     * cv::line caps both endpoints, hence caps == 3. */
    _raster.clear();
    renderer_gpu::thick_line(line.pt1, line.pt2, line.thick, 3, _width, _height, _raster);
    if (!add_raster(SPAN_P0, _raster, plane_color_y(line.color)))
        return false;

    _raster.clear();
    renderer_gpu::thick_line({half_coord(line.pt1.x), half_coord(line.pt1.y)},
                             {half_coord(line.pt2.x), half_coord(line.pt2.y)}, half_thickness(line.thick), 3,
                             _width / 2, _height / 2, _raster);
    return add_raster(SPAN_P1, _raster, plane_color_uv(line.color));
}

bool RendererGPU::flatten_circle(const render::Circle &circle) {
    /* cv::circle only *fills* for thickness < 0; thickness 0 and 1 both draw the
     * one-pixel Bresenham arc, and thickness > 1 goes to EllipseEx, which
     * strokes an arc polyline and is not ported. In-tree generators (landmarks,
     * keypoints) always pass cv::FILLED; the other thicknesses only arrive
     * through a user-supplied WatermarkCircleMeta. */
    if (circle.thick > 1) {
        _last_unsupported = "circle outline with thickness > 1";
        return false;
    }
    if (circle.radius < 0) {
        _last_unsupported = "circle with negative radius";
        return false;
    }
    const bool fill = circle.thick < 0;

    /* RendererNV12::draw_circle halves the radius and the thickness for the U/V
     * plane. calc_thick_for_u_v_planes() leaves thickness <= 1 alone, so the
     * fill/outline choice is the same on both planes. */
    _raster.clear();
    if (fill)
        renderer_gpu::filled_disc(circle.center.x, circle.center.y, circle.radius, _width, _height, _raster);
    else
        renderer_gpu::circle_outline(circle.center.x, circle.center.y, circle.radius, _width, _height, _raster);
    if (!add_raster(SPAN_P0, _raster, plane_color_y(circle.color)))
        return false;

    const int uv_cx = half_coord(circle.center.x), uv_cy = half_coord(circle.center.y);
    const int uv_r = circle.radius / 2, uv_w = _width / 2, uv_h = _height / 2;
    _raster.clear();
    if (fill)
        renderer_gpu::filled_disc(uv_cx, uv_cy, uv_r, uv_w, uv_h, _raster);
    else
        renderer_gpu::circle_outline(uv_cx, uv_cy, uv_r, uv_w, uv_h, _raster);
    return add_raster(SPAN_P1, _raster, plane_color_uv(circle.color));
}

bool RendererGPU::flatten_polygon(const render::Polygon &polygon) {
    /* Negative thickness makes cv::drawContours fill the contour instead of
     * stroking it, which is a different rasterizer. */
    if (polygon.thick <= 0) {
        _last_unsupported = "filled polygon";
        return false;
    }
    if (polygon.points.empty())
        return true;

    /* cv::drawContours strokes every edge of the closed contour with
     * ThickLine(flags = 2), which caps only the far endpoint so a shared vertex
     * is not capped twice. */
    const int n = static_cast<int>(polygon.points.size());
    _raster.clear();
    for (int i = 0; i < n; ++i)
        renderer_gpu::thick_line(polygon.points[i], polygon.points[(i + 1) % n], polygon.thick, 2, _width, _height,
                                 _raster);
    if (!add_raster(SPAN_P0, _raster, plane_color_y(polygon.color)))
        return false;

    const int uv_thick = half_thickness(polygon.thick);
    _raster.clear();
    for (int i = 0; i < n; ++i) {
        const cv::Point &a = polygon.points[i];
        const cv::Point &b = polygon.points[(i + 1) % n];
        renderer_gpu::thick_line({half_coord(a.x), half_coord(a.y)}, {half_coord(b.x), half_coord(b.y)}, uv_thick, 2,
                                 _width / 2, _height / 2, _raster);
    }
    return add_raster(SPAN_P1, _raster, plane_color_uv(polygon.color));
}

bool RendererGPU::flatten_blur(const render::Blur &blur) {
    const cv::Rect &r = blur.rect;

    /* blur_rectangle builds its submatrices with the plain cv::Mat(mat, rect)
     * constructor, which throws on anything the plane does not fully contain.
     * Declining here keeps that a CPU-path decision instead of letting the GPU
     * quietly render something the golden never would. Two pixels in each
     * direction are needed, or the halved chroma rectangle is empty. */
    if (r.width < 2 || r.height < 2 || r.x < 0 || r.y < 0 || r.x + r.width > _width || r.y + r.height > _height) {
        _last_unsupported = "blur rectangle outside the frame";
        return false;
    }

    /* Order matters and is not the obvious one: blur_rectangle does U/V first
     * and Y second. Only the plane ordering is observable here, since the two
     * planes are independent images, but keeping it makes the two renderers
     * line up line for line. Each plane gets its own kernel size - blurring
     * only Y would leave a sharp chroma outline. */
    const cv::Rect uv(r.x / 2, r.y / 2, r.width / 2, r.height / 2);
    const cv::Size ks_uv = render::computeBlurKernelSize(r.width / 2, r.height / 2);
    const cv::Size ks_y = render::computeBlurKernelSize(r.width, r.height);

    struct Job {
        BatchId id;
        cv::Rect rect;
        cv::Size ksize;
        int cn;
    } jobs[2] = {{BLUR_P1, uv, ks_uv, 2}, {BLUR_P0, r, ks_y, 1}};

    for (const Job &job : jobs) {
        if (job.rect.width <= 0 || job.rect.height <= 0)
            continue;
        const CoeffEntry *cx = gaussian_coeffs(job.ksize.width);
        const CoeffEntry *cy = gaussian_coeffs(job.ksize.height);
        if (!cx || !cy) {
            _last_unsupported = "Gaussian kernel construction failed";
            return false;
        }
        /* sepFilter2D takes the integer fixed-point path only when *both*
         * kernels quantize exactly, so the decision is per blur, not per
         * kernel. In practice that means the small kernels of a small ROI. */
        add_blur(job.id, job.rect, job.cn, job.ksize.width, job.ksize.height, *cx, *cy,
                 cx->int_valid && cy->int_valid);
    }
    return true;
}

bool RendererGPU::flatten_instance_mask(const render::InstanceSegmantationMask &mask) {
    if (mask.size.width <= 0 || mask.size.height <= 0 ||
        mask.data.size() != static_cast<size_t>(mask.size.area())) {
        _last_unsupported = "malformed instance segmentation mask";
        return false;
    }

    /* Everything up to the blend is draw_instance_mask verbatim,
     * run on the CPU with OpenCV: the resampling and thresholding are cheap
     * next to a full-frame round trip, and doing them with the same calls is
     * the only way to be sure the coverage bitmap is the same one. Only the
     * final addWeighted + copyTo, which touches the frame, moves to the GPU. */
    cv::Mat unpadded(mask.size, CV_32F, const_cast<float *>(mask.data.data()));
    cv::Mat raw_cls_mask;
    cv::Mat resized_y, resized_uv, binary_y, binary_uv;
    cv::Rect roi_y, roi_uv;
    try {
        cv::copyMakeBorder(unpadded, raw_cls_mask, 1, 1, 1, 1, cv::BORDER_CONSTANT, {0});
        const cv::Rect box = expand_box(mask.box, float(raw_cls_mask.cols) / (raw_cls_mask.cols - 2),
                                        float(raw_cls_mask.rows) / (raw_cls_mask.rows - 2));

        const int w = std::max(box.width + 1, 1);
        const int h = std::max(box.height + 1, 1);
        const int x0_y = clamp_to(box.x, 0, _width);
        const int y0_y = clamp_to(box.y, 0, _height);
        const int x1_y = clamp_to(box.x + box.width + 1, 0, _width);
        const int y1_y = clamp_to(box.y + box.height + 1, 0, _height);
        const int x0_uv = half_coord(x0_y), y0_uv = half_coord(y0_y);
        const int x1_uv = half_coord(x1_y), y1_uv = half_coord(y1_y);

        roi_y = cv::Rect(x0_y, y0_y, x1_y - x0_y, y1_y - y0_y);
        roi_uv = cv::Rect(x0_uv, y0_uv, x1_uv - x0_uv, y1_uv - y0_uv);
        if (roi_y.width <= 0 || roi_y.height <= 0)
            return true; // nothing of the mask lands on the frame

        cv::resize(raw_cls_mask, resized_y, {w, h});

        cv::threshold(resized_y({cv::Point(x0_y - box.x, y0_y - box.y), cv::Point(x1_y - box.x, y1_y - box.y)}),
                      binary_y, 0.5f, 1.0f, cv::THRESH_BINARY);
        binary_y.convertTo(binary_y, CV_8U);

        if (roi_uv.width > 0 && roi_uv.height > 0) {
            cv::resize(raw_cls_mask, resized_uv, {half_coord(w) + 1, half_coord(h) + 1});
            const int bx_uv = half_coord(box.x), by_uv = half_coord(box.y);
            cv::threshold(resized_uv({cv::Point(x0_uv - bx_uv, y0_uv - by_uv),
                                      cv::Point(x1_uv - bx_uv, y1_uv - by_uv)}),
                          binary_uv, 0.5f, 1.0f, cv::THRESH_BINARY);
            binary_uv.convertTo(binary_uv, CV_8U);
        }
    } catch (const cv::Exception &e) {
        GST_WARNING("gvawatermark GPU renderer: instance mask preparation failed: %s", e.what());
        _last_unsupported = "instance segmentation mask preparation failed";
        return false;
    }

    if (!add_blend(BLEND_P0, roi_y, binary_y, plane_color_y(mask.color)))
        return false;
    if (roi_uv.width > 0 && roi_uv.height > 0 && !add_blend(BLEND_P1, roi_uv, binary_uv, plane_color_uv(mask.color)))
        return false;
    return true;
}

bool RendererGPU::flatten(std::vector<render::Prim> &prims) {
    /* Groups are reset lazily as group_for() hands them out, so the vectors keep
     * their storage; only the used count is dropped here. Pending runs can
     * survive a frame that bailed out mid-flatten, hence the explicit clear. */
    _group_count = {0, 0};
    for (int plane = 0; plane < 2; ++plane) {
        if (_span_pending[plane] == 0)
            continue;
        for (std::vector<SpanRec> &row : _span_rows[plane])
            row.clear();
        _span_pending[plane] = 0;
    }
    _span_records = 0;
    _blob_host.clear();
    _scratch_needed = 0;

    /* Drop the atlas before it can grow without bound; must happen before any
     * offsets are handed out for this frame. */
    if (_atlas_host.size() > ATLAS_MAX_BYTES || _atlas_index.size() > ATLAS_MAX_ENTRIES) {
        _atlas_index.clear();
        _atlas_host.clear();
        _atlas_uploaded = 0;
    }

    for (const render::Prim &prim : prims) {
        if (const auto *rect = std::get_if<render::Rect>(&prim)) {
            if (!flatten_rect(*rect))
                return false;
        } else if (const auto *text = std::get_if<render::Text>(&prim)) {
            if (!flatten_text(*text))
                return false;
        } else if (const auto *line = std::get_if<render::Line>(&prim)) {
            if (!flatten_line(*line))
                return false;
        } else if (const auto *circle = std::get_if<render::Circle>(&prim)) {
            if (!flatten_circle(*circle))
                return false;
        } else if (const auto *polygon = std::get_if<render::Polygon>(&prim)) {
            if (!flatten_polygon(*polygon))
                return false;
        } else if (const auto *blur = std::get_if<render::Blur>(&prim)) {
            if (!flatten_blur(*blur))
                return false;
        } else if (const auto *mask = std::get_if<render::InstanceSegmantationMask>(&prim)) {
            if (!flatten_instance_mask(*mask))
                return false;
        } else {
            /* Not ported, and there is nothing to port to:
             * RendererNV12::draw_semantic_mask throws std::logic_error. */
            _last_unsupported = "semantic segmentation mask";
            return false;
        }
    }

    for (int plane = 0; plane < 2; ++plane)
        flush_spans(plane);
    return true;
}

// ---------------------------------------------------------------------------
// CPU text rasterization
// ---------------------------------------------------------------------------

const RendererGPU::AtlasEntry *RendererGPU::rasterize_text(const std::string &text, int fonttype, double fontscale,
                                                           int thick) {
    char params[64];
    std::snprintf(params, sizeof(params), "\x1f%d\x1f%.6f\x1f%d", fonttype, fontscale, thick);
    std::string key = text + params;

    if (auto it = _atlas_index.find(key); it != _atlas_index.end())
        return &it->second;

    int baseline = 0;
    cv::Size size;
    try {
        size = cv::getTextSize(text, fonttype, fontscale, std::max(1, thick), &baseline);
    } catch (const cv::Exception &e) {
        GST_WARNING("gvawatermark GPU renderer: getTextSize failed: %s", e.what());
        return nullptr;
    }
    if (size.width <= 0 || size.height <= 0)
        return nullptr;

    /* Rasterize into a padded bitmap so thick strokes, accents and descenders
     * cannot be clipped differently than cv::putText would draw them straight
     * into the plane. Verified pixel-identical to a direct putText. */
    const int pad = std::max(1, thick) + 4;
    const int width = size.width + 2 * pad;
    const int height = size.height + baseline + 2 * pad;

    cv::Mat mask = cv::Mat::zeros(height, width, CV_8UC1);
    try {
        cv::putText(mask, text, cv::Point(pad, pad + size.height), fonttype, fontscale, cv::Scalar(255),
                    std::max(1, thick), cv::LINE_8);
    } catch (const cv::Exception &e) {
        GST_WARNING("gvawatermark GPU renderer: putText failed: %s", e.what());
        return nullptr;
    }

    AtlasEntry entry{};
    entry.offset = _atlas_host.size();
    entry.width = width;
    entry.height = height;
    entry.origin_x = -pad;
    entry.origin_y = -(size.height + pad);

    _atlas_host.resize(entry.offset + static_cast<size_t>(width) * height);
    uint8_t *dst = _atlas_host.data() + entry.offset;
    for (int row = 0; row < height; ++row)
        std::memcpy(dst + static_cast<size_t>(row) * width, mask.ptr<uint8_t>(row), width);

    return &_atlas_index.emplace(std::move(key), entry).first->second;
}

// ---------------------------------------------------------------------------
// Device transfers
// ---------------------------------------------------------------------------

bool RendererGPU::sync_atlas() {
    if (_atlas_host.empty())
        return true;

    if (_atlas_buffer_capacity < _atlas_host.size()) {
        size_t capacity = std::max<size_t>(64u << 10, _atlas_buffer_capacity * 2);
        while (capacity < _atlas_host.size())
            capacity *= 2;
        cl_int err = CL_SUCCESS;
        cl_mem buffer = clCreateBuffer(_shared->context, CL_MEM_READ_ONLY, capacity, nullptr, &err);
        if (!buffer || !cl_ok(err, "clCreateBuffer(atlas)"))
            return false;
        if (_atlas_buffer)
            clReleaseMemObject(_atlas_buffer);
        _atlas_buffer = buffer;
        _atlas_buffer_capacity = capacity;
        _atlas_uploaded = 0; // fresh allocation, re-upload everything
    }

    if (_atlas_uploaded < _atlas_host.size()) {
        const size_t bytes = _atlas_host.size() - _atlas_uploaded;
        if (!cl_ok(clEnqueueWriteBuffer(_queue, _atlas_buffer, CL_FALSE, _atlas_uploaded, bytes,
                                        _atlas_host.data() + _atlas_uploaded, 0, nullptr, nullptr),
                   "clEnqueueWriteBuffer(atlas)"))
            return false;
        _atlas_uploaded = _atlas_host.size();
    }
    return true;
}

bool RendererGPU::sync_coeffs() {
    if (_coeff_host.empty())
        return true;

    const size_t bytes = _coeff_host.size() * sizeof(float);
    if (_coeff_buffer_capacity < bytes) {
        size_t capacity = std::max<size_t>(4u << 10, _coeff_buffer_capacity * 2);
        while (capacity < bytes)
            capacity *= 2;
        cl_int err = CL_SUCCESS;
        cl_mem buffer = clCreateBuffer(_shared->context, CL_MEM_READ_ONLY, capacity, nullptr, &err);
        if (!buffer || !cl_ok(err, "clCreateBuffer(coeffs)"))
            return false;
        if (_coeff_buffer)
            clReleaseMemObject(_coeff_buffer);
        _coeff_buffer = buffer;
        _coeff_buffer_capacity = capacity;
        _coeff_uploaded = 0;
    }

    /* Append-only, like the atlas: entries are never rewritten, so only the
     * tail added since the last frame has to go across. */
    if (_coeff_uploaded < bytes) {
        if (!cl_ok(clEnqueueWriteBuffer(_queue, _coeff_buffer, CL_FALSE, _coeff_uploaded, bytes - _coeff_uploaded,
                                        reinterpret_cast<const uint8_t *>(_coeff_host.data()) + _coeff_uploaded, 0,
                                        nullptr, nullptr),
                   "clEnqueueWriteBuffer(coeffs)"))
            return false;
        _coeff_uploaded = bytes;
    }
    return true;
}

bool RendererGPU::sync_blob() {
    if (_blob_host.empty())
        return true;

    if (_blob_buffer_capacity < _blob_host.size()) {
        size_t capacity = std::max<size_t>(256u << 10, _blob_buffer_capacity * 2);
        while (capacity < _blob_host.size())
            capacity *= 2;
        cl_int err = CL_SUCCESS;
        cl_mem buffer = clCreateBuffer(_shared->context, CL_MEM_READ_ONLY, capacity, nullptr, &err);
        if (!buffer || !cl_ok(err, "clCreateBuffer(blob)"))
            return false;
        if (_blob_buffer)
            clReleaseMemObject(_blob_buffer);
        _blob_buffer = buffer;
        _blob_buffer_capacity = capacity;
    }

    return cl_ok(clEnqueueWriteBuffer(_queue, _blob_buffer, CL_FALSE, 0, _blob_host.size(), _blob_host.data(), 0,
                                      nullptr, nullptr),
                 "clEnqueueWriteBuffer(blob)");
}

bool RendererGPU::sync_scratch() {
    if (_scratch_needed == 0 || _scratch_buffer_capacity >= _scratch_needed)
        return true;

    size_t capacity = std::max<size_t>(1u << 16, _scratch_buffer_capacity * 2);
    while (capacity < _scratch_needed)
        capacity *= 2;
    cl_int err = CL_SUCCESS;
    cl_mem buffer = clCreateBuffer(_shared->context, CL_MEM_READ_WRITE, capacity * sizeof(cl_float2), nullptr, &err);
    if (!buffer || !cl_ok(err, "clCreateBuffer(scratch)"))
        return false;
    if (_scratch_buffer)
        clReleaseMemObject(_scratch_buffer);
    _scratch_buffer = buffer;
    _scratch_buffer_capacity = capacity;
    return true;
}

bool RendererGPU::sync_prims() {
    _prims.clear();
    for (int plane = 0; plane < 2; ++plane) {
        for (size_t i = 0; i < _group_count[plane]; ++i) {
            Group &group = _groups[plane][i];
            group.base = static_cast<int>(_prims.size());
            _prims.insert(_prims.end(), group.prims.begin(), group.prims.end());
        }
    }
    if (_prims.empty())
        return true;

    const size_t bytes = _prims.size() * sizeof(Prim);
    if (_prim_buffer_capacity < bytes) {
        size_t capacity = std::max<size_t>(64 * sizeof(Prim), _prim_buffer_capacity * 2);
        while (capacity < bytes)
            capacity *= 2;
        cl_int err = CL_SUCCESS;
        cl_mem buffer = clCreateBuffer(_shared->context, CL_MEM_READ_ONLY, capacity, nullptr, &err);
        if (!buffer || !cl_ok(err, "clCreateBuffer(prims)"))
            return false;
        if (_prim_buffer)
            clReleaseMemObject(_prim_buffer);
        _prim_buffer = buffer;
        _prim_buffer_capacity = capacity;
    }

    return cl_ok(clEnqueueWriteBuffer(_queue, _prim_buffer, CL_FALSE, 0, bytes, _prims.data(), 0, nullptr, nullptr),
                 "clEnqueueWriteBuffer(prims)");
}

bool RendererGPU::get_planes(VASurfaceID surface, cl_mem planes[2]) {
    if (auto it = _plane_cache.find(surface); it != _plane_cache.end()) {
        planes[0] = it->second[0];
        planes[1] = it->second[1];
        return true;
    }

    VASurfaceID id = surface;
    std::array<cl_mem, 2> imported = {nullptr, nullptr};
    for (int plane = 0; plane < 2; ++plane) {
        cl_int err = CL_SUCCESS;
        imported[plane] = _shared->create_from_va(_shared->context, CL_MEM_READ_WRITE, &id, plane, &err);
        if (!imported[plane] || err != CL_SUCCESS) {
            char what[64];
            std::snprintf(what, sizeof(what), "clCreateFromVA_APIMediaSurfaceINTEL(plane %d)", plane);
            cl_ok(err, what);
            for (cl_mem done : imported) {
                if (done)
                    clReleaseMemObject(done);
            }
            _last_unsupported = "surface cannot be imported into OpenCL";
            return false;
        }
    }

    _plane_cache.emplace(surface, imported);
    planes[0] = imported[0];
    planes[1] = imported[1];
    return true;
}

bool RendererGPU::enqueue(const Group &group, cl_mem image, int image_w, int image_h) {
    if (group.prims.empty())
        return true;

    /* The read-modify-write primitives are a pair of dispatches over one
     * scratch buffer rather than a single kernel over a prim array, so they get
     * their own argument shape. Both passes take the same prim, and the queue
     * is in-order, which is the only synchronization the pair needs. */
    if (group.id == BLUR_P0 || group.id == BLUR_P1 || group.id == BLEND_P0 || group.id == BLEND_P1) {
        const bool is_blur = (group.id == BLUR_P0 || group.id == BLUR_P1);
        const Prim &p = group.prims[0];
        const cl_int base = group.base;
        cl_kernel passes[2] = {is_blur ? _k_blur_h : _k_blend_gather, is_blur ? _k_blur_v : _k_blend_scatter};
        // The blur's row pass also covers the kh - 1 rows the column pass reaches into.
        const size_t rows[2] = {static_cast<size_t>(is_blur ? p.a.s[3] + p.b.s[1] - 1 : p.a.s[3]),
                                static_cast<size_t>(p.a.s[3])};
        cl_mem side = is_blur ? _coeff_buffer : _blob_buffer;
        if (!side) {
            GST_WARNING("gvawatermark GPU renderer: %s dispatch without its data buffer",
                        is_blur ? "blur" : "blend");
            return false;
        }

        for (int pass = 0; pass < 2; ++pass) {
            cl_uint arg = 0;
            cl_int err = CL_SUCCESS;
            err |= clSetKernelArg(passes[pass], arg++, sizeof(cl_mem), &image);
            err |= clSetKernelArg(passes[pass], arg++, sizeof(cl_int), &image_w);
            err |= clSetKernelArg(passes[pass], arg++, sizeof(cl_int), &image_h);
            err |= clSetKernelArg(passes[pass], arg++, sizeof(cl_mem), &_scratch_buffer);
            err |= clSetKernelArg(passes[pass], arg++, sizeof(cl_mem), &side);
            err |= clSetKernelArg(passes[pass], arg++, sizeof(cl_mem), &_prim_buffer);
            err |= clSetKernelArg(passes[pass], arg++, sizeof(cl_int), &base);
            if (!cl_ok(err, "clSetKernelArg"))
                return false;

            const size_t global[2] = {static_cast<size_t>(p.a.s[2]), rows[pass]};
            if (global[0] == 0 || global[1] == 0)
                return true;
            if (!cl_ok(clEnqueueNDRangeKernel(_queue, passes[pass], 2, nullptr, global, nullptr, 0, nullptr, nullptr),
                       "clEnqueueNDRangeKernel"))
                return false;
        }
        return true;
    }

    cl_kernel kernel = nullptr;
    bool with_atlas = false;
    switch (group.id) {
    case SPAN_P0:
    case SPAN_P1:
        kernel = _k_spans;
        break;
    case OUTLINE_P0:
    case OUTLINE_P1:
        kernel = _k_outlines;
        break;
    case FILL_P0:
    case FILL_P1:
        kernel = _k_fills;
        break;
    default:
        kernel = _k_masks;
        with_atlas = true;
        break;
    }

    const cl_int base = group.base;
    cl_uint arg = 0;
    cl_int err = CL_SUCCESS;
    err |= clSetKernelArg(kernel, arg++, sizeof(cl_mem), &image);
    err |= clSetKernelArg(kernel, arg++, sizeof(cl_int), &image_w);
    err |= clSetKernelArg(kernel, arg++, sizeof(cl_int), &image_h);
    if (with_atlas)
        err |= clSetKernelArg(kernel, arg++, sizeof(cl_mem), &_atlas_buffer);
    err |= clSetKernelArg(kernel, arg++, sizeof(cl_mem), &_prim_buffer);
    err |= clSetKernelArg(kernel, arg++, sizeof(cl_int), &base);
    if (!cl_ok(err, "clSetKernelArg"))
        return false;

    /* One work item per candidate pixel. Outlines multiply the third dimension
     * by the four edges plus the four corner discs of the rectangle. */
    const bool is_outline = (group.id == OUTLINE_P0 || group.id == OUTLINE_P1);
    const size_t global[3] = {static_cast<size_t>(group.max_w), static_cast<size_t>(group.max_h),
                              group.prims.size() * (is_outline ? 8u : 1u)};
    if (global[0] == 0 || global[1] == 0 || global[2] == 0)
        return true;

    return cl_ok(clEnqueueNDRangeKernel(_queue, kernel, 3, nullptr, global, nullptr, 0, nullptr, nullptr),
                 "clEnqueueNDRangeKernel");
}

// ---------------------------------------------------------------------------
// Frame entry point
// ---------------------------------------------------------------------------

bool RendererGPU::draw_surface(VASurfaceID surface, std::vector<render::Prim> prims) {
    if (surface == VA_INVALID_SURFACE)
        return false;

    /* Imported first, before any of the per-frame work: this is where a surface
     * that does not import the way the kernels expect is caught, and that
     * verdict must be reached while falling back to the CPU renderer is still
     * free. */
    cl_mem planes[2] = {nullptr, nullptr};
    if (!get_planes(surface, planes))
        return false;

    /* Convert primitive colours to the frame's native colour space on the CPU,
     * exactly like Renderer::draw does, so the kernels only ever write bytes. */
    render::convert_prims_color(prims, *_color_converter);

    if (!flatten(prims))
        return false;
    if (!sync_prims())
        return false;
    if (_prims.empty())
        return true;
    if (!sync_atlas() || !sync_coeffs() || !sync_blob() || !sync_scratch())
        return false;

    const cl_uint n_planes = 2;
    if (!cl_ok(_shared->acquire(_queue, n_planes, planes, 0, nullptr, nullptr),
               "clEnqueueAcquireVA_APIMediaSurfacesINTEL"))
        return false;

    /* The queue is in-order, so the groups of one plane execute in the order
     * they were scheduled, which is the CPU renderer's draw order wherever that
     * is observable. The two planes are separate images and need no ordering
     * against each other. */
    bool ok = true;
    for (int plane = 0; plane < 2 && ok; ++plane) {
        for (size_t i = 0; i < _group_count[plane] && ok; ++i)
            ok = enqueue(_groups[plane][i], planes[plane], plane_width(plane), plane_height(plane));
    }

    /* Release makes the writes visible to VA; the following finish makes them
     * visible to the downstream element. */
    cl_ok(_shared->release(_queue, n_planes, planes, 0, nullptr, nullptr),
          "clEnqueueReleaseVA_APIMediaSurfacesINTEL");
    if (!cl_ok(clFinish(_queue), "clFinish"))
        return false;

    if (!ok)
        _last_unsupported = "OpenCL enqueue failed";
    return ok;
}

#endif // ENABLE_GVAWATERMARK_GPU
