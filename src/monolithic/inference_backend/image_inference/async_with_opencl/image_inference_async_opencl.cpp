/*******************************************************************************
 * Copyright (C) 2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#include "image_inference_async_opencl.h"

#include "opencl_preproc.h"

#include "dlstreamer/d3d11/context.h"
#include "dlstreamer/d3d11/device_lock.h"
#include "inference_backend/input_image_layer_descriptor.h"
#include "safe_arithmetic.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>
#include <utility>

using namespace InferenceBackend;
using dlstreamer::D3D11DeviceLock;

namespace {

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

uint8_t to_u8(double value) {
    return static_cast<uint8_t>(std::clamp(std::lround(value), 0l, 255l));
}

// The source and destination rectangles D3D11Converter::SetupPreprocessing gives GstD3D11Converter,
// computed the same way, and reported to `image_transform_info` alike. The source rectangle is kept
// fractional where a crop is mapped back to the source.
OpenCLPreprocGeometry ComputeGeometry(const InputImageLayerDesc::Ptr &pre_proc_info,
                                      const ImageTransformationParams::Ptr &image_transform_info, uint32_t frame_w,
                                      uint32_t frame_h, const InferenceBackend::Rectangle<uint32_t> &roi,
                                      uint32_t dst_width, uint32_t dst_height) {
    // The logical frame, or the ROI clamped to it: ROI coordinates come from detection metadata and may
    // be rounded outside the frame by a pixel.
    uint32_t src_x = 0, src_y = 0, src_w = frame_w, src_h = frame_h;
    if (roi.width > 0 && roi.height > 0) {
        src_x = (std::min)(roi.x, frame_w);
        src_y = (std::min)(roi.y, frame_h);
        src_w = (std::min)(roi.width, frame_w - src_x);
        src_h = (std::min)(roi.height, frame_h - src_y);
    }
    if (src_w == 0 || src_h == 0)
        throw std::runtime_error("OpenCL preprocessing: empty source region");

    OpenCLPreprocGeometry g;
    g.src_x = src_x;
    g.src_y = src_y;
    g.src_w = src_w;
    g.src_h = src_h;
    const int dst_w = safe_convert<int>(dst_width);
    const int dst_h = safe_convert<int>(dst_height);

    if (!pre_proc_info || !pre_proc_info->isDefined()) {
        // Plain resize of the source region to the whole tensor.
        g.dst_w = dst_w;
        g.dst_h = dst_h;
        return g;
    }

    int padding_x = 0, padding_y = 0;
    if (pre_proc_info->doNeedPadding() && (!image_transform_info || !image_transform_info->WasPadding())) {
        const auto &padding = pre_proc_info->getPadding();
        padding_x = safe_convert<int>(padding.stride_x);
        padding_y = safe_convert<int>(padding.stride_y);
        const auto &fill = padding.fill_value;
        for (size_t c = 0; c < 3 && !fill.empty(); ++c)
            g.fill_rgb[c] = to_u8(fill.size() >= 3 ? fill[c] : fill[0]);
    }
    if (padding_x * 2 > dst_w || padding_y * 2 > dst_h)
        throw std::out_of_range("Invalid padding in relation to size");

    const int input_w = dst_w - padding_x * 2;
    const int input_h = dst_h - padding_y * 2;
    int region_w = safe_convert<int>(src_w);
    int region_h = safe_convert<int>(src_h);

    double scale_x = 1, scale_y = 1;
    if (pre_proc_info->doNeedResize() && (region_w != input_w || region_h != input_h)) {
        if (pre_proc_info->isAspectRatioMultipleOfResize())
            throw std::runtime_error("Aspect-ratio-multiple-of resize is supported only by the OpenCV preprocessor");
        const double crop_scale = pre_proc_info->doNeedCrop() ? 1.125 : 1.0;
        scale_x = static_cast<double>(input_w) / src_w;
        scale_y = static_cast<double>(input_h) / src_h;
        if (pre_proc_info->getResizeType() == InputImageLayerDesc::Resize::ASPECT_RATIO)
            scale_x = scale_y = (std::min)(scale_x, scale_y);
        scale_x *= crop_scale;
        scale_y *= crop_scale;
        region_w = static_cast<int>(src_w * scale_x + 0.5);
        region_h = static_cast<int>(src_h * scale_y + 0.5);
        if (image_transform_info)
            image_transform_info->ResizeHasDone(scale_x, scale_y);
    }

    if (pre_proc_info->doNeedCrop() && (region_w != input_w || region_h != input_h)) {
        int border_x = region_w > input_w ? region_w - input_w : 0;
        int border_y = region_h > input_h ? region_h - input_h : 0;
        const int cropped_w = region_w - border_x;
        const int cropped_h = region_h - border_y;

        if (pre_proc_info->getCropType() == InputImageLayerDesc::Crop::CENTRAL_RESIZE) {
            const uint32_t crop_size = (std::min)(frame_w, frame_h);
            const uint32_t start_x = (frame_w - crop_size) / 2;
            const uint32_t start_y = (frame_h - crop_size) / 2;
            g.src_x = start_x;
            g.src_y = start_y;
            g.src_w = crop_size;
            g.src_h = crop_size;
            region_w = input_w;
            region_h = input_h;
            if (image_transform_info)
                image_transform_info->CropHasDone(start_x, start_y);
        } else {
            switch (pre_proc_info->getCropType()) {
            case InputImageLayerDesc::Crop::CENTRAL:
                border_x /= 2;
                border_y /= 2;
                break;
            case InputImageLayerDesc::Crop::TOP_LEFT:
                border_x = 0;
                border_y = 0;
                break;
            case InputImageLayerDesc::Crop::TOP_RIGHT:
                border_y = 0;
                break;
            case InputImageLayerDesc::Crop::BOTTOM_LEFT:
                border_x = 0;
                break;
            case InputImageLayerDesc::Crop::BOTTOM_RIGHT:
                break;
            default:
                throw std::runtime_error("Unknown crop format.");
            }
            region_w = cropped_w;
            region_h = cropped_h;
            if (image_transform_info)
                image_transform_info->CropHasDone(static_cast<size_t>(border_x), static_cast<size_t>(border_y));

            g.src_x += border_x / scale_x;
            g.src_y += border_y / scale_y;
            g.src_w = cropped_w / scale_x;
            g.src_h = cropped_h / scale_y;
        }
    }

    // Centred, the rest is padding.
    g.dst_x = (dst_w - region_w) / 2;
    g.dst_y = (dst_h - region_h) / 2;
    g.dst_w = region_w;
    g.dst_h = region_h;
    if (image_transform_info)
        image_transform_info->PaddingHasDone(static_cast<size_t>((std::max)(g.dst_x, 0)),
                                             static_cast<size_t>((std::max)(g.dst_y, 0)));
    return g;
}

} // namespace

// Event queries, recycled: one is ended after each frame copy.
struct ImageInferenceAsyncOpenCL::QueryPool {
    std::mutex mutex;
    std::vector<Microsoft::WRL::ComPtr<ID3D11Query>> free;

    Microsoft::WRL::ComPtr<ID3D11Query> acquire(GstD3D11Device *device) {
        {
            std::lock_guard lk(mutex);
            if (!free.empty()) {
                auto query = std::move(free.back());
                free.pop_back();
                return query;
            }
        }
        D3D11_QUERY_DESC desc = {};
        desc.Query = D3D11_QUERY_EVENT;
        Microsoft::WRL::ComPtr<ID3D11Query> query;
        if (FAILED(gst_d3d11_device_get_device_handle(device)->CreateQuery(&desc, &query)))
            throw std::runtime_error("OpenCL preprocessing: failed to create a D3D11 event query");
        return query;
    }

    void release(Microsoft::WRL::ComPtr<ID3D11Query> query) {
        std::lock_guard lk(mutex);
        free.push_back(std::move(query));
    }
};

// A frame in a texture the OpenCL kernel can read: a pooled shared copy of the frame, or the frame's
// own texture when that is a single shared one already.
struct ImageInferenceAsyncOpenCL::StagingFrame {
    // The frame's memory. Referenced so that the decoder can't recycle the surface while the copy is
    // around, which makes the memory a key for the copy.
    GstMemory *source = nullptr;
    GstMemory *staging = nullptr;
    ID3D11Texture2D *texture = nullptr;
    // Ended after the D3D11 work on `texture`.
    Microsoft::WRL::ComPtr<ID3D11Query> written;
    std::shared_ptr<QueryPool> query_pool;

    StagingFrame() = default;
    StagingFrame(const StagingFrame &) = delete;
    StagingFrame &operator=(const StagingFrame &) = delete;

    ~StagingFrame() {
        if (written && query_pool)
            query_pool->release(std::move(written));
        if (staging)
            gst_memory_unref(staging);
        if (source)
            gst_memory_unref(source);
    }
};

// The per-image state behind its create_cl_buffer callback. Destroyed with the image, after
// inference: returns the output buffer.
struct ImageInferenceAsyncOpenCL::OutputSlot {
    std::shared_ptr<StagingFrame> staging;
    OpenCLPreprocGeometry geometry;
    uint32_t frame_width = 0, frame_height = 0;
    std::shared_ptr<OpenCLBufferPool> pool;
    cl_mem output = nullptr;

    ~OutputSlot() {
        if (output)
            pool->release(output);
    }
};

// -- Constructor / Destructor -------------------------------------------------

ImageInferenceAsyncOpenCL::ImageInferenceAsyncOpenCL(const InferenceBackend::InferenceConfig &,
                                                     dlstreamer::ContextPtr d3d11_context,
                                                     ImageInference::Ptr inference, ErrorHandlingFunc error_handler)
    : _device_context_storage(d3d11_context), _inference(std::move(inference)),
      _error_handler(std::move(error_handler)) {
    if (!d3d11_context)
        throw std::invalid_argument("OpenCL preprocessing: null D3D11 context");
    _gst_device = static_cast<GstD3D11Device *>(d3d11_context->handle(dlstreamer::D3D11Context::key::d3d11_device));
    if (!_gst_device)
        throw std::invalid_argument("OpenCL preprocessing: the context has no D3D11 device");

    size_t width = 0, height = 0, batch_size = 0;
    int format = 0, memory_type = 0;
    _inference->GetModelImageInputInfo(width, height, batch_size, format, memory_type);
    if (format != FourCC::FOURCC_BGRP && format != FourCC::FOURCC_RGBP)
        throw std::runtime_error("OpenCL preprocessing: the model input is not planar BGR or RGB");

    _dst_fourcc = format;
    _bgr = format == FourCC::FOURCC_BGRP;
    _dst_width = safe_convert<uint32_t>(width);
    _dst_height = safe_convert<uint32_t>(height);
    _submit_queue_max = safe_mul(batch_size, _inference->GetNireq());

    Microsoft::WRL::ComPtr<ID3D11Device5> device5;
    if (FAILED(gst_d3d11_device_get_device_handle(_gst_device)->QueryInterface(IID_PPV_ARGS(&device5))) ||
        FAILED(device5->CreateFence(0, D3D11_FENCE_FLAG_NONE, IID_PPV_ARGS(&_fence))) ||
        FAILED(gst_d3d11_device_get_device_context_handle(_gst_device)->QueryInterface(IID_PPV_ARGS(&_context4))))
        throw std::runtime_error("OpenCL preprocessing: the D3D11 device has no fences (ID3D11Device5)");
    _query_pool = std::make_shared<QueryPool>();
    _fence_event = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    if (!_fence_event)
        throw std::runtime_error("OpenCL preprocessing: CreateEvent failed");

    _worker_running = true;
    _worker_thread = std::thread([this]() { WorkerLoop(); });

    GVA_INFO("OpenCL preprocessing: %ux%u planar %s (nireq=%zu, queue=%zu)", _dst_width, _dst_height,
             _bgr ? "BGR" : "RGB", _inference->GetNireq(), _submit_queue_max);
}

ImageInferenceAsyncOpenCL::~ImageInferenceAsyncOpenCL() {
    {
        // Under the lock, or the worker can miss the wakeup between its predicate check and its wait.
        std::lock_guard lk(_queue_mutex);
        _worker_running = false;
    }
    _queue_cv.notify_one();
    if (_worker_thread.joinable())
        _worker_thread.join();

    // Wait for in-flight requests: their images hold staging textures and output buffers.
    try {
        if (_inference)
            _inference->Flush();
    } catch (const std::exception &e) {
        GVA_ERROR("OpenCL preprocessing: Flush on teardown failed: %s", e.what());
    }

    // The imports reference the staging textures, so they go first.
    _preproc.reset();
    DropStagingPool();
    if (_fence_event)
        CloseHandle(_fence_event);
}

// -- Staging copies -----------------------------------------------------------

void ImageInferenceAsyncOpenCL::EnsureStagingPool(const D3D11_TEXTURE2D_DESC &src_desc) {
    if (_staging_pool && src_desc.Width == _staging_desc.Width && src_desc.Height == _staging_desc.Height &&
        src_desc.Format == _staging_desc.Format)
        return;

    DropStagingPool();
    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width = src_desc.Width;
    desc.Height = src_desc.Height;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = src_desc.Format;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    // OpenCL reads only shared textures correctly (see dlstreamer::D3D11TargetPool).
    desc.MiscFlags = D3D11_RESOURCE_MISC_SHARED;
    _staging_pool = gst_d3d11_pool_allocator_new(_gst_device, &desc);
    if (!_staging_pool || !gst_d3d11_allocator_set_active(GST_D3D11_ALLOCATOR(_staging_pool), TRUE)) {
        DropStagingPool();
        throw std::runtime_error("OpenCL preprocessing: failed to create the staging texture pool");
    }
    _staging_desc = desc;
    GVA_INFO("OpenCL preprocessing: staging textures %ux%u format %u", desc.Width, desc.Height, desc.Format);
}

void ImageInferenceAsyncOpenCL::DropStagingPool() {
    if (!_staging_pool)
        return;
    {
        // Frees the idle textures, which must not race with the immediate context.
        D3D11DeviceLock lock(_gst_device);
        gst_d3d11_allocator_set_active(GST_D3D11_ALLOCATOR(_staging_pool), FALSE);
    }
    gst_object_unref(_staging_pool);
    _staging_pool = nullptr;
    _staging_desc = {};
}

std::shared_ptr<ImageInferenceAsyncOpenCL::StagingFrame>
ImageInferenceAsyncOpenCL::GetStagingFrame(const Image &src_image) {
    auto *src_mem = static_cast<GstMemory *>(src_image.gst_d3d11_memory);
    if (!src_mem || !gst_is_d3d11_memory(src_mem))
        throw std::invalid_argument("OpenCL preprocessing: the frame has no GstD3D11Memory");
    auto *src_dmem = GST_D3D11_MEMORY_CAST(src_mem);
    if (gst_d3d11_device_get_device_handle(src_dmem->device) != gst_d3d11_device_get_device_handle(_gst_device))
        throw std::runtime_error("OpenCL preprocessing: frames on another D3D11 device than the inference one are not "
                                 "supported");

    std::lock_guard lk(_staging_mutex);
    // The ROIs of a frame come one after another, and share its copy.
    if (auto last = _last_staging.lock(); last && last->source == src_mem)
        return last;

    D3D11_TEXTURE2D_DESC src_desc = {};
    if (!gst_d3d11_memory_get_texture_desc(src_dmem, &src_desc))
        throw std::runtime_error("OpenCL preprocessing: failed to get the frame's texture desc");
    const guint subresource = gst_d3d11_memory_get_subresource_index(src_dmem);
    const bool readable_in_place = (src_desc.MiscFlags & D3D11_RESOURCE_MISC_SHARED) &&
                                   !(src_desc.MiscFlags & D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX) &&
                                   src_desc.ArraySize == 1 && subresource == 0;

    auto frame = std::make_shared<StagingFrame>();
    frame->source = gst_memory_ref(src_mem);
    frame->query_pool = _query_pool;
    frame->written = _query_pool->acquire(_gst_device);

    GstMemory *staging = nullptr;
    if (!readable_in_place) {
        EnsureStagingPool(src_desc);
        if (gst_d3d11_pool_allocator_acquire_memory(_staging_pool, &staging) != GST_FLOW_OK || !staging)
            throw std::runtime_error("OpenCL preprocessing: failed to acquire a staging texture");
        frame->staging = staging;
    }

    // The maps upload whatever a CPU writer left in staging memory.
    GstMapInfo src_map, dst_map;
    if (!gst_memory_map(src_mem, &src_map, static_cast<GstMapFlags>(GST_MAP_READ | GST_MAP_D3D11)))
        throw std::runtime_error("OpenCL preprocessing: failed to map the frame");
    if (staging && !gst_memory_map(staging, &dst_map, static_cast<GstMapFlags>(GST_MAP_WRITE | GST_MAP_D3D11))) {
        gst_memory_unmap(src_mem, &src_map);
        throw std::runtime_error("OpenCL preprocessing: failed to map the staging texture");
    }
    {
        D3D11DeviceLock lock(_gst_device);
        auto *context = gst_d3d11_device_get_device_context_handle(_gst_device);
        if (staging)
            context->CopySubresourceRegion(reinterpret_cast<ID3D11Resource *>(dst_map.data), 0, 0, 0, 0,
                                           reinterpret_cast<ID3D11Resource *>(src_map.data), subresource, nullptr);
        // Not flushed: a submission costs a few hundred microseconds of CPU, here under the device
        // lock, and other D3D11 users (the decoder first of all) submit often enough anyway. If not,
        // WaitForStaging() flushes.
        context->End(frame->written.Get());
    }
    if (staging) {
        gst_memory_unmap(staging, &dst_map);
        frame->texture =
            reinterpret_cast<ID3D11Texture2D *>(gst_d3d11_memory_get_resource_handle(GST_D3D11_MEMORY_CAST(staging)));
    } else {
        frame->texture = reinterpret_cast<ID3D11Texture2D *>(gst_d3d11_memory_get_resource_handle(src_dmem));
    }
    gst_memory_unmap(src_mem, &src_map);

    _last_staging = frame;
    return frame;
}

void ImageInferenceAsyncOpenCL::WaitForStaging(const StagingFrame &frame) {
    uint64_t value = 0;
    {
        D3D11DeviceLock lock(_gst_device);
        auto *context = gst_d3d11_device_get_device_context_handle(_gst_device);
        if (context->GetData(frame.written.Get(), nullptr, 0, D3D11_ASYNC_GETDATA_DONOTFLUSH) == S_OK)
            return;
        // Still queued or running: submit it, with a fence signal behind it to wait for.
        value = ++_fence_value;
        if (FAILED(_context4->Signal(_fence.Get(), value)))
            throw std::runtime_error("OpenCL preprocessing: failed to signal the D3D11 fence");
        context->Flush();
    }
    if (_fence->GetCompletedValue() < value && (FAILED(_fence->SetEventOnCompletion(value, _fence_event)) ||
                                                WaitForSingleObject(_fence_event, INFINITE) != WAIT_OBJECT_0))
        throw std::runtime_error("OpenCL preprocessing: D3D11 fence wait failed");
}

OpenCLPreproc &ImageInferenceAsyncOpenCL::GetPreproc(void *cl_ctx) {
    std::lock_guard lk(_preproc_mutex);
    if (!_preproc)
        _preproc = std::make_unique<OpenCLPreproc>(static_cast<cl_context>(cl_ctx), _gst_device, _dst_width,
                                                   _dst_height, _bgr);
    else if (_preproc->context() != static_cast<cl_context>(cl_ctx))
        throw std::runtime_error("OpenCL preprocessing: OpenVINO's cl_context changed");
    return *_preproc;
}

// -- Worker thread ------------------------------------------------------------

void ImageInferenceAsyncOpenCL::WorkerLoop() {
    while (true) {
        SubmitTask task;
        {
            std::unique_lock lk(_queue_mutex);
            _queue_cv.wait(lk, [this] { return !_submit_queue.empty() || !_worker_running.load(); });
            if (!_worker_running.load() && _submit_queue.empty())
                break;
            task = std::move(_submit_queue.front());
            _submit_queue.pop();
            // Flush() must also wait for this task, not only for the queue to drain.
            _worker_busy = true;
        }
        _queue_not_full_cv.notify_one();

        IFrameBase::Ptr frame = task.frame;
        try {
            _inference->SubmitImage(std::move(task.frame), task.input_preprocessors);
        } catch (const std::exception &e) {
            // The frame never reached an infer request: report it, so the element still pushes it.
            GVA_ERROR("OpenCL worker: OV SubmitImage failed: %s", e.what());
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

void ImageInferenceAsyncOpenCL::SubmitImage(IFrameBase::Ptr frame,
                                            const std::map<std::string, InputLayerDesc::Ptr> &input_preprocessors) {
    if (!frame)
        throw std::invalid_argument("OpenCL SubmitImage: null frame");

    const Image *src_img = frame->GetImage().get();
    if (!src_img || src_img->type != MemoryType::D3D11)
        throw std::invalid_argument("OpenCL SubmitImage: expected D3D11 image");

    auto slot = std::make_shared<OutputSlot>();
    try {
        // Logical frame size, not the (macroblock-padded) texture size.
        slot->frame_width = src_img->width;
        slot->frame_height = src_img->height;
        slot->geometry =
            ComputeGeometry(getImagePreProcInfo(input_preprocessors), frame->GetImageTransformationParams(),
                            slot->frame_width, slot->frame_height, src_img->rect, _dst_width, _dst_height);
        slot->staging = GetStagingFrame(*src_img);
    } catch (const std::exception &e) {
        GVA_ERROR("OpenCL SubmitImage: failed: %s", e.what());
        std::throw_with_nested(std::runtime_error("Unable to pre-process image using OpenCL"));
    }

    auto image = std::make_shared<Image>();
    image->type = MemoryType::D3D11;
    image->width = _dst_width;
    image->height = _dst_height;
    image->format = _dst_fourcc;
    image->d3d11_texture = slot->staging->texture;
    image->gst_d3d11_device = _gst_device;
    // OpenVINO asks for the input tensor from the worker thread, passing its cl_context; the kernel
    // runs then. The slot, and with it the output buffer, lives as long as the image.
    image->create_cl_buffer = [this, slot](void *cl_ctx) -> void * {
        if (slot->output)
            return slot->output;
        OpenCLPreproc &preproc = GetPreproc(cl_ctx);
        slot->pool = preproc.output_pool();
        slot->output = slot->pool->acquire();
        WaitForStaging(*slot->staging);
        preproc.convert(slot->staging->texture, slot->frame_width, slot->frame_height, slot->geometry, slot->output);
        // Read: the next frame can have the texture.
        slot->staging.reset();
        return slot->output;
    };
    frame->SetImage(image);

    {
        std::unique_lock lk(_queue_mutex);
        _queue_not_full_cv.wait(lk, [this] { return _submit_queue.size() < _submit_queue_max; });
        _submit_queue.push({std::move(frame), input_preprocessors});
    }
    _queue_cv.notify_one();
}

// -- ImageInference interface delegation --------------------------------------

const std::string &ImageInferenceAsyncOpenCL::GetModelName() const {
    return _inference->GetModelName();
}

size_t ImageInferenceAsyncOpenCL::GetBatchSize() const {
    return _inference->GetBatchSize();
}

size_t ImageInferenceAsyncOpenCL::GetNireq() const {
    return _inference->GetNireq();
}

void ImageInferenceAsyncOpenCL::GetModelImageInputInfo(size_t &width, size_t &height, size_t &batch_size, int &format,
                                                       int &memory_type) const {
    _inference->GetModelImageInputInfo(width, height, batch_size, format, memory_type);
}

std::map<std::string, std::vector<size_t>> ImageInferenceAsyncOpenCL::GetModelInputsInfo() const {
    return _inference->GetModelInputsInfo();
}

std::map<std::string, std::vector<size_t>> ImageInferenceAsyncOpenCL::GetModelOutputsInfo() const {
    return _inference->GetModelOutputsInfo();
}

std::map<std::string, GstStructure *> ImageInferenceAsyncOpenCL::GetModelInfoPostproc() const {
    return _inference->GetModelInfoPostproc();
}

bool ImageInferenceAsyncOpenCL::IsQueueFull() {
    return _inference->IsQueueFull();
}

void ImageInferenceAsyncOpenCL::Flush() {
    {
        std::unique_lock lk(_queue_mutex);
        _queue_empty_cv.wait(lk, [this] { return _submit_queue.empty() && !_worker_busy; });
    }
    if (_inference)
        _inference->Flush();
}

void ImageInferenceAsyncOpenCL::Close() {
    _inference->Close();
}
