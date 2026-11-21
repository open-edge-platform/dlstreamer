/*******************************************************************************
 * Copyright (C) 2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#pragma once

#include "inference_backend/image_inference.h"
#include "inference_backend/logger.h"

#include <atomic>
#include <condition_variable>
#include <map>
#include <memory>
#include <mutex>
#include <queue>
#include <thread>

#ifndef GST_USE_UNSTABLE_API
#define GST_USE_UNSTABLE_API
#endif
#include <gst/d3d11/gstd3d11.h>
#include <gst/d3d12/gstd3d12.h>

#include <d3d11_4.h>
#include <wrl/client.h>

#include <tuple>

namespace dlstreamer {
class D3D12TargetPool;
}

namespace InferenceBackend {

// D3D11 and D3D12 frames for the OpenCL pre-processing (pre-process-backend=opencl, Windows).
//
// OpenCL reads only single textures created with D3D11_RESOURCE_MISC_SHARED correctly, which decoder textures are
// not. So each frame is first copied on the GPU into a pooled shared texture, on the streaming thread, once per frame
// (its ROIs share the copy). The OpenCL context is created with CL_CONTEXT_INTEROP_USER_SYNC, which saves the CPU
// waits an acquire does otherwise, so the kernel must be ordered after the copy here: an event query is ended after
// each copy, without a submission, and checked on a worker thread, which then submits the frame to the inference,
// whose OpenclPreprocessor pre-processes it. By then other D3D11 users, the decoder first, have submitted the copy.
//
// OpenCL can't import D3D12 textures. A D3D12 frame is copied on the GPU into a pooled texture that D3D11 can open
// (dlstreamer::D3D12TargetPool), and the pre-processing reads the D3D11 view of the copy, as a D3D11 frame, on the
// D3D11 device of the inference, which must be on the frame's adapter. OpenCL sees no D3D12 fences: the worker thread
// waits for the copy on the CPU.
class ImageInferenceAsyncOpenCL : public ImageInference {
  public:
    // d3d11_context: the D3D11 device OpenCL reads the frames through, which `inference` was created with (see
    // D3D11ContextFor()).
    ImageInferenceAsyncOpenCL(const InferenceConfig &config, dlstreamer::ContextPtr d3d11_context,
                              ImageInference::Ptr inference, ErrorHandlingFunc error_handler);
    ~ImageInferenceAsyncOpenCL() override;

    // The D3D11 device OpenCL reads the frames of `frames_context` through: the frames' own for D3D11 frames, the
    // one of their adapter for D3D12 frames (dlstreamer::d3d11_device_for()).
    static dlstreamer::ContextPtr D3D11ContextFor(const dlstreamer::ContextPtr &frames_context);

    // The copies of the frames come from a pool per texture size and format, so that streams of different sizes
    // sharing the instance don't make a pool start over at every frame. A pool that none of the last kPoolIdleFrames
    // frames used is dropped (checked every kPoolCheckPeriod frames), with its idle textures.
    static constexpr uint64_t kPoolIdleFrames = 1024;
    static constexpr uint64_t kPoolCheckPeriod = 64;

    ImageInferenceAsyncOpenCL(const ImageInferenceAsyncOpenCL &) = delete;
    ImageInferenceAsyncOpenCL &operator=(const ImageInferenceAsyncOpenCL &) = delete;

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
    struct StagingFrame;
    struct QueryPool;
    struct D3D11StagingPool;

    // A pool, with the count of frames when it was last used.
    template <typename Pool>
    struct PoolSlot {
        std::unique_ptr<Pool> pool;
        uint64_t last_used = 0;
    };

    std::shared_ptr<StagingFrame> GetStagingFrame(const Image &src_image);
    std::shared_ptr<StagingFrame> GetD3D12StagingFrame(const Image &src_image);
    D3D11StagingPool &StagingPoolFor(const D3D11_TEXTURE2D_DESC &src_desc);
    dlstreamer::D3D12TargetPool &D3D12PoolFor(const D3D12_RESOURCE_DESC &desc);
    // Counts a frame staged (copied or read in place), and drops the pools idle for kPoolIdleFrames frames. Under
    // _staging_mutex.
    void CountFrame();
    void WaitForStaging(StagingFrame &frame);
    void WorkerLoop();

    GstD3D11Device *_gst_device = nullptr;
    gint64 _adapter_luid = 0; // of _gst_device
    // The latest D3D12 device of the frames, found on the adapter of _gst_device.
    GstD3D12Device *_d3d12_device_checked = nullptr;
    dlstreamer::ContextPtr _device_context_storage;
    ImageInference::Ptr _inference;
    ErrorHandlingFunc _error_handler;

    // Shared copies of D3D11 frames and shareable copies of D3D12 frames, recycled (see kPoolIdleFrames). Guarded by
    // _staging_mutex, as is the rest of this block.
    using D3D11PoolKey = std::tuple<UINT, UINT, DXGI_FORMAT>;
    std::map<D3D11PoolKey, PoolSlot<D3D11StagingPool>> _staging_pools;
    using D3D12PoolKey = std::tuple<UINT64, UINT, DXGI_FORMAT>;
    std::map<D3D12PoolKey, PoolSlot<dlstreamer::D3D12TargetPool>> _d3d12_pools;
    uint64_t _frames_staged = 0;
    // The copy of the latest frame, for its next ROI.
    std::weak_ptr<StagingFrame> _last_staging;
    std::mutex _staging_mutex;

    std::shared_ptr<QueryPool> _query_pool;
    Microsoft::WRL::ComPtr<ID3D11Fence> _fence;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext4> _context4;
    uint64_t _fence_value = 0; // guarded by the D3D11 device lock
    HANDLE _fence_event = nullptr;

    // Bounded queue (batch x nireq) to the worker thread, which submits to the inference.
    struct SubmitTask {
        IFrameBase::Ptr frame;
        std::map<std::string, InputLayerDesc::Ptr> input_preprocessors;
        std::shared_ptr<StagingFrame> staging;
    };
    std::queue<SubmitTask> _submit_queue;
    std::mutex _queue_mutex;
    std::condition_variable _queue_cv;
    std::condition_variable _queue_not_full_cv;
    std::condition_variable _queue_empty_cv;
    size_t _submit_queue_max = 0;
    bool _worker_busy = false; // guarded by _queue_mutex
    std::atomic<bool> _worker_running{false};
    std::thread _worker_thread;
};

} // namespace InferenceBackend
