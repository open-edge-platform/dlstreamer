/*******************************************************************************
 * Copyright (C) 2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#pragma once

#include "inference_backend/image_inference.h"
#include "inference_backend/logger.h"
#include "inference_backend/pre_proc.h"

#include <atomic>
#include <condition_variable>
#include <map>
#include <memory>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

#ifndef GST_USE_UNSTABLE_API
#define GST_USE_UNSTABLE_API
#endif
#include <gst/d3d11/gstd3d11.h>

#include <d3d11_4.h>
#include <wrl/client.h>

namespace InferenceBackend {

class OpenCLPreproc;

// Experimental: pre-processes D3D11 frames with OpenCL ("pre-process-backend=opencl").
//
// Where the D3D11 surface sharing backend converts a frame with GstD3D11Converter into a BGRA
// texture, copies that into an OpenCL buffer and leaves the channel drop, layout change and type
// conversion to OpenVINO's pre-processing, this backend does all of it in one OpenCL kernel: colour
// conversion, crop, resize and padding, written straight into the model's planar u8 input tensor,
// an OpenCL buffer of OpenVINO's GPU context.
//
// OpenCL reads only shared D3D11 textures correctly, which decoder surfaces are not, so each frame is
// first copied on the GPU into a pooled shared texture (once per frame; the ROIs of a frame share
// the copy). The kernel runs when OpenVINO asks for the input tensor, on the worker thread. The
// OpenCL context, which OpenVINO uses too, leaves D3D11 sync to us (CL_CONTEXT_INTEROP_USER_SYNC),
// which saves the CPU-side GPU waits its acquire would do otherwise.
class ImageInferenceAsyncOpenCL : public ImageInference {
  public:
    ImageInferenceAsyncOpenCL(const InferenceBackend::InferenceConfig &config, dlstreamer::ContextPtr d3d11_context,
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
    struct OutputSlot;
    struct QueryPool;

    GstD3D11Device *_gst_device = nullptr;
    dlstreamer::ContextPtr _device_context_storage;

    uint32_t _dst_width = 0, _dst_height = 0;
    int _dst_fourcc = 0;
    bool _bgr = true;

    ImageInference::Ptr _inference;

    // Shared copies of the frames, recycled.
    GstD3D11PoolAllocator *_staging_pool = nullptr;
    D3D11_TEXTURE2D_DESC _staging_desc = {};
    // The copy of the latest frame, for its next ROI.
    std::weak_ptr<StagingFrame> _last_staging;
    std::mutex _staging_mutex;

    std::shared_ptr<StagingFrame> GetStagingFrame(const Image &src_image);
    void EnsureStagingPool(const D3D11_TEXTURE2D_DESC &src_desc);
    void DropStagingPool();

    // OpenVINO's OpenCL context is created with CL_CONTEXT_INTEROP_USER_SYNC, so the kernel has to
    // be ordered after the D3D11 copy by us. An event query ended after each copy tells whether it
    // is done; only when it is not, the work is flushed and waited for with the fence.
    std::shared_ptr<QueryPool> _query_pool;
    Microsoft::WRL::ComPtr<ID3D11Fence> _fence;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext4> _context4;
    uint64_t _fence_value = 0; // guarded by the D3D11 device lock
    HANDLE _fence_event = nullptr;

    void WaitForStaging(const StagingFrame &frame);

    // Bound to OpenVINO's cl_context on the first frame.
    std::unique_ptr<OpenCLPreproc> _preproc;
    std::mutex _preproc_mutex;
    OpenCLPreproc &GetPreproc(void *cl_ctx);

    // Persistent worker thread + bounded queue for OV SubmitImage, as in ImageInferenceAsyncD3D11:
    // OV's SubmitImage can block on a free request, and it is where the kernel runs.
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
    bool _worker_busy = false; // guarded by _queue_mutex
    ErrorHandlingFunc _error_handler;

    void WorkerLoop();
};

} // namespace InferenceBackend
