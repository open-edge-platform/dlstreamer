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

// Single D3D11 textures of one size and format, recycled, shared (OpenCL reads only shared textures correctly, see
// dlstreamer::D3D11TargetPool) and, for another device's frames, also through NT handles.
struct ImageInferenceAsyncOpenCL::D3D11StagingPool {
    GstD3D11Device *device; // the device of the textures, which outlives the pool
    GstD3D11PoolAllocator *allocator = nullptr;

    D3D11StagingPool(GstD3D11Device *device, const D3D11_TEXTURE2D_DESC &src_desc, UINT misc_flags) : device(device) {
        D3D11_TEXTURE2D_DESC desc = {};
        desc.Width = src_desc.Width;
        desc.Height = src_desc.Height;
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Format = src_desc.Format;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
        desc.MiscFlags = misc_flags;
        allocator = gst_d3d11_pool_allocator_new(device, &desc);
        if (!allocator)
            throw std::runtime_error("OpenCL pre-processing: failed to create the staging texture pool");
        if (!gst_d3d11_allocator_set_active(GST_D3D11_ALLOCATOR(allocator), TRUE)) {
            gst_object_unref(allocator);
            throw std::runtime_error("OpenCL pre-processing: failed to activate the staging texture pool");
        }
    }

    ~D3D11StagingPool() {
        {
            // Frees the idle textures, which must not race with the immediate context. The others go when their
            // frames release them.
            D3D11DeviceLock lock(device);
            gst_d3d11_allocator_set_active(GST_D3D11_ALLOCATOR(allocator), FALSE);
        }
        gst_object_unref(allocator);
    }

    D3D11StagingPool(const D3D11StagingPool &) = delete;
    D3D11StagingPool &operator=(const D3D11StagingPool &) = delete;
};

// A D3D11 device that frames are copied on, with what the worker needs to wait for the copies: event queries,
// recycled (one is ended after each copy), and a fence for a query that isn't done yet. The inference device copies
// its own frames. Another device copies its frames into textures from its pools, shared with the inference device.
struct ImageInferenceAsyncOpenCL::CopyDevice {
    GstD3D11Device *const device; // referenced
    Microsoft::WRL::ComPtr<ID3D11Fence> fence;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext4> context4;
    uint64_t fence_value = 0;     // under the device lock
    HANDLE fence_event = nullptr; // waited on by the worker thread only
    std::mutex query_mutex;
    std::vector<Microsoft::WRL::ComPtr<ID3D11Query>> free_queries; // guarded by query_mutex

    // Another device: the pools of shared textures, and the count of frames when one of its frames was last staged.
    // Guarded by the wrapper's _staging_mutex.
    D3D11Pools pools;
    uint64_t last_used = 0;

    explicit CopyDevice(GstD3D11Device *gst_device)
        : device(static_cast<GstD3D11Device *>(gst_object_ref(gst_device))) {
        Microsoft::WRL::ComPtr<ID3D11Device5> device5;
        if (FAILED(gst_d3d11_device_get_device_handle(device)->QueryInterface(IID_PPV_ARGS(&device5))) ||
            FAILED(device5->CreateFence(0, D3D11_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence))) ||
            FAILED(gst_d3d11_device_get_device_context_handle(device)->QueryInterface(IID_PPV_ARGS(&context4)))) {
            gst_object_unref(device);
            throw std::runtime_error("OpenCL pre-processing: the D3D11 device has no fences (ID3D11Device5)");
        }
        fence_event = CreateEvent(nullptr, FALSE, FALSE, nullptr);
        if (!fence_event) {
            gst_object_unref(device);
            throw std::runtime_error("OpenCL pre-processing: CreateEvent failed");
        }
    }

    ~CopyDevice() {
        pools.clear(); // they use the device
        CloseHandle(fence_event);
        gst_object_unref(device);
    }

    CopyDevice(const CopyDevice &) = delete;
    CopyDevice &operator=(const CopyDevice &) = delete;

    Microsoft::WRL::ComPtr<ID3D11Query> AcquireQuery() {
        {
            std::lock_guard<std::mutex> lock(query_mutex);
            if (!free_queries.empty()) {
                auto query = std::move(free_queries.back());
                free_queries.pop_back();
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

    void ReleaseQuery(Microsoft::WRL::ComPtr<ID3D11Query> query) {
        std::lock_guard<std::mutex> lock(query_mutex);
        free_queries.push_back(std::move(query));
    }

    // Waits on the CPU until the D3D11 work up to `query` has completed. Worker thread only.
    void Wait(ID3D11Query *query) {
        uint64_t value = 0;
        {
            D3D11DeviceLock lock(device);
            auto *context = gst_d3d11_device_get_device_context_handle(device);
            if (context->GetData(query, nullptr, 0, D3D11_ASYNC_GETDATA_DONOTFLUSH) == S_OK)
                return;
            // Still queued or running: submit it, with a fence signal behind it to wait for.
            value = ++fence_value;
            if (FAILED(context4->Signal(fence.Get(), value)))
                throw std::runtime_error("OpenCL pre-processing: failed to signal the D3D11 fence");
            context->Flush();
        }
        if (fence->GetCompletedValue() < value && (FAILED(fence->SetEventOnCompletion(value, fence_event)) ||
                                                   WaitForSingleObject(fence_event, INFINITE) != WAIT_OBJECT_0))
            throw std::runtime_error("OpenCL pre-processing: D3D11 fence wait failed");
    }
};

// A frame in a texture OpenCL can read on the inference device: a pooled shared copy of the frame, or the frame's
// own texture when that is a single shared one already. For a frame from another D3D11 device, a copy made there, as
// opened on the inference device. For a D3D12 frame, the D3D11 view of a pooled D3D12 copy.
struct ImageInferenceAsyncOpenCL::StagingFrame {
    // The frame's memory, referenced so that the decoder can't recycle the surface while the copy is around, which
    // makes the memory a key for the copy.
    GstMemory *source = nullptr;
    GstMemory *staging = nullptr;
    ID3D11Texture2D *texture = nullptr;
    // The GstD3D11Memory of `texture` on the inference device, if it has one (not referenced separately): not for
    // textures opened from shared handles.
    GstMemory *texture_memory = nullptr;
    // D3D11 frames: the device the copy was made on (or the frame was read in place on), and the query ended there
    // after it.
    std::shared_ptr<CopyDevice> copy_device;
    Microsoft::WRL::ComPtr<ID3D11Query> written;
    // D3D12 frames: `staging` is a GstD3D12Memory, whose fence is the copy.
    bool d3d12 = false;
    // The worker has seen the copy complete. Only touched by the worker thread.
    bool ready = false;

    StagingFrame() = default;
    StagingFrame(const StagingFrame &) = delete;
    StagingFrame &operator=(const StagingFrame &) = delete;

    ~StagingFrame() {
        if (written && copy_device)
            copy_device->ReleaseQuery(std::move(written));
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

    _own_device = std::make_shared<CopyDevice>(_gst_device);

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
    _staging_pools.clear();
    _d3d12_pools.clear();
    _other_devices.clear();
    _own_device.reset();
    gst_clear_object(&_d3d12_device_checked);
}

ImageInferenceAsyncOpenCL::D3D11StagingPool &ImageInferenceAsyncOpenCL::PoolFor(D3D11Pools &pools,
                                                                                GstD3D11Device *device,
                                                                                const D3D11_TEXTURE2D_DESC &desc,
                                                                                UINT misc_flags) {
    auto &slot = pools[{desc.Width, desc.Height, desc.Format}];
    if (!slot.pool) {
        slot.pool = std::make_unique<D3D11StagingPool>(device, desc, misc_flags);
        if (device == _gst_device)
            GVA_INFO("OpenCL pre-processing: staging textures %ux%u format %u", desc.Width, desc.Height, desc.Format);
        else
            GVA_INFO("OpenCL pre-processing: textures shared from D3D11 device %p, %ux%u format %u",
                     static_cast<void *>(device), desc.Width, desc.Height, desc.Format);
    }
    slot.last_used = _frames_staged;
    return *slot.pool;
}

std::shared_ptr<ImageInferenceAsyncOpenCL::CopyDevice> ImageInferenceAsyncOpenCL::OtherDevice(GstD3D11Device *device) {
    ID3D11Device *handle = gst_d3d11_device_get_device_handle(device);
    if (auto it = _other_devices.find(handle); it != _other_devices.end()) {
        it->second->last_used = _frames_staged;
        return it->second;
    }
    // Textures are shared between the devices of one adapter only.
    gint64 luid = 0;
    g_object_get(device, "adapter-luid", &luid, nullptr);
    if (luid != _adapter_luid)
        throw std::runtime_error("OpenCL pre-processing: D3D11 frames on another adapter than the inference device are "
                                 "not supported");
    auto copy_device = std::make_shared<CopyDevice>(device);
    copy_device->last_used = _frames_staged;
    _other_devices.emplace(handle, copy_device);
    GVA_INFO(
        "OpenCL pre-processing: frames of D3D11 device %p are copied there and shared with the inference device %p",
        static_cast<void *>(device), static_cast<void *>(_gst_device));
    return copy_device;
}

ID3D11Texture2D *ImageInferenceAsyncOpenCL::OpenOnInferenceDevice(GstMemory *memory) {
    // Opened once per pooled texture, which the memory keeps: each open makes a new texture object, which OpenCL would
    // import anew. The pools of the other devices serve this wrapper only, so one quark tells the inference device.
    static const GQuark quark = g_quark_from_static_string("dlstreamer-opencl-texture-on-inference-device");
    if (auto *opened = static_cast<ID3D11Texture2D *>(gst_mini_object_get_qdata(GST_MINI_OBJECT_CAST(memory), quark)))
        return opened;

    auto *texture =
        reinterpret_cast<ID3D11Texture2D *>(gst_d3d11_memory_get_resource_handle(GST_D3D11_MEMORY_CAST(memory)));
    Microsoft::WRL::ComPtr<IDXGIResource1> resource;
    HANDLE handle = nullptr;
    if (FAILED(texture->QueryInterface(IID_PPV_ARGS(&resource))) ||
        FAILED(resource->CreateSharedHandle(nullptr, DXGI_SHARED_RESOURCE_READ | DXGI_SHARED_RESOURCE_WRITE, nullptr,
                                            &handle)) ||
        !handle)
        throw std::runtime_error(
            "OpenCL pre-processing: failed to share the copy of a frame from another D3D11 device");
    Microsoft::WRL::ComPtr<ID3D11Device1> device1;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> opened;
    const bool ok =
        SUCCEEDED(gst_d3d11_device_get_device_handle(_gst_device)->QueryInterface(IID_PPV_ARGS(&device1))) &&
        SUCCEEDED(device1->OpenSharedResource1(handle, IID_PPV_ARGS(&opened)));
    CloseHandle(handle);
    if (!ok)
        throw std::runtime_error("OpenCL pre-processing: failed to open the copy of a frame from another D3D11 device "
                                 "on the inference device");
    gst_mini_object_set_qdata(GST_MINI_OBJECT_CAST(memory), quark, opened.Get(),
                              [](gpointer opened) { static_cast<ID3D11Texture2D *>(opened)->Release(); });
    return opened.Detach();
}

dlstreamer::D3D12TargetPool &ImageInferenceAsyncOpenCL::D3D12PoolFor(const D3D12_RESOURCE_DESC &desc) {
    auto &slot = _d3d12_pools[{desc.Width, desc.Height, desc.Format}];
    if (!slot.pool) {
        slot.pool = std::make_unique<dlstreamer::D3D12TargetPool>(GST_CAT_DEFAULT);
        GVA_INFO("OpenCL pre-processing: shareable copies of %llux%u D3D12 textures of format %u",
                 static_cast<unsigned long long>(desc.Width), desc.Height, desc.Format);
    }
    slot.last_used = _frames_staged;
    return *slot.pool;
}

void ImageInferenceAsyncOpenCL::CountFrame() {
    if (++_frames_staged % kPoolCheckPeriod != 0)
        return;
    // The textures in use stay alive until their frames release them, as does a device with frames in flight.
    auto idle = [this](uint64_t last_used) { return _frames_staged - last_used > kPoolIdleFrames; };
    auto drop_idle = [&](auto &pools, const char *what) {
        for (auto it = pools.begin(); it != pools.end();) {
            if (!idle(it->second.last_used)) {
                ++it;
                continue;
            }
            GVA_INFO("OpenCL pre-processing: dropping the idle pool of %s %llux%u format %u", what,
                     static_cast<unsigned long long>(std::get<0>(it->first)), std::get<1>(it->first),
                     std::get<2>(it->first));
            it = pools.erase(it);
        }
    };
    drop_idle(_staging_pools, "staging textures");
    drop_idle(_d3d12_pools, "shareable D3D12 copies");
    for (auto it = _other_devices.begin(); it != _other_devices.end();) {
        if (idle(it->second->last_used)) {
            GVA_INFO("OpenCL pre-processing: dropping the copies on idle D3D11 device %p",
                     static_cast<void *>(it->second->device));
            it = _other_devices.erase(it);
            continue;
        }
        drop_idle(it->second->pools, "textures shared from another D3D11 device");
        ++it;
    }
}

std::shared_ptr<ImageInferenceAsyncOpenCL::StagingFrame>
ImageInferenceAsyncOpenCL::GetStagingFrame(const Image &src_image) {
    auto *src_mem = static_cast<GstMemory *>(src_image.gst_d3d11_memory);
    if (!src_mem || !gst_is_d3d11_memory(src_mem))
        throw std::invalid_argument("OpenCL pre-processing: the frame has no GstD3D11Memory");
    auto *src_dmem = GST_D3D11_MEMORY_CAST(src_mem);

    std::lock_guard<std::mutex> lock(_staging_mutex);
    // The ROIs of a frame come one after another, and share its copy.
    if (auto last = _last_staging.lock(); last && last->source == src_mem)
        return last;

    // A frame from another device is copied there, into a texture shared with the inference device.
    const bool other_device =
        gst_d3d11_device_get_device_handle(src_dmem->device) != gst_d3d11_device_get_device_handle(_gst_device);
    std::shared_ptr<CopyDevice> copy_device = other_device ? OtherDevice(src_dmem->device) : _own_device;

    D3D11_TEXTURE2D_DESC src_desc = {};
    if (!gst_d3d11_memory_get_texture_desc(src_dmem, &src_desc))
        throw std::runtime_error("OpenCL pre-processing: failed to get the frame's texture desc");
    const guint subresource = gst_d3d11_memory_get_subresource_index(src_dmem);
    const bool readable_in_place = !other_device && (src_desc.MiscFlags & D3D11_RESOURCE_MISC_SHARED) &&
                                   !(src_desc.MiscFlags & D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX) &&
                                   src_desc.ArraySize == 1 && subresource == 0;

    auto frame = std::make_shared<StagingFrame>();
    frame->source = gst_memory_ref(src_mem);
    frame->copy_device = copy_device;
    frame->written = copy_device->AcquireQuery();

    GstMemory *staging = nullptr;
    if (!readable_in_place) {
        D3D11StagingPool &pool = other_device
                                     ? PoolFor(copy_device->pools, copy_device->device, src_desc,
                                               D3D11_RESOURCE_MISC_SHARED | D3D11_RESOURCE_MISC_SHARED_NTHANDLE)
                                     : PoolFor(_staging_pools, _gst_device, src_desc, D3D11_RESOURCE_MISC_SHARED);
        if (gst_d3d11_pool_allocator_acquire_memory(pool.allocator, &staging) != GST_FLOW_OK || !staging)
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
        D3D11DeviceLock device_lock(copy_device->device);
        auto *context = gst_d3d11_device_get_device_context_handle(copy_device->device);
        if (staging)
            context->CopySubresourceRegion(reinterpret_cast<ID3D11Resource *>(dst_map.data), 0, 0, 0, 0,
                                           reinterpret_cast<ID3D11Resource *>(src_map.data), subresource, nullptr);
        // Not flushed: a submission costs a few hundred microseconds of CPU, here under the device lock, and other
        // D3D11 users (the decoder first of all) submit often enough anyway. If not, WaitForStaging() flushes.
        context->End(frame->written.Get());
    }
    if (staging)
        gst_memory_unmap(staging, &dst_map);
    gst_memory_unmap(src_mem, &src_map);

    if (other_device) {
        frame->texture = OpenOnInferenceDevice(staging);
    } else {
        frame->texture_memory = staging ? staging : src_mem;
        frame->texture = reinterpret_cast<ID3D11Texture2D *>(
            gst_d3d11_memory_get_resource_handle(GST_D3D11_MEMORY_CAST(frame->texture_memory)));
    }

    _last_staging = frame;
    CountFrame();
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

    auto frame = std::make_shared<StagingFrame>();
    frame->source = gst_memory_ref(src_mem);
    frame->d3d12 = true;
    // Queued on the D3D12 device. The worker waits for it.
    frame->staging = D3D12PoolFor(desc).copy(src_mem, &info);
    if (!frame->staging)
        throw std::runtime_error("OpenCL pre-processing: failed to copy the D3D12 frame into a shareable texture");
    // Opened once per pooled texture: the memory keeps the view.
    frame->texture = gst_d3d12_memory_get_d3d11_texture(GST_D3D12_MEMORY_CAST(frame->staging),
                                                        gst_d3d11_device_get_device_handle(_gst_device));
    if (!frame->texture)
        throw std::runtime_error("OpenCL pre-processing: failed to open the D3D12 copy of the frame in D3D11");

    _last_staging = frame;
    CountFrame();
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
    // The device the copy was made on: the frame's own, for a frame from another device.
    frame.copy_device->Wait(frame.written.Get());
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
    staged->gst_d3d11_memory = staging->texture_memory;
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
