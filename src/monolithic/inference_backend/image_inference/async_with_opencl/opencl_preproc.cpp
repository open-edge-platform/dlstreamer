/*******************************************************************************
 * Copyright (C) 2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#include "opencl_preproc.h"
#include "opencl_preproc_kernels.h"

#include "dlstreamer/d3d11/device_lock.h"
#include "dlstreamer/d3d11/opencl_sharing.h"
#include "inference_backend/logger.h"
#include "inference_backend/opencl_interop_queue.h"

#include <gst/video/video.h>

#include <d3d11.h>

#include <array>
#include <stdexcept>
#include <string>
#include <unordered_map>

using namespace InferenceBackend;
using dlstreamer::D3D11DeviceLock;

namespace {

// Work-group shape: wide along the rows the tensor planes are written in.
constexpr size_t TILE_W = 32;
constexpr size_t TILE_H = 8;

void check_cl(cl_int err, const char *what) {
    if (err != CL_SUCCESS)
        throw std::runtime_error(std::string("OpenCL preprocessing: ") + what +
                                 " failed (cl_err=" + std::to_string(err) + ")");
}

bool has_extension(cl_device_id dev, const char *name) {
    size_t size = 0;
    if (clGetDeviceInfo(dev, CL_DEVICE_EXTENSIONS, 0, nullptr, &size) != CL_SUCCESS || size == 0)
        return false;
    std::string extensions(size, '\0');
    if (clGetDeviceInfo(dev, CL_DEVICE_EXTENSIONS, size, extensions.data(), nullptr) != CL_SUCCESS)
        return false;
    return (" " + extensions + " ").find(std::string(" ") + name + " ") != std::string::npos;
}

cl_float4 make_float4(double x, double y, double z, double w) {
    cl_float4 v;
    v.s[0] = static_cast<float>(x);
    v.s[1] = static_cast<float>(y);
    v.s[2] = static_cast<float>(z);
    v.s[3] = static_cast<float>(w);
    return v;
}

// Rows of the YUV -> RGB matrix acting on normalized (Y, U, V, 1), for the colorimetry GStreamer gives
// a frame of this size by default. GstD3D11Converter is set up from the same defaults by the D3D11
// backends, so all of them convert colours alike.
std::array<cl_float4, 3> yuv_to_rgb_rows(uint32_t width, uint32_t height) {
    GstVideoInfo info;
    gst_video_info_set_format(&info, GST_VIDEO_FORMAT_NV12, width, height);
    gdouble kr = 0.299, kb = 0.114;
    if (!gst_video_color_matrix_get_Kr_Kb(info.colorimetry.matrix, &kr, &kb)) {
        kr = 0.299;
        kb = 0.114;
    }
    const double kg = 1.0 - kr - kb;
    const bool full_range = info.colorimetry.range == GST_VIDEO_COLOR_RANGE_0_255;
    const double y_scale = full_range ? 1.0 : 255.0 / 219.0;
    const double y_offset = full_range ? 0.0 : 16.0 / 255.0;
    const double c_scale = full_range ? 1.0 : 255.0 / 224.0;
    const double c_offset = 128.0 / 255.0;

    auto row = [&](double u, double v) {
        return make_float4(y_scale, u * c_scale, v * c_scale, -(y_scale * y_offset + (u + v) * c_scale * c_offset));
    };
    return {row(0.0, 2.0 * (1.0 - kr)),                                    // R
            row(-2.0 * (1.0 - kb) * kb / kg, -2.0 * (1.0 - kr) * kr / kg), // G
            row(2.0 * (1.0 - kb), 0.0)};                                   // B
}

// The images through which the kernel reads one shared texture: the Y and UV planes of an NV12
// texture, or the one plane of a packed RGBA/BGRA texture.
struct Import {
    std::array<cl_mem, 2> planes = {nullptr, nullptr};
    cl_uint count = 0;
    bool nv12 = false;
};

} // namespace

// -- OpenCLBufferPool --------------------------------------------------------

OpenCLBufferPool::OpenCLBufferPool(cl_context context, size_t size) : _context(context), _size(size) {
    clRetainContext(_context);
}

OpenCLBufferPool::~OpenCLBufferPool() {
    for (cl_mem buffer : _free)
        clReleaseMemObject(buffer);
    clReleaseContext(_context);
}

cl_mem OpenCLBufferPool::acquire() {
    {
        std::lock_guard lk(_mutex);
        if (!_free.empty()) {
            cl_mem buffer = _free.back();
            _free.pop_back();
            return buffer;
        }
    }
    cl_int err = CL_SUCCESS;
    cl_mem buffer = clCreateBuffer(_context, CL_MEM_READ_WRITE, _size, nullptr, &err);
    check_cl(err, "clCreateBuffer");
    return buffer;
}

void OpenCLBufferPool::release(cl_mem buffer) {
    std::lock_guard lk(_mutex);
    _free.push_back(buffer);
}

// -- OpenCLPreproc -----------------------------------------------------------

struct OpenCLPreproc::Impl {
    cl_context context = nullptr;
    cl_device_id device = nullptr;
    cl_command_queue queue = nullptr;
    cl_program program = nullptr;
    cl_kernel k_nv12 = nullptr;
    cl_kernel k_rgba = nullptr;
    bool fixed_tiles = false;

    GstD3D11Device *d3d11_device = nullptr;
    clCreateFromD3D11Texture2DKHR_fn create_from_texture2d = nullptr;
    clEnqueueAcquireD3D11ObjectsKHR_fn acquire = nullptr;
    clEnqueueReleaseD3D11ObjectsKHR_fn release = nullptr;

    uint32_t dst_width = 0;
    uint32_t dst_height = 0;
    bool bgr = true;
    std::shared_ptr<OpenCLBufferPool> output_pool;

    // Keyed by texture. The textures come from a fixed pool, so this stops growing after the first
    // few frames; and each import holds a reference to its texture, so a key can't be reused by
    // another texture.
    std::unordered_map<ID3D11Texture2D *, Import> imports;

    std::array<cl_float4, 3> yuv_rows = {};
    uint32_t yuv_rows_width = 0;
    uint32_t yuv_rows_height = 0;

    std::mutex mutex;

    Impl(cl_context ctx, GstD3D11Device *dev, uint32_t width, uint32_t height, bool is_bgr)
        : dst_width(width), dst_height(height), bgr(is_bgr) {
        if (!ctx || !dev)
            throw std::invalid_argument("OpenCL preprocessing: null cl_context or D3D11 device");
        clRetainContext(ctx);
        context = ctx;
        d3d11_device = static_cast<GstD3D11Device *>(gst_object_ref(dev));
    }

    // Separate from the constructor so that the destructor cleans up after a failure half-way.
    void init() {
        check_cl(clGetContextInfo(context, CL_CONTEXT_DEVICES, sizeof(device), &device, nullptr),
                 "clGetContextInfo(CL_CONTEXT_DEVICES)");
        cl_platform_id platform = nullptr;
        check_cl(clGetDeviceInfo(device, CL_DEVICE_PLATFORM, sizeof(platform), &platform, nullptr),
                 "clGetDeviceInfo(CL_DEVICE_PLATFORM)");

        const dlstreamer::D3D11SharingFunctions sharing(platform);
        create_from_texture2d = sharing.create_from_texture2d;
        acquire = sharing.acquire;
        release = sharing.release;
        if (!create_from_texture2d || !acquire || !release)
            throw std::runtime_error("OpenCL preprocessing: cl_khr_d3d11_sharing is not available");
        if (!has_extension(device, "cl_intel_d3d11_nv12_media_sharing"))
            GVA_WARNING("OpenCL preprocessing: no cl_intel_d3d11_nv12_media_sharing, NV12 frames will fail");

        cl_int err = CL_SUCCESS;
        queue = CreateInteropCommandQueue(context, device, &err);
        check_cl(err, "clCreateCommandQueue");

        build_program();

        output_pool = std::make_shared<OpenCLBufferPool>(context, static_cast<size_t>(dst_width) * dst_height * 3);

        char name[256] = {};
        clGetDeviceInfo(device, CL_DEVICE_NAME, sizeof(name) - 1, name, nullptr);
        GVA_INFO("OpenCL preprocessing on '%s': %ux%u planar %s", name, dst_width, dst_height, bgr ? "BGR" : "RGB");
    }

    ~Impl() {
        {
            // Releasing an import uses the D3D11 immediate context.
            D3D11DeviceLock lock(d3d11_device);
            for (auto &entry : imports) {
                for (cl_uint i = 0; i < entry.second.count; ++i)
                    clReleaseMemObject(entry.second.planes[i]);
            }
            imports.clear();
        }
        if (k_nv12)
            clReleaseKernel(k_nv12);
        if (k_rgba)
            clReleaseKernel(k_rgba);
        if (program)
            clReleaseProgram(program);
        if (queue)
            clReleaseCommandQueue(queue);
        output_pool.reset();
        if (context)
            clReleaseContext(context);
        if (d3d11_device)
            gst_object_unref(d3d11_device);
    }

    void build_program() {
        cl_int err = CL_SUCCESS;
        const char *source = opencl_preproc::KERNELS;
        program = clCreateProgramWithSource(context, 1, &source, nullptr, &err);
        check_cl(err, "clCreateProgramWithSource");
        err = clBuildProgram(program, 1, &device, "-cl-fast-relaxed-math", nullptr, nullptr);
        if (err != CL_SUCCESS) {
            size_t log_size = 0;
            clGetProgramBuildInfo(program, device, CL_PROGRAM_BUILD_LOG, 0, nullptr, &log_size);
            std::string log(log_size, '\0');
            clGetProgramBuildInfo(program, device, CL_PROGRAM_BUILD_LOG, log_size, log.data(), nullptr);
            GVA_ERROR("OpenCL preprocessing: kernel build failed: %s", log.c_str());
            check_cl(err, "clBuildProgram");
        }
        k_nv12 = clCreateKernel(program, "pp_nv12", &err);
        check_cl(err, "clCreateKernel(pp_nv12)");
        k_rgba = clCreateKernel(program, "pp_rgba", &err);
        check_cl(err, "clCreateKernel(pp_rgba)");

        fixed_tiles = true;
        for (cl_kernel kernel : {k_nv12, k_rgba}) {
            size_t max_items = 0;
            if (clGetKernelWorkGroupInfo(kernel, device, CL_KERNEL_WORK_GROUP_SIZE, sizeof(max_items), &max_items,
                                         nullptr) != CL_SUCCESS ||
                max_items < TILE_W * TILE_H)
                fixed_tiles = false;
        }
    }

    Import &get_import(ID3D11Texture2D *texture) {
        auto it = imports.find(texture);
        if (it != imports.end())
            return it->second;

        D3D11_TEXTURE2D_DESC desc = {};
        texture->GetDesc(&desc);
        Import import;
        switch (desc.Format) {
        case DXGI_FORMAT_NV12:
            import.nv12 = true;
            import.count = 2;
            break;
        case DXGI_FORMAT_R8G8B8A8_UNORM:
        case DXGI_FORMAT_B8G8R8A8_UNORM:
        case DXGI_FORMAT_B8G8R8X8_UNORM:
            import.count = 1;
            break;
        default:
            throw std::runtime_error("OpenCL preprocessing: unsupported texture format " + std::to_string(desc.Format));
        }

        {
            // cl_intel_d3d11_nv12_media_sharing takes subresource 0 of a single NV12 texture as the
            // Y plane and 1 as the UV plane.
            D3D11DeviceLock lock(d3d11_device);
            for (cl_uint plane = 0; plane < import.count; ++plane) {
                cl_int err = CL_SUCCESS;
                import.planes[plane] = create_from_texture2d(context, CL_MEM_READ_ONLY, texture, plane, &err);
                if (err != CL_SUCCESS || !import.planes[plane]) {
                    for (cl_uint done = 0; done < plane; ++done)
                        clReleaseMemObject(import.planes[done]);
                    check_cl(err == CL_SUCCESS ? CL_INVALID_MEM_OBJECT : err, "clCreateFromD3D11Texture2DKHR");
                }
            }
        }
        GVA_DEBUG("OpenCL preprocessing: imported texture %p (%ux%u, format %u), %zu imported",
                  static_cast<void *>(texture), desc.Width, desc.Height, desc.Format, imports.size() + 1);
        return imports.emplace(texture, import).first->second;
    }

    cl_int enqueue(const Import &import, uint32_t frame_width, uint32_t frame_height, const OpenCLPreprocGeometry &g,
                   cl_mem output) {
        const cl_int dst_w = static_cast<cl_int>(dst_width);
        const cl_int dst_h = static_cast<cl_int>(dst_height);
        cl_int4 dst_rect;
        dst_rect.s[0] = g.dst_x;
        dst_rect.s[1] = g.dst_y;
        dst_rect.s[2] = g.dst_w;
        dst_rect.s[3] = g.dst_h;
        const cl_float4 src_map =
            make_float4(g.src_x, g.src_y, g.dst_w > 0 ? g.src_w / g.dst_w : 0.0, g.dst_h > 0 ? g.src_h / g.dst_h : 0.0);
        const cl_float4 frame_clamp = make_float4(0.5, 0.5, frame_width - 0.5, frame_height - 0.5);
        const cl_float4 fill = bgr ? make_float4(g.fill_rgb[2], g.fill_rgb[1], g.fill_rgb[0], 0)
                                   : make_float4(g.fill_rgb[0], g.fill_rgb[1], g.fill_rgb[2], 0);

        cl_kernel kernel = import.nv12 ? k_nv12 : k_rgba;
        cl_uint arg = 0;
        cl_int err = CL_SUCCESS;
        err |= clSetKernelArg(kernel, arg++, sizeof(cl_mem), &import.planes[0]);
        if (import.nv12)
            err |= clSetKernelArg(kernel, arg++, sizeof(cl_mem), &import.planes[1]);
        err |= clSetKernelArg(kernel, arg++, sizeof(cl_mem), &output);
        err |= clSetKernelArg(kernel, arg++, sizeof(cl_int), &dst_w);
        err |= clSetKernelArg(kernel, arg++, sizeof(cl_int), &dst_h);
        err |= clSetKernelArg(kernel, arg++, sizeof(cl_int4), &dst_rect);
        err |= clSetKernelArg(kernel, arg++, sizeof(cl_float4), &src_map);
        if (import.nv12) {
            if (frame_width != yuv_rows_width || frame_height != yuv_rows_height) {
                yuv_rows = yuv_to_rgb_rows(frame_width, frame_height);
                yuv_rows_width = frame_width;
                yuv_rows_height = frame_height;
            }
            const cl_float4 chroma_clamp =
                make_float4(0.5, 0.5, (frame_width + 1) / 2 - 0.5, (frame_height + 1) / 2 - 0.5);
            err |= clSetKernelArg(kernel, arg++, sizeof(cl_float4), &frame_clamp);
            err |= clSetKernelArg(kernel, arg++, sizeof(cl_float4), &chroma_clamp);
            // Rows in tensor channel order.
            for (int c = 0; c < 3; ++c)
                err |= clSetKernelArg(kernel, arg++, sizeof(cl_float4), &yuv_rows[bgr ? 2 - c : c]);
        } else {
            const cl_int bgr_out = bgr ? 1 : 0;
            err |= clSetKernelArg(kernel, arg++, sizeof(cl_float4), &frame_clamp);
            err |= clSetKernelArg(kernel, arg++, sizeof(cl_int), &bgr_out);
        }
        err |= clSetKernelArg(kernel, arg++, sizeof(cl_float4), &fill);
        if (err != CL_SUCCESS)
            return CL_INVALID_KERNEL_ARGS;

        const size_t tile[2] = {TILE_W, TILE_H};
        const size_t global[2] = {fixed_tiles ? (dst_width + TILE_W - 1) / TILE_W * TILE_W : dst_width,
                                  fixed_tiles ? (dst_height + TILE_H - 1) / TILE_H * TILE_H : dst_height};
        return clEnqueueNDRangeKernel(queue, kernel, 2, nullptr, global, fixed_tiles ? tile : nullptr, 0, nullptr,
                                      nullptr);
    }
};

OpenCLPreproc::OpenCLPreproc(cl_context context, GstD3D11Device *device, uint32_t dst_width, uint32_t dst_height,
                             bool bgr)
    : _impl(std::make_unique<Impl>(context, device, dst_width, dst_height, bgr)) {
    _impl->init();
}

OpenCLPreproc::~OpenCLPreproc() = default;

cl_context OpenCLPreproc::context() const {
    return _impl->context;
}

const std::shared_ptr<OpenCLBufferPool> &OpenCLPreproc::output_pool() const {
    return _impl->output_pool;
}

void OpenCLPreproc::convert(ID3D11Texture2D *texture, uint32_t frame_width, uint32_t frame_height,
                            const OpenCLPreprocGeometry &geometry, cl_mem output) {
    if (!texture || !output)
        throw std::invalid_argument("OpenCL preprocessing: null texture or output");

    std::lock_guard lk(_impl->mutex);
    Import &import = _impl->get_import(texture);

    {
        // With user sync this does not wait for the GPU, but it still goes through the D3D11 device.
        D3D11DeviceLock lock(_impl->d3d11_device);
        check_cl(_impl->acquire(_impl->queue, import.count, import.planes.data(), 0, nullptr, nullptr),
                 "clEnqueueAcquireD3D11ObjectsKHR");
    }

    const cl_int enqueued = _impl->enqueue(import, frame_width, frame_height, geometry, output);
    // Released even if the kernel was not enqueued: a texture left acquired fails every later acquire.
    // No device lock: releasing a shared texture does not use the D3D11 context. It does wait for the
    // queue, i.e. for the kernel.
    const cl_int released = _impl->release(_impl->queue, import.count, import.planes.data(), 0, nullptr, nullptr);
    check_cl(enqueued, "clEnqueueNDRangeKernel");
    check_cl(released, "clEnqueueReleaseD3D11ObjectsKHR");
    check_cl(clFinish(_impl->queue), "clFinish");
}
