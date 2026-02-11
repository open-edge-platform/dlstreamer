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

        void start_async() {
            return this->infer_request_new.start_async();
        }
    };

    void HandleError(const std::shared_ptr<BatchRequest> &request);
    void WorkingFunction(const std::shared_ptr<BatchRequest> &request);

    dlstreamer::ContextPtr context_;
    InferenceBackend::MemoryType memory_type;
    CallbackFunc callback;
    ErrorHandlingFunc handleError;

    std::string model_name;
    std::string image_layer;

    int batch_size;
    int batch_timeout;
    int nireq;
    SafeQueue<std::shared_ptr<BatchRequest>> freeRequests;

    std::unique_ptr<InferenceBackend::ImagePreprocessor> pre_processor;

    // Threading
    std::mutex requests_mutex_;
    std::atomic<unsigned int> requests_processing_;
    std::condition_variable request_processed_;
    std::mutex flush_mutex;

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
