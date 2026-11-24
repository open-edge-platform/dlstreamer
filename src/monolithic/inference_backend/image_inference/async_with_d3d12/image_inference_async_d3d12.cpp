/*******************************************************************************
 * Copyright (C) 2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#include "image_inference_async_d3d12.h"

#include "inference_backend/image_inference.h"
#include "inference_backend/input_image_layer_descriptor.h"
#include "safe_arithmetic.hpp"
#include "utils.h"

#include "d3d12_converter.h"
#include "d3d12_surface_sharing.h"
#include "dlstreamer/d3d12/context.h"

#include <cstring>
#include <string>
#include <utility>

using namespace InferenceBackend;

namespace {

constexpr size_t MAX_CACHED_CONVERTERS = 8;

struct GstBufferDeleter {
    void operator()(GstBuffer *b) const {
        if (b)
            gst_buffer_unref(b);
    }
};
using GstBufferPtr = std::unique_ptr<GstBuffer, GstBufferDeleter>;

DXGI_FORMAT FourCCToDXGI(int fourcc) {
    switch (fourcc) {
    case FourCC::FOURCC_BGRP:
    case FourCC::FOURCC_BGRA:
    case FourCC::FOURCC_BGRX:
        return DXGI_FORMAT_B8G8R8A8_UNORM;
    case FourCC::FOURCC_RGBP:
    case FourCC::FOURCC_RGBA:
    case FourCC::FOURCC_RGBX:
        return DXGI_FORMAT_R8G8B8A8_UNORM;
    case FourCC::FOURCC_NV12:
        return DXGI_FORMAT_NV12;
    default:
        return DXGI_FORMAT_UNKNOWN;
    }
}

int DXGIToFourCC(DXGI_FORMAT format) {
    switch (format) {
    case DXGI_FORMAT_B8G8R8A8_UNORM:
        return FourCC::FOURCC_BGRA;
    case DXGI_FORMAT_R8G8B8A8_UNORM:
        return FourCC::FOURCC_RGBA;
    case DXGI_FORMAT_NV12:
        return FourCC::FOURCC_NV12;
    default:
        return FourCC::FOURCC_BGRA;
    }
}

const InputImageLayerDesc::Ptr
getImagePreProcInfo(const std::map<std::string, InferenceBackend::InputLayerDesc::Ptr> &input_preprocessors) {
    const auto image_it = input_preprocessors.find("image");
    if (image_it != input_preprocessors.cend()) {
        const auto description = image_it->second;
        if (description) {
            return description->input_image_preroc_params;
        }
    }
    return nullptr;
}

void AllocateTexturePool(GstD3D12Device *device, const D3D12_RESOURCE_DESC &desc, size_t count,
                         std::vector<GstMemory *> &out) {
    D3D12_HEAP_PROPERTIES heap_props = {};
    heap_props.Type = D3D12_HEAP_TYPE_DEFAULT;

    std::vector<GstMemory *> allocated;
    try {
        for (size_t i = 0; i < count; i++) {
            GstMemory *mem = gst_d3d12_allocator_alloc(nullptr, device, &heap_props, D3D12_HEAP_FLAG_NONE, &desc,
                                                       D3D12_RESOURCE_STATE_COMMON, nullptr);
            if (!mem)
                throw std::runtime_error("D3D12: Failed to allocate texture");
            allocated.push_back(mem);
        }
    } catch (...) {
        for (auto *m : allocated)
            gst_memory_unref(m);
        throw;
    }
    out.insert(out.end(), allocated.begin(), allocated.end());
}

} // namespace

// -- TexturePool --------------------------------------------------------------

GstMemory *ImageInferenceAsyncD3D12::TexturePool::Acquire() {
    std::unique_lock lk(mutex);
    cv.wait(lk, [this] { return !free.empty(); });
    GstMemory *mem = free.back();
    free.pop_back();
    return mem;
}

void ImageInferenceAsyncD3D12::TexturePool::Release(GstMemory *mem) {
    {
        std::lock_guard lk(mutex);
        free.push_back(mem);
    }
    cv.notify_one();
}

// -- Constructor / Destructor -------------------------------------------------

ImageInferenceAsyncD3D12::ImageInferenceAsyncD3D12(const InferenceBackend::InferenceConfig &config,
                                                   dlstreamer::ContextPtr d3d12_context, ImageInference::Ptr inference,
                                                   ErrorHandlingFunc error_handler)
    : _inference(inference), _device_context_storage(d3d12_context), _error_handler(std::move(error_handler)) {

    _gst_device = static_cast<GstD3D12Device *>(d3d12_context->handle(dlstreamer::D3D12Context::key::d3d12_device));
    if (!_gst_device)
        throw std::invalid_argument("D3D12: context does not carry a GstD3D12Device");

    size_t width = 0, height = 0, batch_size = 0;
    int format = 0, memory_type = 0;
    inference->GetModelImageInputInfo(width, height, batch_size, format, memory_type);

    _surface_sharing = (static_cast<MemoryType>(memory_type) == MemoryType::D3D12);

    _dst_fourcc = format;
    _dst_format = FourCCToDXGI(format);
    if (_dst_format == DXGI_FORMAT_UNKNOWN) {
        GVA_WARNING("D3D12: Format %d has no DXGI equivalent, using BGRA", format);
        _dst_format = DXGI_FORMAT_B8G8R8A8_UNORM;
    }
    if (_surface_sharing && _dst_format == DXGI_FORMAT_NV12)
        throw std::runtime_error("D3D12 surface sharing supports BGRA/RGBA model inputs only");

    _dst_width = width;
    _dst_height = height;

    _dst_desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    _dst_desc.Width = _dst_width;
    _dst_desc.Height = _dst_height;
    _dst_desc.DepthOrArraySize = 1;
    _dst_desc.MipLevels = 1;
    _dst_desc.Format = _dst_format;
    _dst_desc.SampleDesc.Count = 1;
    _dst_desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    // SIMULTANEOUS_ACCESS: required by GstD3D12Memory for CPU readback and lets the
    // texture decay to COMMON between the DIRECT (convert) and COPY (share) queues.
    _dst_desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET | D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS;

    D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp = {};
    gst_d3d12_device_get_device_handle(_gst_device)
        ->GetCopyableFootprints(&_dst_desc, 0, 1, 0, &fp, nullptr, nullptr, nullptr);
    _dst_row_pitch = fp.Footprint.RowPitch;

    size_t pool_size = safe_mul(batch_size, _inference->GetNireq());
    AllocateTexturePool(_gst_device, _dst_desc, pool_size, _tex_pool.free);

    _submit_queue_max = pool_size;
    _worker_running = true;
    _worker_thread = std::thread([this]() { WorkerLoop(); });

    GVA_INFO("D3D12 preprocessing: %ux%u format=%u (nireq=%u, pool=%zu, surface_sharing=%d)", _dst_width, _dst_height,
             _dst_format, _inference->GetNireq(), pool_size, _surface_sharing);
}

ImageInferenceAsyncD3D12::~ImageInferenceAsyncD3D12() {
    {
        // Under the lock, or the worker can miss the wakeup between its predicate
        // check and its wait, and join() below never returns.
        std::lock_guard lk(_queue_mutex);
        _worker_running = false;
    }
    _queue_cv.notify_one();
    if (_worker_thread.joinable())
        _worker_thread.join();

    // Wait for in-flight requests while the pools are alive: releasing their images returns
    // textures to the pools. stop() normally flushed already; after a failed flush, the
    // inference destructor would otherwise release them after the unref loops below.
    try {
        if (_inference)
            _inference->Flush();
    } catch (const std::exception &e) {
        GVA_ERROR("D3D12: Flush on teardown failed: %s", e.what());
    }

    // The shared-buffer copies reference the pooled textures; drain them first.
    _surface_sharing_obj.reset();
    // Converters wait for their last submission before releasing command allocators.
    _converters.clear();
    for (auto &[device, res] : _cross_device_resources)
        res->converters.clear();

    for (auto *mem : _tex_pool.free)
        gst_memory_unref(mem);

    for (auto &[device, res] : _cross_device_resources) {
        for (auto *mem : res->tex_pool.free)
            gst_memory_unref(mem);
        if (res->device)
            gst_object_unref(res->device);
    }
}

// -- Converter management -----------------------------------------------------

D3D12Converter &ImageInferenceAsyncD3D12::GetConverter(ConverterMap &converters, GstD3D12Device *device,
                                                       uint32_t src_width, uint32_t src_height,
                                                       DXGI_FORMAT src_format) {
    std::lock_guard lk(_converter_mutex);
    const ConverterKey key{src_width, src_height, src_format};
    auto it = converters.find(key);
    if (it != converters.end())
        return *it->second;

    // Bounded, since a long-running instance can see many sizes over time.
    if (converters.size() >= MAX_CACHED_CONVERTERS)
        converters.clear();
    auto &converter = converters[key];
    converter = std::make_unique<D3D12Converter>(device, src_width, src_height, src_format, _dst_width, _dst_height,
                                                 _dst_format);
    GVA_INFO("D3D12: Created converter on device %p: %ux%u (fmt=%u) -> %ux%u", static_cast<void *>(device), src_width,
             src_height, src_format, _dst_width, _dst_height);
    return *converter;
}

ImageInferenceAsyncD3D12::CrossDeviceResources &
ImageInferenceAsyncD3D12::GetCrossDeviceResources(GstD3D12Device *src_gst_device) {
    auto *src_device = gst_d3d12_device_get_device_handle(src_gst_device);
    std::lock_guard lk(_cross_device_mutex);
    auto it = _cross_device_resources.find(src_device);
    if (it != _cross_device_resources.end())
        return *it->second;

    auto res = std::make_unique<CrossDeviceResources>();
    res->device = static_cast<GstD3D12Device *>(gst_object_ref(src_gst_device));
    AllocateTexturePool(res->device, _dst_desc, _submit_queue_max, res->tex_pool.free);

    GVA_INFO("D3D12: Created %zu textures for source device %p", _submit_queue_max, static_cast<void *>(src_device));
    auto &ref = *res;
    _cross_device_resources[src_device] = std::move(res);
    return ref;
}

D3D12SurfaceSharing *ImageInferenceAsyncD3D12::GetOrCreateSurfaceSharing(void *cl_ctx) {
    std::lock_guard lk(_surface_sharing_mutex);
    if (!_surface_sharing_obj)
        _surface_sharing_obj = std::make_unique<D3D12SurfaceSharing>(static_cast<cl_context>(cl_ctx));
    return _surface_sharing_obj.get();
}

// -- SubmitImage helpers ------------------------------------------------------

GstBuffer *ImageInferenceAsyncD3D12::WrapSourceFrame(const Image &src_image, GstD3D12Device *device) {
    GstMemory *mem = nullptr;
    if (src_image.gst_d3d12_memory) {
        // Preferred: reuse the upstream GstD3D12Memory so the decoder's fence and
        // array-slice/subresource layout travel with it.
        mem = gst_memory_ref(static_cast<GstMemory *>(src_image.gst_d3d12_memory));
    } else {
        if (!src_image.d3d12_resource)
            throw std::invalid_argument("WrapSourceFrame: null resource");
        D3D12_RESOURCE_DESC desc = src_image.d3d12_resource->GetDesc();
        // Subresource index -> array slice for plane 0 (mip levels are always 1 here).
        guint array_slice = desc.MipLevels ? src_image.d3d12_subresource_index / desc.MipLevels : 0;
        mem =
            gst_d3d12_allocator_alloc_wrapped(nullptr, device, src_image.d3d12_resource, array_slice, nullptr, nullptr);
        if (!mem)
            throw std::runtime_error("WrapSourceFrame: Failed to wrap D3D12 resource");
    }

    GstBuffer *buf = gst_buffer_new();
    gst_buffer_append_memory(buf, mem);
    return buf;
}

void ImageInferenceAsyncD3D12::BuildSurfaceSharingImage(IFrameBase::Ptr &frame, GstD3D12Device *device,
                                                        D3D12Converter &converter, GstMemory *dst_mem,
                                                        uint64_t fence_value, ReleaseFunc release_mem) {
    auto *dmem = GST_D3D12_MEMORY_CAST(dst_mem);
    ID3D12Resource *tex = gst_d3d12_memory_get_resource_handle(dmem);
    guint subresource = 0;
    gst_d3d12_memory_get_subresource_index(dmem, 0, &subresource);

    auto image = std::unique_ptr<Image>(new Image());
    image->type = MemoryType::D3D12;
    image->width = _dst_width;
    image->height = _dst_height;
    image->format = _dst_fourcc;
    image->d3d12_resource = tex;
    image->d3d12_subresource_index = subresource;
    image->gst_d3d12_device = device;
    image->gst_d3d12_memory = dst_mem;

    // Path: texture -> shared D3D12 buffer -> CL external-memory buffer. The
    // consumer (OV) passes its cl_context through the callback; this class knows
    // nothing about OV. The copy is ordered after the conversion via its fence.
    ID3D12Fence *fence = converter.fence();
    uint32_t w = _dst_width, h = _dst_height;
    image->create_cl_buffer = [this, device, tex, subresource, w, h, fence, fence_value](void *cl_ctx) -> void * {
        auto *ss = GetOrCreateSurfaceSharing(cl_ctx);
        return ss->create_cl_buffer(device, tex, subresource, w, h, fence, fence_value);
    };

    frame->SetImage(std::shared_ptr<Image>(image.release(), [release_mem, dst_mem](Image *img) {
        release_mem(dst_mem);
        delete img;
    }));
}

void ImageInferenceAsyncD3D12::BuildSystemMemoryImage(IFrameBase::Ptr &frame, GstMemory *dst_mem,
                                                      ReleaseFunc release_mem) {
    // GstD3D12Memory CPU map = readback into a staging buffer on the COPY queue, waiting
    // for the fence the converter attached to this memory.
    GstMapInfo map_info;
    if (!gst_memory_map(dst_mem, &map_info, GST_MAP_READ))
        throw std::runtime_error("D3D12: Failed to map converted texture");

    auto image = std::unique_ptr<Image>(new Image());
    image->type = MemoryType::SYSTEM;
    image->width = _dst_width;
    image->height = _dst_height;
    image->format = DXGIToFourCC(_dst_format);
    image->planes[0] = map_info.data;
    image->stride[0] = _dst_row_pitch;
    if (_dst_format == DXGI_FORMAT_NV12) {
        D3D12_PLACED_SUBRESOURCE_FOOTPRINT fps[2] = {};
        gst_d3d12_device_get_device_handle(_gst_device)
            ->GetCopyableFootprints(&_dst_desc, 0, 2, 0, fps, nullptr, nullptr, nullptr);
        image->planes[1] = map_info.data + fps[1].Offset;
        image->stride[1] = fps[1].Footprint.RowPitch;
    }

    frame->SetImage(std::shared_ptr<Image>(image.release(), [release_mem, dst_mem, map_info](Image *img) mutable {
        gst_memory_unmap(dst_mem, &map_info);
        release_mem(dst_mem);
        delete img;
    }));
}

void ImageInferenceAsyncD3D12::ConvertAndBuildImage(
    IFrameBase::Ptr &frame, GstD3D12Device *conv_device, D3D12Converter &converter, GstMemory *dst_mem,
    ReleaseFunc release_mem, const std::map<std::string, InputLayerDesc::Ptr> &input_preprocessors) {

    const Image &src_image = *frame->GetImage();
    // ROI for inference-region=1; read before SetImage(nullptr) drops the source image.
    const Rectangle<uint32_t> roi = src_image.rect;

    GstBufferPtr src_buf(WrapSourceFrame(src_image, conv_device));
    GstBufferPtr dst_buf(gst_buffer_new());
    gst_buffer_append_memory(dst_buf.get(), gst_memory_ref(dst_mem));

    uint64_t fence_value = converter.Convert(src_buf.get(), dst_buf.get(), getImagePreProcInfo(input_preprocessors),
                                             frame->GetImageTransformationParams(), roi);

    src_buf.reset();
    dst_buf.reset();
    frame->SetImage(nullptr);

    if (_surface_sharing)
        BuildSurfaceSharingImage(frame, conv_device, converter, dst_mem, fence_value, release_mem);
    else
        BuildSystemMemoryImage(frame, dst_mem, release_mem);
}

// -- Worker thread ------------------------------------------------------------

void ImageInferenceAsyncD3D12::WorkerLoop() {
    while (true) {
        SubmitTask task;
        {
            std::unique_lock lk(_queue_mutex);
            _queue_cv.wait(lk, [this] { return !_submit_queue.empty() || !_worker_running.load(); });
            if (!_worker_running.load() && _submit_queue.empty())
                break;
            task = std::move(_submit_queue.front());
            _submit_queue.pop();
            // Flush() must also wait for this task, not only for the queue to drain:
            // otherwise EOS can overtake the frame that is being submitted.
            _worker_busy = true;
        }
        _queue_not_full_cv.notify_one();

        IFrameBase::Ptr frame = task.frame;
        try {
            _inference->SubmitImage(std::move(task.frame), task.input_preprocessors);
        } catch (const std::exception &e) {
            // The frame never reached an infer request, so no completion will ever arrive for
            // it. Report it as failed so the element pushes its buffer instead of waiting.
            GVA_ERROR("D3D12 worker: OV SubmitImage failed: %s", e.what());
            if (_error_handler)
                _error_handler({frame});
        }
        frame.reset();

        {
            std::lock_guard lk(_queue_mutex);
            _worker_busy = false;
        }
        _queue_empty_cv.notify_all();
    }
}

// -- SubmitImage --------------------------------------------------------------

void ImageInferenceAsyncD3D12::SubmitImage(IFrameBase::Ptr frame,
                                           const std::map<std::string, InputLayerDesc::Ptr> &input_preprocessors) {
    if (!frame)
        throw std::invalid_argument("D3D12 SubmitImage: null frame");

    const Image *src_img = frame->GetImage().get();
    if (!src_img || src_img->type != MemoryType::D3D12)
        throw std::invalid_argument("D3D12 SubmitImage: expected D3D12 image");
    if (!src_img->d3d12_resource)
        throw std::invalid_argument("D3D12 SubmitImage: missing d3d12_resource on source image");

    auto *src_gst_device = static_cast<GstD3D12Device *>(src_img->gst_d3d12_device);
    if (!src_gst_device && src_img->gst_d3d12_memory)
        src_gst_device = GST_D3D12_MEMORY_CAST(src_img->gst_d3d12_memory)->device;
    if (!src_gst_device)
        throw std::invalid_argument("D3D12 SubmitImage: missing gst_d3d12_device on source image");

    bool cross_device =
        (gst_d3d12_device_get_device_handle(src_gst_device) != gst_d3d12_device_get_device_handle(_gst_device));

    D3D12_RESOURCE_DESC src_desc = src_img->d3d12_resource->GetDesc();
    // Logical frame size, not the (macroblock-padded) resource size.
    const uint32_t src_w = src_img->width, src_h = src_img->height;

    GstD3D12Device *conv_device;
    D3D12Converter *converter;
    TexturePool *pool;

    if (!cross_device) {
        conv_device = _gst_device;
        converter = &GetConverter(_converters, conv_device, src_w, src_h, src_desc.Format);
        pool = &_tex_pool;
    } else {
        auto &cross_res = GetCrossDeviceResources(src_gst_device);
        conv_device = cross_res.device;
        converter = &GetConverter(cross_res.converters, conv_device, src_w, src_h, src_desc.Format);
        pool = &cross_res.tex_pool;
    }

    GstMemory *dst_mem = pool->Acquire();
    ReleaseFunc release_mem = [pool](GstMemory *mem) { pool->Release(mem); };

    try {
        ConvertAndBuildImage(frame, conv_device, *converter, dst_mem, release_mem, input_preprocessors);
    } catch (std::exception &e) {
        pool->Release(dst_mem);
        GVA_ERROR("D3D12 SubmitImage: failed: %s", e.what());
        std::throw_with_nested(std::runtime_error("Unable to convert image using D3D12"));
    }

    {
        std::unique_lock lk(_queue_mutex);
        _queue_not_full_cv.wait(lk, [this] { return _submit_queue.size() < _submit_queue_max; });
        _submit_queue.push({std::move(frame), input_preprocessors});
    }
    _queue_cv.notify_one();
}

// -- ImageInference interface delegation --------------------------------------

const std::string &ImageInferenceAsyncD3D12::GetModelName() const {
    return _inference->GetModelName();
}

size_t ImageInferenceAsyncD3D12::GetBatchSize() const {
    return _inference->GetBatchSize();
}

size_t ImageInferenceAsyncD3D12::GetNireq() const {
    return _inference->GetNireq();
}

void ImageInferenceAsyncD3D12::GetModelImageInputInfo(size_t &width, size_t &height, size_t &batch_size, int &format,
                                                      int &memory_type) const {
    _inference->GetModelImageInputInfo(width, height, batch_size, format, memory_type);
}

std::map<std::string, std::vector<size_t>> ImageInferenceAsyncD3D12::GetModelInputsInfo() const {
    return _inference->GetModelInputsInfo();
}

std::map<std::string, std::vector<size_t>> ImageInferenceAsyncD3D12::GetModelOutputsInfo() const {
    return _inference->GetModelOutputsInfo();
}

std::map<std::string, GstStructure *> ImageInferenceAsyncD3D12::GetModelInfoPostproc() const {
    return _inference->GetModelInfoPostproc();
}

bool ImageInferenceAsyncD3D12::IsQueueFull() {
    return _inference->IsQueueFull();
}

void ImageInferenceAsyncD3D12::Flush() {
    {
        std::unique_lock lk(_queue_mutex);
        _queue_empty_cv.wait(lk, [this] { return _submit_queue.empty() && !_worker_busy; });
    }
    if (_inference)
        _inference->Flush();
}

void ImageInferenceAsyncD3D12::Close() {
    _inference->Close();
}
