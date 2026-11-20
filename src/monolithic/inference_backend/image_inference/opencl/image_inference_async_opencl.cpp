/*******************************************************************************
 * Copyright (C) 2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#include "image_inference_async_opencl.h"

#include "dlstreamer/d3d11/context.h"
#include "dlstreamer/d3d11/device_lock.h"
#include "dlstreamer/d3d12/d3d11_device.h"
#include "dlstreamer/d3d12/target_pool.h"
#include "safe_arithmetic.hpp"

#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

using dlstreamer::D3D11DeviceLock;

namespace InferenceBackend {

namespace {

// The video format of a frame in a texture of `format`, for copying it.
GstVideoFormat VideoFormatOf(DXGI_FORMAT format) {
    switch (format) {
    case DXGI_FORMAT_NV12:
        return GST_VIDEO_FORMAT_NV12;
    case DXGI_FORMAT_B8G8R8A8_UNORM:
        return GST_VIDEO_FORMAT_BGRA;
    case DXGI_FORMAT_R8G8B8A8_UNORM:
        return GST_VIDEO_FORMAT_RGBA;
    case DXGI_FORMAT_B8G8R8X8_UNORM:
        return GST_VIDEO_FORMAT_BGRx;
    default:
        return GST_VIDEO_FORMAT_UNKNOWN;
    }
}

} // namespace

// Event queries, recycled: one is ended after each frame copy.
struct ImageInferenceAsyncOpenCL::QueryPool {
    std::mutex mutex;
    std::vector<Microsoft::WRL::ComPtr<ID3D11Query>> free;

    Microsoft::WRL::ComPtr<ID3D11Query> Acquire(GstD3D11Device *device) {
        {
            std::lock_guard<std::mutex> lock(mutex);
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
            throw std::runtime_error("OpenCL pre-processing: failed to create a D3D11 event query");
        return query;
    }

    void Release(Microsoft::WRL::ComPtr<ID3D11Query> query) {
        std::lock_guard<std::mutex> lock(mutex);
        free.push_back(std::move(query));
    }
};

// A frame in a texture OpenCL can read: a pooled shared copy of the frame, or the frame's own texture when that is a
// single shared one already. For a D3D12 frame, the D3D11 view of a pooled D3D12 copy.
struct ImageInferenceAsyncOpenCL::StagingFrame {
    // The frame's memory, referenced so that the decoder can't recycle the surface while the copy is around, which
    // makes the memory a key for the copy.
    GstMemory *source = nullptr;
    GstMemory *staging = nullptr;
    ID3D11Texture2D *texture = nullptr;
    // D3D11 frames: ended after the D3D11 work on `texture`.
    Microsoft::WRL::ComPtr<ID3D11Query> written;
    std::shared_ptr<QueryPool> query_pool;
    // D3D12 frames: `staging` is a GstD3D12Memory, whose fence is the copy.
    bool d3d12 = false;
    // The worker has seen the copy complete. Only touched by the worker thread.
    bool ready = false;

    StagingFrame() = default;
    StagingFrame(const StagingFrame &) = delete;
    StagingFrame &operator=(const StagingFrame &) = delete;

    ~StagingFrame() {
        if (written && query_pool)
            query_pool->Release(std::move(written));
        if (staging)
            gst_memory_unref(staging);
        if (source)
            gst_memory_unref(source);
    }
};

dlstreamer::ContextPtr ImageInferenceAsyncOpenCL::D3D11ContextFor(const dlstreamer::ContextPtr &frames_context) {
    if (!frames_context || frames_context->handle(dlstreamer::BaseContext::key::d3d11_device))
        return frames_context;
    auto *d3d12_device =
        static_cast<GstD3D12Device *>(frames_context->handle(dlstreamer::BaseContext::key::d3d12_device));
    if (!d3d12_device)
        throw std::invalid_argument("OpenCL pre-processing: the context has no D3D11 or D3D12 device");
    GstD3D11Device *d3d11_device = dlstreamer::d3d11_device_for(d3d12_device);
    if (!d3d11_device)
        throw std::runtime_error("OpenCL pre-processing: cannot create a D3D11 device on the adapter of the D3D12 "
                                 "frames, through which OpenCL reads them");
    // Not owned: the device is kept for the process.
    return std::make_shared<dlstreamer::D3D11Context>(d3d11_device);
}

ImageInferenceAsyncOpenCL::ImageInferenceAsyncOpenCL(const InferenceConfig &, dlstreamer::ContextPtr d3d11_context,
                                                     ImageInference::Ptr inference, ErrorHandlingFunc error_handler)
    : _device_context_storage(d3d11_context), _inference(std::move(inference)),
      _error_handler(std::move(error_handler)) {
    if (!d3d11_context || !_inference)
        throw std::invalid_argument("OpenCL pre-processing: null D3D11 context or inference");
    _gst_device = static_cast<GstD3D11Device *>(d3d11_context->handle(dlstreamer::D3D11Context::key::d3d11_device));
    if (!_gst_device)
        throw std::invalid_argument("OpenCL pre-processing: the context has no D3D11 device");
    g_object_get(_gst_device, "adapter-luid", &_adapter_luid, nullptr);

    _submit_queue_max = safe_mul(_inference->GetBatchSize(), _inference->GetNireq());
    if (_submit_queue_max == 0)
        _submit_queue_max = 1;

    Microsoft::WRL::ComPtr<ID3D11Device5> device5;
    if (FAILED(gst_d3d11_device_get_device_handle(_gst_device)->QueryInterface(IID_PPV_ARGS(&device5))) ||
        FAILED(device5->CreateFence(0, D3D11_FENCE_FLAG_NONE, IID_PPV_ARGS(&_fence))) ||
        FAILED(gst_d3d11_device_get_device_context_handle(_gst_device)->QueryInterface(IID_PPV_ARGS(&_context4))))
        throw std::runtime_error("OpenCL pre-processing: the D3D11 device has no fences (ID3D11Device5)");
    _query_pool = std::make_shared<QueryPool>();
    _fence_event = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    if (!_fence_event)
        throw std::runtime_error("OpenCL pre-processing: CreateEvent failed");

    _worker_running = true;
    _worker_thread = std::thread([this]() { WorkerLoop(); });
}

ImageInferenceAsyncOpenCL::~ImageInferenceAsyncOpenCL() {
    {
        // Under the lock, or the worker can miss the wakeup between its predicate check and its wait.
        std::lock_guard<std::mutex> lock(_queue_mutex);
        _worker_running = false;
    }
    _queue_cv.notify_one();
    if (_worker_thread.joinable())
        _worker_thread.join();

    // The inference holds the imports of the staging textures, and in-flight frames hold the textures: it goes
    // first.
    try {
        _inference->Flush();
    } catch (const std::exception &e) {
        GVA_ERROR("OpenCL pre-processing: flush on teardown failed: %s", e.what());
    }
    _inference.reset();
    DropStagingPool();
    _d3d12_pools.clear();
    gst_clear_object(&_d3d12_device_checked);
    if (_fence_event)
        CloseHandle(_fence_event);
}

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
        throw std::runtime_error("OpenCL pre-processing: failed to create the staging texture pool");
    }
    _staging_desc = desc;
    GVA_INFO("OpenCL pre-processing: staging textures %ux%u format %u", desc.Width, desc.Height, desc.Format);
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
        throw std::invalid_argument("OpenCL pre-processing: the frame has no GstD3D11Memory");
    auto *src_dmem = GST_D3D11_MEMORY_CAST(src_mem);
    if (gst_d3d11_device_get_device_handle(src_dmem->device) != gst_d3d11_device_get_device_handle(_gst_device))
        throw std::runtime_error("OpenCL pre-processing: frames on another D3D11 device than the inference one are "
                                 "not supported");

    std::lock_guard<std::mutex> lock(_staging_mutex);
    // The ROIs of a frame come one after another, and share its copy.
    if (auto last = _last_staging.lock(); last && last->source == src_mem)
        return last;

    D3D11_TEXTURE2D_DESC src_desc = {};
    if (!gst_d3d11_memory_get_texture_desc(src_dmem, &src_desc))
        throw std::runtime_error("OpenCL pre-processing: failed to get the frame's texture desc");
    const guint subresource = gst_d3d11_memory_get_subresource_index(src_dmem);
    const bool readable_in_place = (src_desc.MiscFlags & D3D11_RESOURCE_MISC_SHARED) &&
                                   !(src_desc.MiscFlags & D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX) &&
                                   src_desc.ArraySize == 1 && subresource == 0;

    auto frame = std::make_shared<StagingFrame>();
    frame->source = gst_memory_ref(src_mem);
    frame->query_pool = _query_pool;
    frame->written = _query_pool->Acquire(_gst_device);

    GstMemory *staging = nullptr;
    if (!readable_in_place) {
        EnsureStagingPool(src_desc);
        if (gst_d3d11_pool_allocator_acquire_memory(_staging_pool, &staging) != GST_FLOW_OK || !staging)
            throw std::runtime_error("OpenCL pre-processing: failed to acquire a staging texture");
        frame->staging = staging;
    }

    // The maps upload whatever a CPU writer left in staging memory.
    GstMapInfo src_map, dst_map;
    if (!gst_memory_map(src_mem, &src_map, static_cast<GstMapFlags>(GST_MAP_READ | GST_MAP_D3D11)))
        throw std::runtime_error("OpenCL pre-processing: failed to map the frame");
    if (staging && !gst_memory_map(staging, &dst_map, static_cast<GstMapFlags>(GST_MAP_WRITE | GST_MAP_D3D11))) {
        gst_memory_unmap(src_mem, &src_map);
        throw std::runtime_error("OpenCL pre-processing: failed to map the staging texture");
    }
    {
        D3D11DeviceLock device_lock(_gst_device);
        auto *context = gst_d3d11_device_get_device_context_handle(_gst_device);
        if (staging)
            context->CopySubresourceRegion(reinterpret_cast<ID3D11Resource *>(dst_map.data), 0, 0, 0, 0,
                                           reinterpret_cast<ID3D11Resource *>(src_map.data), subresource, nullptr);
        // Not flushed: a submission costs a few hundred microseconds of CPU, here under the device lock, and other
        // D3D11 users (the decoder first of all) submit often enough anyway. If not, WaitForStaging() flushes.
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

std::shared_ptr<ImageInferenceAsyncOpenCL::StagingFrame>
ImageInferenceAsyncOpenCL::GetD3D12StagingFrame(const Image &src_image) {
    auto *src_mem = static_cast<GstMemory *>(src_image.gst_d3d12_memory);
    if (!src_mem || !gst_is_d3d12_memory(src_mem))
        throw std::invalid_argument("OpenCL pre-processing: the frame has no GstD3D12Memory");
    auto *src_dmem = GST_D3D12_MEMORY_CAST(src_mem);

    std::lock_guard<std::mutex> lock(_staging_mutex);
    // The ROIs of a frame come one after another, and share its copy.
    if (auto last = _last_staging.lock(); last && last->source == src_mem)
        return last;

    // The copy is opened on the D3D11 device of the inference, which must be on the frame's adapter.
    if (src_dmem->device != _d3d12_device_checked) {
        gint64 luid = 0;
        g_object_get(src_dmem->device, "adapter-luid", &luid, nullptr);
        if (luid != _adapter_luid)
            throw std::runtime_error("OpenCL pre-processing: D3D12 frames on another adapter than the inference "
                                     "device are not supported");
        gst_object_replace(reinterpret_cast<GstObject **>(&_d3d12_device_checked), GST_OBJECT(src_dmem->device));
    }

    ID3D12Resource *resource = gst_d3d12_memory_get_resource_handle(src_dmem);
    if (!resource)
        throw std::runtime_error("OpenCL pre-processing: the frame has no D3D12 resource");
    const D3D12_RESOURCE_DESC desc = resource->GetDesc();
    const GstVideoFormat format = VideoFormatOf(desc.Format);
    if (format == GST_VIDEO_FORMAT_UNKNOWN)
        throw std::invalid_argument("OpenCL pre-processing: unsupported D3D12 texture format " +
                                    std::to_string(desc.Format));
    // The frame, which may be smaller than its texture (decoders align the texture size).
    GstVideoInfo info;
    if (!gst_video_info_set_format(&info, format, src_image.width, src_image.height))
        throw std::invalid_argument("OpenCL pre-processing: invalid D3D12 frame size");

    auto &pool = _d3d12_pools[{desc.Width, desc.Height, desc.Format}];
    if (!pool) {
        pool = std::make_unique<dlstreamer::D3D12TargetPool>(GST_CAT_DEFAULT);
        GVA_INFO("OpenCL pre-processing: shareable copies of %llux%u D3D12 textures of format %u",
                 static_cast<unsigned long long>(desc.Width), desc.Height, desc.Format);
    }

    auto frame = std::make_shared<StagingFrame>();
    frame->source = gst_memory_ref(src_mem);
    frame->d3d12 = true;
    // Queued on the D3D12 device. The worker waits for it.
    frame->staging = pool->copy(src_mem, &info);
    if (!frame->staging)
        throw std::runtime_error("OpenCL pre-processing: failed to copy the D3D12 frame into a shareable texture");
    // Opened once per pooled texture: the memory keeps the view.
    frame->texture = gst_d3d12_memory_get_d3d11_texture(GST_D3D12_MEMORY_CAST(frame->staging),
                                                        gst_d3d11_device_get_device_handle(_gst_device));
    if (!frame->texture)
        throw std::runtime_error("OpenCL pre-processing: failed to open the D3D12 copy of the frame in D3D11");

    _last_staging = frame;
    return frame;
}

void ImageInferenceAsyncOpenCL::WaitForStaging(StagingFrame &frame) {
    if (frame.ready)
        return;
    if (frame.d3d12) {
        // OpenCL sees no D3D12 fences.
        if (!gst_d3d12_memory_sync(GST_D3D12_MEMORY_CAST(frame.staging)))
            throw std::runtime_error("OpenCL pre-processing: waiting for the D3D12 copy failed");
        frame.ready = true;
        return;
    }
    uint64_t value = 0;
    {
        D3D11DeviceLock lock(_gst_device);
        auto *context = gst_d3d11_device_get_device_context_handle(_gst_device);
        if (context->GetData(frame.written.Get(), nullptr, 0, D3D11_ASYNC_GETDATA_DONOTFLUSH) == S_OK) {
            frame.ready = true;
            return;
        }
        // Still queued or running: submit it, with a fence signal behind it to wait for.
        value = ++_fence_value;
        if (FAILED(_context4->Signal(_fence.Get(), value)))
            throw std::runtime_error("OpenCL pre-processing: failed to signal the D3D11 fence");
        context->Flush();
    }
    if (_fence->GetCompletedValue() < value && (FAILED(_fence->SetEventOnCompletion(value, _fence_event)) ||
                                                WaitForSingleObject(_fence_event, INFINITE) != WAIT_OBJECT_0))
        throw std::runtime_error("OpenCL pre-processing: D3D11 fence wait failed");
    frame.ready = true;
}

void ImageInferenceAsyncOpenCL::WorkerLoop() {
    while (true) {
        SubmitTask task;
        {
            std::unique_lock<std::mutex> lock(_queue_mutex);
            _queue_cv.wait(lock, [this] { return !_submit_queue.empty() || !_worker_running.load(); });
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
            WaitForStaging(*task.staging);
            task.staging.reset();
            _inference->SubmitImage(std::move(task.frame), task.input_preprocessors);
        } catch (const std::exception &e) {
            // The frame never reached an infer request: report it, so that the element still pushes it.
            GVA_ERROR("OpenCL pre-processing: submitting a frame failed: %s", e.what());
            if (_error_handler)
                _error_handler({frame});
        }
        frame.reset();

        {
            std::lock_guard<std::mutex> lock(_queue_mutex);
            _worker_busy = false;
        }
        _queue_empty_cv.notify_all();
    }
}

void ImageInferenceAsyncOpenCL::SubmitImage(IFrameBase::Ptr frame,
                                            const std::map<std::string, InputLayerDesc::Ptr> &input_preprocessors) {
    if (!frame)
        throw std::invalid_argument("OpenCL pre-processing: null frame");
    const ImagePtr src = frame->GetImage();
    if (!src || (src->type != MemoryType::D3D11 && src->type != MemoryType::D3D12))
        throw std::invalid_argument("OpenCL pre-processing: expected a D3D11 or D3D12 frame");

    std::shared_ptr<StagingFrame> staging;
    try {
        staging = src->type == MemoryType::D3D12 ? GetD3D12StagingFrame(*src) : GetStagingFrame(*src);
    } catch (const std::exception &e) {
        GVA_ERROR("OpenCL pre-processing: staging the frame failed: %s", e.what());
        std::throw_with_nested(std::runtime_error("Unable to pre-process image using OpenCL"));
    }

    // The frame as the pre-processing reads it: the staging texture, with the ROI of this frame (the source image is
    // shared by the ROIs of the frame and its rect changes per ROI), as a D3D11 frame also for a D3D12 one. The
    // texture is not reused while the image is alive.
    auto staged = std::shared_ptr<Image>(new Image(*src), [staging](Image *image) { delete image; });
    staged->type = MemoryType::D3D11;
    staged->d3d11_texture = staging->texture;
    staged->d3d11_subresource_index = 0;
    staged->gst_d3d11_device = _gst_device;
    staged->gst_d3d11_memory = staging->d3d12 ? nullptr : (staging->staging ? staging->staging : staging->source);
    staged->create_cl_buffer = nullptr;
    frame->SetImage(staged);

    {
        std::unique_lock<std::mutex> lock(_queue_mutex);
        _queue_not_full_cv.wait(lock, [this] { return _submit_queue.size() < _submit_queue_max; });
        _submit_queue.push({std::move(frame), input_preprocessors, std::move(staging)});
    }
    _queue_cv.notify_one();
}

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
        std::unique_lock<std::mutex> lock(_queue_mutex);
        _queue_empty_cv.wait(lock, [this] { return _submit_queue.empty() && !_worker_busy; });
    }
    _inference->Flush();
}

void ImageInferenceAsyncOpenCL::Close() {
    _inference->Close();
}

} // namespace InferenceBackend
