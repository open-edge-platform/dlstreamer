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

#include <d3d11_4.h>
#include <wrl/client.h>

namespace InferenceBackend {

// D3D11 frames for the OpenCL pre-processing (pre-process-backend=opencl, Windows).
//
// OpenCL reads only single textures created with D3D11_RESOURCE_MISC_SHARED correctly, which decoder textures are
// not. So each frame is first copied on the GPU into a pooled shared texture, on the streaming thread, once per frame
// (its ROIs share the copy). The OpenCL context is created with CL_CONTEXT_INTEROP_USER_SYNC, which saves the CPU
// waits an acquire does otherwise, so the kernel must be ordered after the copy here: an event query is ended after
// each copy, without a submission, and checked on a worker thread, which then submits the frame to the inference,
// whose OpenclPreprocessor pre-processes it. By then other D3D11 users, the decoder first, have submitted the copy.
class ImageInferenceAsyncOpenCL : public ImageInference {
  public:
    ImageInferenceAsyncOpenCL(const InferenceConfig &config, dlstreamer::ContextPtr d3d11_context,
                              ImageInference::Ptr inference, ErrorHandlingFunc error_handler);
    ~ImageInferenceAsyncOpenCL() override;

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

    std::shared_ptr<StagingFrame> GetStagingFrame(const Image &src_image);
    void EnsureStagingPool(const D3D11_TEXTURE2D_DESC &src_desc);
    void DropStagingPool();
    void WaitForStaging(StagingFrame &frame);
    void WorkerLoop();

    GstD3D11Device *_gst_device = nullptr;
    dlstreamer::ContextPtr _device_context_storage;
    ImageInference::Ptr _inference;
    ErrorHandlingFunc _error_handler;

    // Shared copies of the frames, recycled.
    GstD3D11PoolAllocator *_staging_pool = nullptr;
    D3D11_TEXTURE2D_DESC _staging_desc = {};
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
