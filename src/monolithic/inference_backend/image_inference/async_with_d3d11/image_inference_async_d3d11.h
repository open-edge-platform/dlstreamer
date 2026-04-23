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
#include <vector>

#include <gst/d3d11/gstd3d11.h>

struct ID3D11Device;

namespace InferenceBackend {

class D3D11Converter;

class ImageInferenceAsyncD3D11 : public ImageInference {
  public:
    ImageInferenceAsyncD3D11(const InferenceBackend::InferenceConfig &config, dlstreamer::ContextPtr d3d11_context,
                             ImageInference::Ptr inference);

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
    uint32_t _dst_width = 0, _dst_height = 0;

    // Converter on the dst device. Created lazily; recreated on source dimension/format change.
    std::unique_ptr<D3D11Converter> _converter;
    std::mutex _converter_mutex;

    void EnsureConverter(uint32_t src_width, uint32_t src_height, DXGI_FORMAT src_format);

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

    // Cross-device: per-source-device converter + texture pool (with SHARED flags).
    // Only instantiated when source and dst devices differ.
    struct CrossDeviceResources {
        GstD3D11Device *device = nullptr;
        bool owns_device = false;
        std::unique_ptr<D3D11Converter> converter;
        TexturePool tex_pool;
    };
    std::map<ID3D11Device *, std::unique_ptr<CrossDeviceResources>> _cross_device_resources;
    std::mutex _cross_device_mutex;

    CrossDeviceResources &GetCrossDeviceResources(GstD3D11Device *src_gst_device, uint32_t src_width,
                                                  uint32_t src_height, DXGI_FORMAT src_format);

    ImageInference::Ptr _inference;
    bool _surface_sharing = false;

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

    void WorkerLoop();

    // SubmitImage helpers
    using ReleaseFunc = std::function<void(GstMemory *)>;
    GstBuffer *WrapSourceTexture(const Image &src_image, GstD3D11Device *device);
    void ConvertAndBuildImage(IFrameBase::Ptr &frame, GstD3D11Device *conv_device, D3D11Converter &converter,
                              GstMemory *dst_mem, ReleaseFunc release_mem,
                              const std::map<std::string, InputLayerDesc::Ptr> &input_preprocessors);
    void BuildSurfaceSharingImage(IFrameBase::Ptr &frame, GstMemory *dst_mem, ReleaseFunc release_mem);
    void BuildSystemMemoryImage(IFrameBase::Ptr &frame, GstMemory *dst_mem, ReleaseFunc release_mem);
    void ShareTextureToDstDevice(IFrameBase::Ptr &frame, GstMemory *dst_mem, ReleaseFunc release_mem);
};

} // namespace InferenceBackend
