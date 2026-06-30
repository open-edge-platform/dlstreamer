/*******************************************************************************
 * Copyright (C) 2018-2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#pragma once

#include "inference_backend/image_inference.h"
#include "inference_backend/input_image_layer_descriptor.h"
#include "inference_backend/pre_proc.h"

#include <openvino/openvino.hpp>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <gst/gst.h>
#include <map>
#include <string>
#include <thread>

#include "config.h"
#include "safe_queue.h"

class OpenVINOImageInference : public InferenceBackend::ImageInference {
  public:
    OpenVINOImageInference(const InferenceBackend::InferenceConfig &config, InferenceBackend::Allocator *allocator,
                           dlstreamer::ContextPtr context, CallbackFunc callback, ErrorHandlingFunc error_handler,
                           InferenceBackend::MemoryType memory_type);

    ~OpenVINOImageInference();

    void SubmitImage(IFrameBase::Ptr frame,
                     const std::map<std::string, InferenceBackend::InputLayerDesc::Ptr> &input_preprocessors) override;

    const std::string &GetModelName() const override;

    size_t GetBatchSize() const override;
    int GetBatchTimeout() const;
    size_t GetNireq() const override;

    void GetModelImageInputInfo(size_t &width, size_t &height, size_t &batch_size, int &format,
                                int &memory_type) const override;

    std::map<std::string, std::vector<size_t>> GetModelInputsInfo() const override;
    std::map<std::string, std::vector<size_t>> GetModelOutputsInfo() const override;
    std::map<std::string, GstStructure *> GetModelInfoPostproc() const override;
    static std::map<std::string, GstStructure *>
    GetModelInfoPreproc(const std::string model_file, const gchar *pre_proc_config, const gchar *ov_extension_lib);

    bool IsQueueFull() override;

    void Flush() override;

    void Close() override;

  protected:
    std::unique_ptr<class OpenVinoNewApiImpl> _impl;

    struct BatchRequest {
        ov::InferRequest infer_request_new;
        std::vector<IFrameBase::Ptr> buffers;
        std::vector<ov::TensorVector> in_tensors;
        // True for requests created from the batch-1 model used to flush leftover frames of a partial batch
        // (two-model batching). Determines which pool FreeRequest() returns the request to.
        bool single = false;

        void start_async() {
            return this->infer_request_new.start_async();
        }
    };

    void HandleError(const std::shared_ptr<BatchRequest> &request);
    void WorkingFunction(const std::shared_ptr<BatchRequest> &request);

    // Pads a partially filled batch up to batch_size and starts inference asynchronously. Used only on the
    // DL Streamer-side batching path (remote context); the model keeps a static batch dimension and the
    // padded frames are inferred but their results are discarded. Used for full batches, and as a fallback
    // partial-batch flush when the batch-1 model is unavailable (see SubmitPartialAsSingles).
    void StartBatchAsync(const std::shared_ptr<BatchRequest> &request);

    // Flushes a partially filled batch by submitting each leftover frame individually through a batch-1 model
    // (OpenVINO Automatic Batching style), avoiding the wasted compute of padding the batched model. Used on the
    // remote-context (VA) DL Streamer-side batching path when the batch-1 model is available.
    void SubmitPartialAsSingles(const std::shared_ptr<BatchRequest> &request);

    dlstreamer::ContextPtr context_;
    InferenceBackend::MemoryType memory_type;
    CallbackFunc callback;
    ErrorHandlingFunc handleError;

    std::string model_name;
    std::string image_layer;

    int batch_size;
    int batch_timeout;
    int nireq;
    // True when OpenVINO Automatic Batching (BATCH device) is active. In that case batch_size is 1 and the
    // DL Streamer-side partial-batch timeout flush below is not used (the BATCH device honours the timeout).
    bool _auto_batching = false;
    // True when two-model batching is active: a batch-1 model (sharing the remote context) flushes leftover frames
    // of a partial batch individually instead of padding the batched model. Set only on the remote-context (VA) path.
    bool _two_model_batching = false;
    SafeQueue<std::shared_ptr<BatchRequest>> freeRequests;
    // Pool of batch-1 requests used to flush partial batches when _two_model_batching is active.
    SafeQueue<std::shared_ptr<BatchRequest>> freeRequestsSingle;

    std::unique_ptr<InferenceBackend::ImagePreprocessor> pre_processor;

    // Threading
    std::mutex requests_mutex_;
    std::atomic<unsigned int> requests_processing_;
    std::condition_variable request_processed_;
    std::mutex flush_mutex;

    // Batch-timeout support (static batch + padded partial-batch flush).
    // Used only when batch_timeout > -1 and a remote context bypasses OpenVINO Automatic Batching. The model keeps a
    // static batch dimension and a partially filled batch is padded to batch_size and submitted once batch_timeout
    // milliseconds elapse since the batch was started.
    // _partial_request and _partial_deadline are guarded by _partial_mutex which is the only lock the
    // timeout thread takes (lock order: requests_mutex_ -> _partial_mutex).
    std::mutex _partial_mutex;
    std::condition_variable _timeout_cv;
    std::shared_ptr<BatchRequest> _partial_request;
    std::chrono::steady_clock::time_point _partial_deadline;
    bool _timeout_stop = false;
    std::thread _timeout_thread;

    void TimeoutLoop();
    void StopTimeoutThread();

  private:
    void FreeRequest(std::shared_ptr<BatchRequest> request);
    bool DoNeedImagePreProcessing(const InferenceBackend::ImagePtr src_img);
    void SubmitImageProcessing(const std::string &input_name, std::shared_ptr<BatchRequest> request,
                               const InferenceBackend::Image &src_img,
                               const InferenceBackend::InputImageLayerDesc::Ptr &pre_proc_info,
                               const InferenceBackend::ImageTransformationParams::Ptr image_transform_info);
    void BypassImageProcessing(const std::string &input_name, std::shared_ptr<BatchRequest> request,
                               const InferenceBackend::Image &src_img, size_t batch_size);
    void SetCompletionCallback(std::shared_ptr<BatchRequest> &batch_request);
    void
    ApplyInputPreprocessors(std::shared_ptr<BatchRequest> &request,
                            const std::map<std::string, InferenceBackend::InputLayerDesc::Ptr> &input_preprocessors);
};
