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
#include "infer_slot_pool.h"
#include "openvino_image_to_tensor_converter.h"
#include "openvino_prepared_model.h"

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

    static std::map<std::string, std::vector<size_t>> GetModelInputShapes(const std::string model_file,
                                                                          const gchar *ov_extension_lib);

    // True if the NPU plugin/driver can consume strided input tensors (NPU_ENABLE_STRIDES_FOR).
    static bool IsNpuStridedInputSupported();

    // Batch size the backend uses when batch-size is 0 (auto): ov::optimal_batch_size, or 1 if not supported.
    static int GetDeviceOptimalBatchSize(const std::string &device);

    bool IsQueueFull() override;

    void Flush() override;

    void Close() override;

  protected:
    struct InferSlot {
        ov::InferRequest infer_request;
        std::vector<IFrameBase::Ptr> buffers;
        std::vector<ov::TensorVector> in_tensors;

        void start_async() {
            return this->infer_request.start_async();
        }
    };

    void HandleError(InferSlot *slot);
    void WorkingFunction(InferSlot *slot);

    dlstreamer::ContextPtr context_;
    InferenceBackend::MemoryType memory_type;
    CallbackFunc callback;
    ErrorHandlingFunc handleError;

    std::string model_path;
    InferenceBackend::OpenVino::PreparedModel prepared_model;
    ov::CompiledModel compiled_model;
    std::unique_ptr<InferenceBackend::OpenVino::ImageToTensorConverter> img_to_tensor;

    std::string model_name;
    std::string image_layer;

    int batch_size;
    int batch_timeout;
    int nireq;
    // All slots live here for as long as the object does; everything else only points to them.
    std::vector<std::unique_ptr<InferSlot>> slots;
    InferSlotPool<InferSlot *> free_slots;

    std::unique_ptr<InferenceBackend::ImagePreprocessor> pre_processor;

    // Threading
    std::mutex requests_mutex_;
    std::atomic<unsigned int> frames_in_flight_;
    std::condition_variable request_processed_;
    std::mutex flush_mutex;

  private:
    void FreeSlot(InferSlot *slot);
    bool DoNeedImagePreProcessing(const InferenceBackend::ImagePtr src_img);
    void SubmitImageProcessing(const std::string &input_name, InferSlot *slot,
                               const InferenceBackend::Image &src_img,
                               const InferenceBackend::InputImageLayerDesc::Ptr &pre_proc_info,
                               const InferenceBackend::ImageTransformationParams::Ptr image_transform_info);
    void BypassImageProcessing(const std::string &input_name, InferSlot *slot,
                               const InferenceBackend::Image &src_img, size_t batch_size);
    void SetCompletionCallback(InferSlot *slot);
    void
    ApplyInputPreprocessors(InferSlot *slot,
                            const std::map<std::string, InferenceBackend::InputLayerDesc::Ptr> &input_preprocessors);
};
