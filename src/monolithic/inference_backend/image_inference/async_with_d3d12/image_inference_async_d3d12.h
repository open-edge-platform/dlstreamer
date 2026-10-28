/*******************************************************************************
 * Copyright (C) 2026 Intel Corporation
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

#include <gst/d3d12/gstd3d12.h>

struct ID3D12Device;

namespace InferenceBackend {

class D3D12Converter;
class D3D12SurfaceSharing;

// D3D12 twin of ImageInferenceAsyncD3D11. Converts incoming D3D12 frames to the
// model input size/format with GstD3D12Converter into a pooled texture, then either
// maps that texture to system memory ("d3d12") or hands it to OpenVINO through
// OpenCL external-memory import of a shared D3D12 buffer ("d3d12-surface-sharing").
class ImageInferenceAsyncD3D12 : public ImageInference {
  public:
    ImageInferenceAsyncD3D12(const InferenceBackend::InferenceConfig &config, dlstreamer::ContextPtr d3d12_context,
                             ImageInference::Ptr inference, ErrorHandlingFunc error_handler);

    ~ImageInferenceAsyncD3D12() override;

    ImageInferenceAsyncD3D12(const ImageInferenceAsyncD3D12 &) = delete;
    ImageInferenceAsyncD3D12 &operator=(const ImageInferenceAsyncD3D12 &) = delete;

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
    GstD3D12Device *_gst_device = nullptr;
    dlstreamer::ContextPtr _device_context_storage;

    D3D12_RESOURCE_DESC _dst_desc = {};
    DXGI_FORMAT _dst_format = DXGI_FORMAT_UNKNOWN;
    int _dst_fourcc = 0;
    uint32_t _dst_width = 0, _dst_height = 0;
    // Row pitch of the CPU staging copy (256-byte aligned by D3D12), for the system-memory path.
    uint32_t _dst_row_pitch = 0;

    // Converters, one per source size/format: streams of different resolutions can share a
    // model instance, and rebuilding the converter on every switch is expensive.
    using ConverterKey = std::tuple<uint32_t, uint32_t, DXGI_FORMAT>;
    using ConverterMap = std::map<ConverterKey, std::unique_ptr<D3D12Converter>>;
    ConverterMap _converters; // on the dst device
    std::mutex _converter_mutex;

    D3D12Converter &GetConverter(ConverterMap &converters, GstD3D12Device *device, uint32_t src_width,
                                 uint32_t src_height, DXGI_FORMAT src_format);

    struct TexturePool {
        std::vector<GstMemory *> free;
        std::mutex mutex;
        std::condition_variable cv;

        GstMemory *Acquire();
        void Release(GstMemory *mem);
    };
    TexturePool _tex_pool;

    // Source frames may live on another GstD3D12Device (different adapter or a
    // decoder that did not share its device). Convert on that device instead.
    struct CrossDeviceResources {
        GstD3D12Device *device = nullptr;
        ConverterMap converters;
        TexturePool tex_pool;
    };
    std::map<ID3D12Device *, std::unique_ptr<CrossDeviceResources>> _cross_device_resources;
    std::mutex _cross_device_mutex;

    CrossDeviceResources &GetCrossDeviceResources(GstD3D12Device *src_gst_device);

    ImageInference::Ptr _inference;
    bool _surface_sharing = false;

    // Lazily bound to the consumer's cl_context on first SubmitImage.
    std::unique_ptr<D3D12SurfaceSharing> _surface_sharing_obj;
    std::mutex _surface_sharing_mutex;
    D3D12SurfaceSharing *GetOrCreateSurfaceSharing(void *cl_ctx);

    struct SubmitTask {
        IFrameBase::Ptr frame;
        std::map<std::string, InputLayerDesc::Ptr> input_preprocessors;
    };
    std::queue<SubmitTask> _submit_queue;
    std::mutex _queue_mutex;
    std::condition_variable _queue_cv;
    std::condition_variable _queue_not_full_cv;
    std::condition_variable _queue_empty_cv;
    size_t _submit_queue_max = 0;
    std::thread _worker_thread;
    std::atomic<bool> _worker_running{false};
    bool _worker_busy = false; // worker is submitting a popped task; guarded by _queue_mutex
    // Reports frames that never reached an infer request, so the element still pushes them.
    ErrorHandlingFunc _error_handler;

    void WorkerLoop();

    using ReleaseFunc = std::function<void(GstMemory *)>;
    GstBuffer *WrapSourceFrame(const Image &src_image, GstD3D12Device *device);
    void ConvertAndBuildImage(IFrameBase::Ptr &frame, GstD3D12Device *conv_device, D3D12Converter &converter,
                              GstMemory *dst_mem, ReleaseFunc release_mem,
                              const std::map<std::string, InputLayerDesc::Ptr> &input_preprocessors);
    void BuildSurfaceSharingImage(IFrameBase::Ptr &frame, GstD3D12Device *device, D3D12Converter &converter,
                                  GstMemory *dst_mem, uint64_t fence_value, ReleaseFunc release_mem);
    void BuildSystemMemoryImage(IFrameBase::Ptr &frame, GstMemory *dst_mem, ReleaseFunc release_mem);
};

} // namespace InferenceBackend
