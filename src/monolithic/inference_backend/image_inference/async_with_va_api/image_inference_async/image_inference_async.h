/*******************************************************************************
 * Copyright (C) 2018-2025 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#pragma once

#include "config.h"

#include "thread_pool.h"

#include "inference_backend/image_inference.h"
#include "inference_backend/logger.h"
#include "inference_backend/pre_proc.h"

#include <map>

namespace InferenceBackend {

class VaApiConverter;
class VaApiImagePool;
class VaApiImage;
class VaApiContext;

class ImageInferenceAsync : public ImageInference {
  public:
    constexpr static size_t DEFAULT_THREAD_POOL_SIZE = 5;

    ImageInferenceAsync(const InferenceBackend::InferenceConfig &config, dlstreamer::ContextPtr vadpy_context,
                        ImageInference::Ptr inference, ErrorHandlingFunc error_handler = nullptr);

    ~ImageInferenceAsync() override;

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
    // vaapi image processing
    std::unique_ptr<VaApiContext> _va_context;
    std::unique_ptr<VaApiConverter> _va_converter;
    std::shared_ptr<VaApiImagePool> _va_image_pool;

    ImageInference::Ptr _inference;
    ErrorHandlingFunc _error_handler;

    std::unique_ptr<ThreadPool> _thread_pool;

    void SubmitInference(VaApiImage *va_api_image, IFrameBase::Ptr frame,
                         const std::map<std::string, InputLayerDesc::Ptr> &input_preprocessors);
};

} // namespace InferenceBackend
