/*******************************************************************************
 * Copyright (C) 2025-2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#pragma once

#include "config.h"

#include "inference_backend/image_inference.h"
#include "inference_backend/logger.h"
#include "inference_backend/pre_proc.h"

#include <atomic>
#include <condition_variable>
#include <functional>
#include <map>
#include <mutex>
#include <queue>
#include <thread>
#include <tuple>
#include <vector>

#include <gst/d3d11/gstd3d11.h>
#include <wrl/client.h>

struct ID3D11Device;

namespace InferenceBackend {

class D3D11Converter;
class D3D11SurfaceSharing;

class ImageInferenceAsyncD3D11 : public ImageInference {
  public:
    ImageInferenceAsyncD3D11(const InferenceBackend::InferenceConfig &config, dlstreamer::ContextPtr d3d11_context,
                             ImageInference::Ptr inference, ErrorHandlingFunc error_handler);

    ~ImageInferenceAsyncD3D11() override;

    ImageInferenceAsyncD3D11(const ImageInferenceAsyncD3D11 &) = delete;
    ImageInferenceAsyncD3D11 &operator=(const ImageInferenceAsyncD3D11 &) = delete;

    void SubmitImage(IFrameBase::Ptr frame,
                     const std::map<std::string, InputLayerDesc::Ptr> &input_preprocessors) override;

    const std::string &GetModelName() const override;

    size_t GetBatchSize() const override;
    size_t GetNireq() const override;

    void GetModelImageInputInfo(size_t &width, size_t &height, size_t &batch_size, int &format,
                                int &memory_type) const override;

    std::map<std::string, std::vector<size_t>> GetModelInputsInfo() const override;
    std::map<std::string, std::vector<size_t>> GetModelOutputsInfo() const override;
    std::map<std::string, GstStructure *> GetModelInfoPostproc() const override;

    bool IsQueueFull() override;

    void Flush() override;

    void Close() override;

  private:
    GstD3D11Device *_gst_device = nullptr;
    dlstreamer::ContextPtr _device_context_storage;

    D3D11_TEXTURE2D_DESC _dst_texture_desc = {};
    DXGI_FORMAT _dst_format = DXGI_FORMAT_UNKNOWN;
    int _dst_fourcc = 0;
    uint32_t _dst_width = 0, _dst_height = 0;

    // Converters, one per source size/format: streams of different resolutions can share a
    // model instance, and rebuilding the converter on every switch is expensive.
    using ConverterKey = std::tuple<uint32_t, uint32_t, DXGI_FORMAT>;
    using ConverterMap = std::map<ConverterKey, std::unique_ptr<D3D11Converter>>;
    ConverterMap _converters; // on the dst device
    std::mutex _converter_mutex;

    D3D11Converter &GetConverter(ConverterMap &converters, GstD3D11Device *device, uint32_t src_width,
                                 uint32_t src_height, DXGI_FORMAT src_format);

    // Texture pool used by same-device path (on dst device) and cross-device
    // paths (per-source-device, with SHARED flags). Acquire blocks until a
    // texture is available; Release returns it and wakes waiters.
    struct TexturePool {
        std::vector<GstMemory *> free;
        std::mutex mutex;
        std::condition_variable cv;

        GstMemory *Acquire();
        void Release(GstMemory *mem);
    };
    TexturePool _tex_pool;

    // Cross-device: per-source-device converters + texture pool (with SHARED flags).
    // Only instantiated when source and dst devices differ.
    struct CrossDeviceResources {
        GstD3D11Device *device = nullptr;
        bool owns_device = false;
        ConverterMap converters;
        TexturePool tex_pool;
        // Syncs the conversion on the source device before the dst device reads the texture.
        GstD3D11Fence *fence = nullptr;
        // Pooled textures opened on the dst device, keyed by the pooled GstMemory.
        std::map<GstMemory *, Microsoft::WRL::ComPtr<ID3D11Texture2D>> shared_textures;

        CrossDeviceResources() = default;
        CrossDeviceResources(const CrossDeviceResources &) = delete;
        CrossDeviceResources &operator=(const CrossDeviceResources &) = delete;
        ~CrossDeviceResources();
    };
    std::map<ID3D11Device *, std::unique_ptr<CrossDeviceResources>> _cross_device_resources;
    std::mutex _cross_device_mutex;

    CrossDeviceResources &GetCrossDeviceResources(GstD3D11Device *src_gst_device);

    ImageInference::Ptr _inference;
    bool _surface_sharing = false;

    // D3D11 surface sharing (BGRA texture -> OpenCL image -> CL buffer).
    // Lazily bound to the consumer's cl_context on first SubmitImage.
    std::unique_ptr<D3D11SurfaceSharing> _surface_sharing_obj;
    std::mutex _surface_sharing_mutex;
    D3D11SurfaceSharing *GetOrCreateSurfaceSharing(void *cl_ctx);

    // Persistent worker thread + bounded queue for OV SubmitImage.
    // OV::SubmitImage can block on freeRequests.pop(), the single worker thread absorbs
    // that blocking so streaming threads aren't stalled.
    struct SubmitTask {
        IFrameBase::Ptr frame;
        std::map<std::string, InputLayerDesc::Ptr> input_preprocessors;
    };
    std::queue<SubmitTask> _submit_queue;
    std::mutex _queue_mutex;
    std::condition_variable _queue_cv;          // signals worker: new task available
    std::condition_variable _queue_not_full_cv; // signals streaming thread: queue has space
    std::condition_variable _queue_empty_cv;    // signals Flush: queue drained
    size_t _submit_queue_max = 0;
    std::thread _worker_thread;
    std::atomic<bool> _worker_running{false};
    bool _worker_busy = false; // worker is submitting a popped task; guarded by _queue_mutex
    // Reports frames that never reached an infer request, so the element still pushes them.
    ErrorHandlingFunc _error_handler;

    void WorkerLoop();

    // SubmitImage helpers
    using ReleaseFunc = std::function<void(GstMemory *)>;
    GstBuffer *WrapSourceTexture(const Image &src_image, GstD3D11Device *device);
    // cross_res is null when the source is on the dst device.
    void ConvertAndBuildImage(IFrameBase::Ptr &frame, GstD3D11Device *conv_device, D3D11Converter &converter,
                              CrossDeviceResources *cross_res, GstMemory *dst_mem, ReleaseFunc release_mem,
                              const std::map<std::string, InputLayerDesc::Ptr> &input_preprocessors);
    void BuildSurfaceSharingImage(IFrameBase::Ptr &frame, GstMemory *dst_mem, ReleaseFunc release_mem);
    void BuildSystemMemoryImage(IFrameBase::Ptr &frame, GstMemory *dst_mem, ReleaseFunc release_mem);
    void ShareTextureToDstDevice(IFrameBase::Ptr &frame, CrossDeviceResources &cross_res, GstMemory *dst_mem,
                                 ReleaseFunc release_mem);
};

} // namespace InferenceBackend
