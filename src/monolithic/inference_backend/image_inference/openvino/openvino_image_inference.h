/*******************************************************************************
 * Copyright (C) 2018-2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

// OpenVinoImageInference: the OpenVINO implementation of InferenceBackend::ImageInference, created by the factory in
// image_inference.cpp. Start here.
//
// Set-up (the constructor, in openvino_image_inference.cpp), in this order:
//   1. PrepareModel()           ov::Model and config -> PreparedModel: reshape, layouts, pre-processing, batch size.
//   2. CreateRemoteContext()    GPU surface sharing only: OpenVINO context made from the pipeline's VA-API/D3D context.
//   3. CompileModelForDevice()  PreparedModel -> ov::CompiledModel on the device.
//   4. One InferSlot per request: an infer request plus the batch of frames being filled for it. A slot is empty (in
//      the InferSlotPool), being filled (current_slot_) or running inference.
//
// Run time:
//   SubmitImage()        (openvino_image_inference_submit.cpp) takes the current slot, or waits for an empty one;
//                        puts the frame into the slot's input tensors, by software pre-processing or with
//                        ImageToTensorConverter, which wraps the image memory without copying it; starts the request
//                        when the batch is full.
//   completion callback  (openvino_image_inference.cpp, on an OpenVINO thread) passes the outputs and the frames on to
//                        the callback, then FreeSlot() returns the slot to the pool.
//   Flush(), Close()     (openvino_image_inference.cpp) start the partly filled batch and wait until no frame is in
//                        flight.
//
// Modules. An arrow means "uses"; none of them includes this header.
//   openvino_config_helper.h              typed view of the string-keyed InferenceConfig
//   openvino_core.h                       the shared ov::Core and device queries
//   openvino_prepared_model.*             -> config_helper, core
//   openvino_model_compilation.*          -> prepared_model, config_helper, core
//   openvino_image_to_tensor_converter.*  -> core
//   infer_slot_pool.h                     the pool of empty slots; depends on nothing here
//   openvino_formatters.h, openvino_blob_wrapper.h   fmt formatters; ov::Tensor as InputBlob/OutputBlob
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

class OpenVinoImageInference : public InferenceBackend::ImageInference {
  public:
    OpenVinoImageInference(const InferenceBackend::InferenceConfig &config, InferenceBackend::Allocator *allocator,
                           dlstreamer::ContextPtr context, CallbackFunc callback, ErrorHandlingFunc error_handler,
                           InferenceBackend::MemoryType memory_type);

    ~OpenVinoImageInference();

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

    bool IsQueueFull() override;

    void Flush() override;

    void Close() override;

  protected:
    struct InferSlot {
        ov::InferRequest infer_request;
        std::vector<IFrameBase::Ptr> buffers;
        std::vector<ov::TensorVector> in_tensors;

        void StartAsync() {
            return infer_request.start_async();
        }
    };

    void HandleError(InferSlot *slot);
    void WorkingFunction(InferSlot *slot);

    dlstreamer::ContextPtr context_;
    InferenceBackend::MemoryType memory_type_;
    CallbackFunc callback_;
    ErrorHandlingFunc handle_error_;

    std::string model_path_;
    InferenceBackend::OpenVino::PreparedModel prepared_model_;
    ov::CompiledModel compiled_model_;
    std::unique_ptr<InferenceBackend::OpenVino::ImageToTensorConverter> img_to_tensor_;

    std::string model_name_;
    std::string image_layer_;

    int batch_size_;
    int batch_timeout_;
    int nireq_;
    // All slots live here for as long as the object does; everything else only points to them.
    std::vector<std::unique_ptr<InferSlot>> slots_;
    // Empty slots only. A slot being filled is current_slot_; a slot running inference is in neither.
    InferSlotPool<InferSlot *> free_slots_;
    // The slot that takes the next frame, null if none. Written under requests_mutex_; atomic so that IsQueueFull() can
    // read it without the lock.
    std::atomic<InferSlot *> current_slot_{nullptr};

    std::unique_ptr<InferenceBackend::ImagePreprocessor> pre_processor_;

    // Threading
    std::mutex requests_mutex_;
    std::atomic<unsigned int> frames_in_flight_;
    std::condition_variable request_processed_;
    std::mutex flush_mutex_;

  private:
    void FreeSlot(InferSlot *slot);
    bool DoNeedImagePreProcessing(const InferenceBackend::ImagePtr src_img);
    void SubmitImageProcessing(const std::string &input_name, InferSlot *slot, const InferenceBackend::Image &src_img,
                               const InferenceBackend::InputImageLayerDesc::Ptr &pre_proc_info,
                               const InferenceBackend::ImageTransformationParams::Ptr image_transform_info);
    void BypassImageProcessing(const std::string &input_name, InferSlot *slot, const InferenceBackend::Image &src_img,
                               size_t batch_size);
    void SetCompletionCallback(InferSlot *slot);
    void
    ApplyInputPreprocessors(InferSlot *slot,
                            const std::map<std::string, InferenceBackend::InputLayerDesc::Ptr> &input_preprocessors);
};
