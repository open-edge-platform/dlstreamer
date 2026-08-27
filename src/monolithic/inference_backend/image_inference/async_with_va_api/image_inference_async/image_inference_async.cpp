/*******************************************************************************
 * Copyright (C) 2018-2025 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#include "environment_variable_options_reader.h"
#include "feature_toggling/ifeature_toggle.h"
#include "inference_backend/input_image_layer_descriptor.h"
#include "runtime_feature_toggler.h"

#include "image_inference_async.h"
#include "inference_backend/image_inference.h"
#include "utils.h"

#include "vaapi_context.h"
#include "vaapi_converter.h"
#include "vaapi_images.h"

#include "safe_arithmetic.hpp"

#include <future>
#include <string>
#include <tuple>
#include <utility>

using namespace InferenceBackend;

namespace {

CREATE_FEATURE_TOGGLE(VaapiPreprocYUVToggle, "vaapi-preproc-yuv",
                      "Vaapi pre-proc with RGBP output may be not high-performant on some systems. Please set "
                      "environment variable ENABLE_GVA_FEATURES=vaapi-preproc-yuv to enable I420 output for vaapi "
                      "pre-proc and see if it enables better performance. ")

std::unique_ptr<VaApiImagePool> create_va_api_image_pool(VaApiImagePool::ImageInfo info, size_t pool_size,
                                                         VaApiContext *context, float vdbox_sfc_pipe_part) {

    // If ENABLE_GVA_FEATURES=vaapi-preproc-yuv set, then VA pipeline ends with scaled I420 image and I420->RGBP CSC
    // happens with OpenCV later.
    auto feature_toggler = std::unique_ptr<FeatureToggling::Runtime::RuntimeFeatureToggler>(
        new FeatureToggling::Runtime::RuntimeFeatureToggler());
    FeatureToggling::Runtime::EnvironmentVariableOptionsReader env_var_options_reader;
    feature_toggler->configure(env_var_options_reader.read("ENABLE_GVA_FEATURES"));
    if (feature_toggler->enabled(VaapiPreprocYUVToggle::id))
        info.format = FourCC::FOURCC_I420;
    else
        GVA_WARNING("%s", VaapiPreprocYUVToggle::deprecation_message.c_str());

    VaApiImagePool::SizeParams size_params;
    // vdbox_sfc_pipe_part is checked below to be in range [0,1]
    size_params.num_fast = vdbox_sfc_pipe_part * pool_size;
    size_params.num_default = pool_size - size_params.num_fast;
    return std::unique_ptr<VaApiImagePool>(new VaApiImagePool(context, size_params, info));
}

VaApiImagePool::ImageInfo get_pool_image_info(const ImageInference::Ptr &inference) {
    size_t width = 0;
    size_t height = 0;
    size_t batch_size = 0;
    int format = 0;
    int memory_type = 0;
    inference->GetModelImageInputInfo(width, height, batch_size, format, memory_type);
    VaApiImagePool::ImageInfo info = {.width = safe_convert<uint32_t>(width),
                                      .height = safe_convert<uint32_t>(height),
                                      .batch = safe_convert<uint32_t>(batch_size),
                                      .format = static_cast<FourCC>(format),
                                      .memory_type = static_cast<MemoryType>(memory_type)};
    return info;
}

const InputImageLayerDesc::Ptr
getImagePreProcInfo(const std::map<std::string, InferenceBackend::InputLayerDesc::Ptr> &input_preprocessors) {
    const auto image_it = input_preprocessors.find("image");
    if (image_it != input_preprocessors.cend()) {
        const auto description = image_it->second;
        if (description) {
            return description->input_image_preroc_params;
        }
    }

    return nullptr;
}

} // namespace

ImageInferenceAsync::ImageInferenceAsync(const InferenceBackend::InferenceConfig &config,
                                         dlstreamer::ContextPtr vadpy_context, ImageInference::Ptr inference)
    : _inference(inference) {
    const auto &pre_process_config = config.at(KEY_PRE_PROCESSOR);
    if (!Utils::checkAllKeysAreKnown({KEY_VAAPI_THREAD_POOL_SIZE, KEY_VAAPI_FAST_SCALE_LOAD_FACTOR,
                                      KEY_NPU_DMABUF_ZERO_COPY},
                                     pre_process_config)) {
        throw std::invalid_argument("Unknown key in pre-processing configuration.");
    }

    auto thread_pool_size_it = pre_process_config.find(KEY_VAAPI_THREAD_POOL_SIZE);
    size_t thread_pool_size = thread_pool_size_it == pre_process_config.end()
                                  ? DEFAULT_THREAD_POOL_SIZE
                                  : std::stoull(thread_pool_size_it->second);

    _thread_pool.reset(new ThreadPool(thread_pool_size));

    auto vdbox_sfc_pipe_part_it = pre_process_config.find(KEY_VAAPI_FAST_SCALE_LOAD_FACTOR);
    float vdbox_sfc_pipe_part =
        vdbox_sfc_pipe_part_it == pre_process_config.end() ? 0 : std::stof(vdbox_sfc_pipe_part_it->second);
    if (vdbox_sfc_pipe_part < 0 || vdbox_sfc_pipe_part > 1)
        throw std::invalid_argument("VAAPI_FAST_SCALE_LOAD_FACTOR must be in range [0,1].");

    if (!_inference)
        throw std::invalid_argument("Invalid inference object.");

    GVA_INFO("VA-API pre-processing configuration:");
    GVA_INFO("-- VAAPI_FAST_SCALE_LOAD_FACTOR: %.2f", vdbox_sfc_pipe_part);
    GVA_INFO("-- VAAPI_THREAD_POOL_SIZE: %lu", thread_pool_size);

    _va_context = std::unique_ptr<VaApiContext>(new VaApiContext(vadpy_context));
    _va_converter = std::unique_ptr<VaApiConverter>(new VaApiConverter(_va_context.get()));

    auto inference_image_info = get_pool_image_info(_inference);
    size_t image_pool_size = safe_mul(safe_convert<size_t>(inference_image_info.batch), _inference->GetNireq());
    if (image_pool_size < thread_pool_size)
        image_pool_size = thread_pool_size;
    _va_image_pool =
        create_va_api_image_pool(inference_image_info, image_pool_size, _va_context.get(), vdbox_sfc_pipe_part);

    GVA_INFO("Vpp image pool size: %lu", image_pool_size);
}

void ImageInferenceAsync::SubmitInference(VaApiImage *va_api_image, IFrameBase::Ptr frame,
                                          const std::map<std::string, InputLayerDesc::Ptr> &input_preprocessors) {
    if (!va_api_image)
        throw std::invalid_argument("Invalid VaapiImage object");
    if (!frame)
        throw std::invalid_argument("Invalid frame object");

    auto deleter = [this, va_api_image](Image *img) {
        delete img;
        try {
            va_api_image->Unmap();
            this->_va_image_pool->ReleaseBuffer(va_api_image);
        } catch (const std::exception &e) {
            GVA_ERROR("Couldn't release VaApiImage: %s", e.what());
        }
    };

    auto mapped = va_api_image->Map();
    // For DMA-BUF zero-copy: propagate dma_fd so inference can import it as NPU remote tensor
    if (va_api_image->dma_buf_fd >= 0) {
        mapped.dma_fd = va_api_image->dma_buf_fd;
        mapped.type = MemoryType::DMA_BUFFER;
    }
    frame->SetImage(std::shared_ptr<Image>(new Image(mapped), deleter));
    _inference->SubmitImage(std::move(frame), input_preprocessors);
}

void ImageInferenceAsync::SubmitImage(IFrameBase::Ptr frame,
                                      const std::map<std::string, InputLayerDesc::Ptr> &input_preprocessors) {
    assert(frame && "Expected valid IFrameBase pointer");

    VaApiImage *dst_image = _va_image_pool->AcquireBuffer();

    try {
        _va_converter->Convert(
            *frame->GetImage(), *dst_image,
            getImagePreProcInfo(input_preprocessors), // contain operations order for Custom Image PreProcessing
            frame->GetImageTransformationParams());
    } catch (std::exception &) {
        _va_image_pool->ReleaseBuffer(dst_image);
        std::throw_with_nested(std::runtime_error("Unable to convert image using VA-API"));
    }

    dst_image->sync = _thread_pool->schedule([this, dst_image, f = std::move(frame), input_preprocessors]() {
        SubmitInference(dst_image, std::move(f), input_preprocessors);
    });
}

const std::string &ImageInferenceAsync::GetModelName() const {
    return _inference->GetModelName();
}

size_t ImageInferenceAsync::GetBatchSize() const {
    return _inference->GetBatchSize();
}

size_t ImageInferenceAsync::GetNireq() const {
    return _inference->GetNireq();
}

void ImageInferenceAsync::GetModelImageInputInfo(size_t &width, size_t &height, size_t &batch_size, int &format,
                                                 int &memory_type) const {
    _inference->GetModelImageInputInfo(width, height, batch_size, format, memory_type);
}

std::map<std::string, std::vector<size_t>> ImageInferenceAsync::GetModelInputsInfo() const {
    if (not _inference) {
        throw std::runtime_error("Inference not set");
    }

    return _inference->GetModelInputsInfo();
}

std::map<std::string, std::vector<size_t>> ImageInferenceAsync::GetModelOutputsInfo() const {
    if (not _inference) {
        throw std::runtime_error("Inference not set");
    }

    return _inference->GetModelOutputsInfo();
}

std::map<std::string, GstStructure *> ImageInferenceAsync::GetModelInfoPostproc() const {
    if (not _inference) {
        throw std::runtime_error("Inference not set");
    }

    return _inference->GetModelInfoPostproc();
}

bool ImageInferenceAsync::IsQueueFull() {
    return _inference->IsQueueFull();
}

void ImageInferenceAsync::Flush() {
    if (_va_image_pool) {
        _va_image_pool->Flush();
    }
    if (_inference) {
        _inference->Flush();
    }
}

void ImageInferenceAsync::Close() {
    _inference->Close();
}

ImageInferenceAsync::~ImageInferenceAsync() = default;
