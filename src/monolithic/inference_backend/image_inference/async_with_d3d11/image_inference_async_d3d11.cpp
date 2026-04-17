/*******************************************************************************
 * Copyright (C) 2025-2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#include "image_inference_async_d3d11.h"

#include "inference_backend/image_inference.h"
#include "inference_backend/input_image_layer_descriptor.h"
#include "safe_arithmetic.hpp"
#include "utils.h"

#include "d3d11_converter.h"
#include "dlstreamer/d3d11/context.h"

#include <cstring>
#include <d3d11_1.h>
#include <dxgi1_2.h>
#include <string>
#include <utility>
#include <wrl/client.h>

using namespace InferenceBackend;

namespace {

struct GstBufferDeleter {
    void operator()(GstBuffer *b) const {
        if (b)
            gst_buffer_unref(b);
    }
};
using GstBufferPtr = std::unique_ptr<GstBuffer, GstBufferDeleter>;

struct HandleCloser {
    void operator()(void *h) const {
        if (h)
            CloseHandle(h);
    }
};
using UniqueHandle = std::unique_ptr<void, HandleCloser>;

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

void AllocateTexturePool(GstD3D11Device *device, const D3D11_TEXTURE2D_DESC &desc, size_t count,
                         std::vector<GstMemory *> &out) {
    auto *allocator = GST_D3D11_ALLOCATOR(g_object_new(gst_d3d11_allocator_get_type(), nullptr));
    for (size_t i = 0; i < count; i++) {
        GstMemory *mem = gst_d3d11_allocator_alloc(allocator, device, &desc);
        if (!mem) {
            gst_object_unref(allocator);
            throw std::runtime_error("D3D11: Failed to allocate texture");
        }
        out.push_back(mem);
    }
    gst_object_unref(allocator);
}

} // namespace

// -- TexturePool --------------------------------------------------------------

GstMemory *ImageInferenceAsyncD3D11::TexturePool::Acquire() {
    std::unique_lock lk(mutex);
    cv.wait(lk, [this] { return !free.empty(); });
    GstMemory *mem = free.back();
    free.pop_back();
    return mem;
}

void ImageInferenceAsyncD3D11::TexturePool::Release(GstMemory *mem) {
    {
        std::lock_guard lk(mutex);
        free.push_back(mem);
    }
    cv.notify_one();
}

// -- Constructor / Destructor -------------------------------------------------

ImageInferenceAsyncD3D11::ImageInferenceAsyncD3D11(const InferenceBackend::InferenceConfig &config,
                                                   dlstreamer::ContextPtr d3d11_context, ImageInference::Ptr inference)
    : _inference(inference), _device_context_storage(d3d11_context) {

    _gst_device = static_cast<GstD3D11Device *>(d3d11_context->handle(dlstreamer::D3D11Context::key::d3d_device));

    size_t width = 0, height = 0, batch_size = 0;
    int format = 0, memory_type = 0;
    inference->GetModelImageInputInfo(width, height, batch_size, format, memory_type);

    _surface_sharing = (static_cast<MemoryType>(memory_type) == MemoryType::D3D11);

    _dst_format = FourCCToDXGI(format);
    if (_dst_format == DXGI_FORMAT_UNKNOWN) {
        GVA_WARNING("D3D11: Format %d has no DXGI equivalent, using BGRA", format);
        _dst_format = DXGI_FORMAT_B8G8R8A8_UNORM;
    }

    _dst_width = width;
    _dst_height = height;

    _dst_texture_desc.Width = _dst_width;
    _dst_texture_desc.Height = _dst_height;
    _dst_texture_desc.MipLevels = 1;
    _dst_texture_desc.ArraySize = 1;
    _dst_texture_desc.Format = _dst_format;
    _dst_texture_desc.SampleDesc.Count = 1;
    _dst_texture_desc.Usage = D3D11_USAGE_DEFAULT;
    _dst_texture_desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;

    size_t pool_size = safe_mul(batch_size, _inference->GetNireq());
    AllocateTexturePool(_gst_device, _dst_texture_desc, pool_size, _tex_pool.free);

    _submit_queue_max = pool_size;
    _worker_running = true;
    _worker_thread = std::thread([this]() { WorkerLoop(); });

    GVA_INFO("D3D11 preprocessing: %ux%u format=%u (nireq=%u, pool=%zu)", _dst_width, _dst_height, _dst_format,
             _inference->GetNireq(), pool_size);
}

ImageInferenceAsyncD3D11::~ImageInferenceAsyncD3D11() {
    _worker_running = false;
    _queue_cv.notify_one();
    if (_worker_thread.joinable())
        _worker_thread.join();

    for (auto *mem : _tex_pool.free)
        gst_memory_unref(mem);

    for (auto &[device, res] : _cross_device_resources) {
        for (auto *mem : res->tex_pool.free)
            gst_memory_unref(mem);
        if (res->owns_device && res->device)
            gst_object_unref(res->device);
    }
}

// -- Converter management -----------------------------------------------------

void ImageInferenceAsyncD3D11::EnsureConverter(uint32_t src_width, uint32_t src_height, DXGI_FORMAT src_format) {
    std::lock_guard lk(_converter_mutex);
    if (_converter)
        return;
    _converter = std::make_unique<D3D11Converter>(_gst_device, src_width, src_height, src_format, _dst_width,
                                                  _dst_height, _dst_format);
    GVA_INFO("D3D11: Created converter on dst device: %ux%u (fmt=%u) -> %ux%u", src_width, src_height, src_format,
             _dst_width, _dst_height);
}

ImageInferenceAsyncD3D11::CrossDeviceResources &
ImageInferenceAsyncD3D11::GetCrossDeviceResources(GstD3D11Device *src_gst_device, uint32_t src_width,
                                                  uint32_t src_height, DXGI_FORMAT src_format) {
    auto *src_device = gst_d3d11_device_get_device_handle(src_gst_device);
    std::lock_guard lk(_cross_device_mutex);
    auto it = _cross_device_resources.find(src_device);
    if (it != _cross_device_resources.end())
        return *it->second;

    auto res = std::make_unique<CrossDeviceResources>();
    res->device = static_cast<GstD3D11Device *>(gst_object_ref(src_gst_device));
    res->owns_device = true;
    res->converter = std::make_unique<D3D11Converter>(res->device, src_width, src_height, src_format, _dst_width,
                                                      _dst_height, _dst_format);

    D3D11_TEXTURE2D_DESC alloc_desc = _dst_texture_desc;
    alloc_desc.MiscFlags |= D3D11_RESOURCE_MISC_SHARED_NTHANDLE | D3D11_RESOURCE_MISC_SHARED;
    AllocateTexturePool(res->device, alloc_desc, _submit_queue_max, res->tex_pool.free);

    GVA_INFO("D3D11: Created cross-device converter + %zu shared textures for source device %p", _submit_queue_max,
             src_device);
    auto &ref = *res;
    _cross_device_resources[src_device] = std::move(res);
    return ref;
}

// -- SubmitImage helpers ------------------------------------------------------

GstBuffer *ImageInferenceAsyncD3D11::WrapSourceTexture(const Image &src_image, GstD3D11Device *device) {
    if (!src_image.d3d11_texture)
        throw std::invalid_argument("WrapSourceTexture: null texture");

    auto *allocator = GST_D3D11_ALLOCATOR(g_object_new(gst_d3d11_allocator_get_type(), nullptr));
    GstMemory *mem = gst_d3d11_allocator_alloc_wrapped(allocator, device, src_image.d3d11_texture, 0, nullptr, nullptr);
    gst_object_unref(allocator);
    if (!mem)
        throw std::runtime_error("WrapSourceTexture: Failed to wrap D3D11 texture");

    GstBuffer *buf = gst_buffer_new();
    gst_buffer_append_memory(buf, mem);
    return buf;
}

void ImageInferenceAsyncD3D11::BuildSurfaceSharingImage(IFrameBase::Ptr &frame, GstMemory *dst_mem,
                                                        ReleaseFunc release_mem) {
    auto *ov_texture = gst_d3d11_memory_get_resource_handle(GST_D3D11_MEMORY_CAST(dst_mem));

    auto image = std::unique_ptr<Image>(new Image());
    image->type = MemoryType::D3D11;
    image->width = _dst_width;
    image->height = _dst_height;
    image->format = FourCC::FOURCC_NV12;
    image->d3d11_texture = static_cast<ID3D11Texture2D *>(ov_texture);
    image->gst_d3d11_device = _gst_device;
    image->d3d11_subresource_index = 0;

    frame->SetImage(std::shared_ptr<Image>(image.release(), [release_mem, dst_mem](Image *img) {
        release_mem(dst_mem);
        delete img;
    }));
}

void ImageInferenceAsyncD3D11::BuildSystemMemoryImage(IFrameBase::Ptr &frame, GstMemory *dst_mem,
                                                      ReleaseFunc release_mem) {
    GstMapInfo map_info;
    if (!gst_memory_map(dst_mem, &map_info, GST_MAP_READ))
        throw std::runtime_error("D3D11: Failed to map converted texture");

    auto image = std::unique_ptr<Image>(new Image());
    image->type = MemoryType::SYSTEM;
    image->width = _dst_width;
    image->height = _dst_height;

    guint stride = 0;
    gst_d3d11_memory_get_resource_stride(GST_D3D11_MEMORY_CAST(dst_mem), &stride);

    image->planes[0] = map_info.data;
    image->stride[0] = stride;
    if (_dst_texture_desc.Format == DXGI_FORMAT_NV12) {
        image->planes[1] = map_info.data + stride * _dst_height;
        image->stride[1] = stride;
        image->format = FourCC::FOURCC_NV12;
    } else {
        image->format = FourCC::FOURCC_BGRA;
    }

    frame->SetImage(std::shared_ptr<Image>(image.release(), [release_mem, dst_mem, map_info](Image *img) mutable {
        gst_memory_unmap(dst_mem, &map_info);
        release_mem(dst_mem);
        delete img;
    }));
}

void ImageInferenceAsyncD3D11::ShareTextureToDstDevice(IFrameBase::Ptr &frame, GstMemory *dst_mem,
                                                       ReleaseFunc release_mem) {
    auto *src_tex =
        reinterpret_cast<ID3D11Texture2D *>(gst_d3d11_memory_get_resource_handle(GST_D3D11_MEMORY_CAST(dst_mem)));

    Microsoft::WRL::ComPtr<IDXGIResource1> dxgi_resource;
    HRESULT hr = src_tex->QueryInterface(IID_PPV_ARGS(&dxgi_resource));
    if (FAILED(hr))
        throw std::runtime_error("D3D11: Failed to get IDXGIResource1 from cross-device texture");

    HANDLE raw_handle = nullptr;
    hr = dxgi_resource->CreateSharedHandle(nullptr, DXGI_SHARED_RESOURCE_READ, nullptr, &raw_handle);
    if (FAILED(hr) || !raw_handle)
        throw std::runtime_error("D3D11: Failed to create shared handle for cross-device texture");
    UniqueHandle shared_handle(raw_handle);

    auto *dst_device = gst_d3d11_device_get_device_handle(_gst_device);
    Microsoft::WRL::ComPtr<ID3D11Device1> device1;
    hr = dst_device->QueryInterface(IID_PPV_ARGS(&device1));
    if (FAILED(hr))
        throw std::runtime_error("D3D11: Failed to get ID3D11Device1");

    Microsoft::WRL::ComPtr<ID3D11Texture2D> shared_texture;
    hr = device1->OpenSharedResource1(shared_handle.get(), IID_PPV_ARGS(&shared_texture));
    if (FAILED(hr))
        throw std::runtime_error("D3D11: Failed to open shared texture on inference device");

    auto image = std::unique_ptr<Image>(new Image());
    image->type = MemoryType::D3D11;
    image->width = _dst_width;
    image->height = _dst_height;
    image->format = FourCC::FOURCC_NV12;
    image->d3d11_texture = shared_texture.Get();
    image->gst_d3d11_device = _gst_device;
    image->d3d11_subresource_index = 0;

    frame->SetImage(std::shared_ptr<Image>(image.release(), [release_mem, dst_mem, shared_texture](Image *img) {
        release_mem(dst_mem);
        delete img;
    }));
}

void ImageInferenceAsyncD3D11::ConvertAndBuildImage(
    IFrameBase::Ptr &frame, GstD3D11Device *conv_device, D3D11Converter &converter, GstMemory *dst_mem,
    ReleaseFunc release_mem, const std::map<std::string, InputLayerDesc::Ptr> &input_preprocessors) {

    bool cross_device =
        (gst_d3d11_device_get_device_handle(conv_device) != gst_d3d11_device_get_device_handle(_gst_device));

    gst_d3d11_device_lock(conv_device);

    GstBufferPtr src_buf(WrapSourceTexture(*frame->GetImage(), conv_device));
    GstBufferPtr dst_buf(gst_buffer_new());
    gst_buffer_append_memory(dst_buf.get(), gst_memory_ref(dst_mem));

    converter.Convert(src_buf.get(), dst_buf.get(), getImagePreProcInfo(input_preprocessors),
                      frame->GetImageTransformationParams());

    src_buf.reset();
    dst_buf.reset();
    frame->SetImage(nullptr);

    if (_surface_sharing) {
        if (cross_device)
            ShareTextureToDstDevice(frame, dst_mem, release_mem);
        else
            BuildSurfaceSharingImage(frame, dst_mem, release_mem);
    } else {
        BuildSystemMemoryImage(frame, dst_mem, release_mem);
    }

    gst_d3d11_device_unlock(conv_device);
}

// -- Worker thread ------------------------------------------------------------

void ImageInferenceAsyncD3D11::WorkerLoop() {
    while (true) {
        SubmitTask task;
        bool queue_now_empty = false;
        {
            std::unique_lock lk(_queue_mutex);
            _queue_cv.wait(lk, [this] { return !_submit_queue.empty() || !_worker_running.load(); });
            if (!_worker_running.load() && _submit_queue.empty())
                break;
            task = std::move(_submit_queue.front());
            _submit_queue.pop();
            queue_now_empty = _submit_queue.empty();
        }
        _queue_not_full_cv.notify_one();
        if (queue_now_empty)
            _queue_empty_cv.notify_all();

        try {
            _inference->SubmitImage(std::move(task.frame), task.input_preprocessors);
        } catch (const std::exception &e) {
            GVA_ERROR("D3D11 worker: OV SubmitImage failed: %s", e.what());
        }
    }
}

// -- SubmitImage --------------------------------------------------------------

void ImageInferenceAsyncD3D11::SubmitImage(IFrameBase::Ptr frame,
                                           const std::map<std::string, InputLayerDesc::Ptr> &input_preprocessors) {
    if (!frame)
        throw std::invalid_argument("D3D11 SubmitImage: null frame");

    const Image *src_img = frame->GetImage().get();
    if (!src_img || src_img->type != MemoryType::D3D11)
        throw std::invalid_argument("D3D11 SubmitImage: expected D3D11 image");

    auto *src_gst_device = static_cast<GstD3D11Device *>(src_img->gst_d3d11_device);
    if (!src_gst_device)
        throw std::invalid_argument("D3D11 SubmitImage: missing gst_d3d11_device on source image");

    bool cross_device =
        (gst_d3d11_device_get_device_handle(src_gst_device) != gst_d3d11_device_get_device_handle(_gst_device));

    D3D11_TEXTURE2D_DESC src_tex_desc;
    src_img->d3d11_texture->GetDesc(&src_tex_desc);

    GstD3D11Device *conv_device;
    D3D11Converter *converter;
    TexturePool *pool;

    if (!cross_device) {
        EnsureConverter(src_tex_desc.Width, src_tex_desc.Height, src_tex_desc.Format);
        conv_device = _gst_device;
        converter = _converter.get();
        pool = &_tex_pool;
    } else {
        auto &cross_res =
            GetCrossDeviceResources(src_gst_device, src_tex_desc.Width, src_tex_desc.Height, src_tex_desc.Format);
        conv_device = cross_res.device;
        converter = cross_res.converter.get();
        pool = &cross_res.tex_pool;
    }

    GstMemory *dst_mem = pool->Acquire();
    ReleaseFunc release_mem = [pool](GstMemory *mem) { pool->Release(mem); };

    try {
        ConvertAndBuildImage(frame, conv_device, *converter, dst_mem, release_mem, input_preprocessors);
    } catch (std::exception &e) {
        pool->Release(dst_mem);
        GVA_ERROR("D3D11 SubmitImage: failed: %s", e.what());
        std::throw_with_nested(std::runtime_error("Unable to convert image using D3D11"));
    }

    {
        std::unique_lock lk(_queue_mutex);
        _queue_not_full_cv.wait(lk, [this] { return _submit_queue.size() < _submit_queue_max; });
        _submit_queue.push({std::move(frame), input_preprocessors});
    }
    _queue_cv.notify_one();
}

// -- ImageInference interface delegation --------------------------------------

const std::string &ImageInferenceAsyncD3D11::GetModelName() const {
    return _inference->GetModelName();
}

size_t ImageInferenceAsyncD3D11::GetBatchSize() const {
    return _inference->GetBatchSize();
}

size_t ImageInferenceAsyncD3D11::GetNireq() const {
    return _inference->GetNireq();
}

void ImageInferenceAsyncD3D11::GetModelImageInputInfo(size_t &width, size_t &height, size_t &batch_size, int &format,
                                                      int &memory_type) const {
    _inference->GetModelImageInputInfo(width, height, batch_size, format, memory_type);
}

std::map<std::string, std::vector<size_t>> ImageInferenceAsyncD3D11::GetModelInputsInfo() const {
    return _inference->GetModelInputsInfo();
}

std::map<std::string, std::vector<size_t>> ImageInferenceAsyncD3D11::GetModelOutputsInfo() const {
    return _inference->GetModelOutputsInfo();
}

std::map<std::string, GstStructure *> ImageInferenceAsyncD3D11::GetModelInfoPostproc() const {
    return _inference->GetModelInfoPostproc();
}

bool ImageInferenceAsyncD3D11::IsQueueFull() {
    return _inference->IsQueueFull();
}

void ImageInferenceAsyncD3D11::Flush() {
    {
        std::unique_lock lk(_queue_mutex);
        _queue_empty_cv.wait(lk, [this] { return _submit_queue.empty(); });
    }
    if (_inference)
        _inference->Flush();
}

void ImageInferenceAsyncD3D11::Close() {
    _inference->Close();
}
