/*******************************************************************************
 * Copyright (C) 2018-2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

// OpenVinoImageInference::SubmitImage and what it needs: taking a free infer request, putting the frame into its input
// tensor (software pre-processing, or bypass when the frame already fits the model), and starting the request once
// the batch is full. See Flush() in openvino_image_inference.cpp for how a partly filled batch is started.

#include "openvino_image_inference.h"

#include "openvino_blob_wrapper.h"

#include "inference_backend/logger.h"
#include "safe_arithmetic.hpp"

#include <cassert>
#include <stdexcept>

using namespace InferenceBackend;
using namespace InferenceBackend::OpenVino;

namespace {

const InputImageLayerDesc::Ptr
GetImagePreProcInfo(const std::map<std::string, InferenceBackend::InputLayerDesc::Ptr> &input_preprocessors) {
    const auto image_it = input_preprocessors.find("image");
    if (image_it != input_preprocessors.cend()) {
        const auto description = image_it->second;
        if (description) {
            return description->input_image_preroc_params;
        }
    }

    return nullptr;
}

Image FillImage(ov::Tensor &tensor, size_t bindex) {
    Image image = Image();
    const auto &dims = tensor.get_shape();
    if (dims.size() < 4)
        throw std::runtime_error("Unsupported layout: dims size less than 4");
    if (dims.size() < 2 || dims[1] > 4)
        throw std::runtime_error("Unsupported layout: NCHW is expected");
    image.width = safe_convert<uint32_t>(dims[3]);
    image.height = safe_convert<uint32_t>(dims[2]);
    if (bindex >= dims[0]) {
        throw std::out_of_range("Image index is out of range in batch blob");
    }
    auto elem_type = tensor.get_element_type();
    size_t plane_size = safe_mul(size_t(safe_mul(image.width, image.height)), elem_type.size());
    size_t buffer_offset = safe_mul(safe_mul(bindex, plane_size), dims[1]);

    image.planes[0] = static_cast<uint8_t *>(tensor.data()) + buffer_offset;
    image.planes[1] = image.planes[0] + plane_size;
    image.planes[2] = image.planes[1] + plane_size;
    image.planes[3] = nullptr;

    image.stride[0] = image.width;
    image.stride[1] = image.width;
    image.stride[2] = image.width;
    image.stride[3] = 0;
    return image;
}

Image MapOvTensorToImg(ov::Tensor &tensor, size_t batch_index) {
    ITT_TASK(__FUNCTION__);
    assert(tensor);

    Image image = FillImage(tensor, batch_index);
    switch (tensor.get_element_type()) {
    case ov::element::f32:
        image.format = FourCC::FOURCC_RGBP_F32;
        break;
    case ov::element::u8:
        image.format = FourCC::FOURCC_RGBP;
        break;
    default:
        throw std::runtime_error("Unsupported precision");
    }
    return image;
}

} // namespace

void OpenVinoImageInference::SubmitImageProcessing(const std::string &input_name, InferSlot *slot, const Image &src_img,
                                                   const InputImageLayerDesc::Ptr &pre_proc_info,
                                                   const ImageTransformationParams::Ptr image_transform_info) {
    ITT_TASK(__FUNCTION__);
    assert(slot);

    // FIXME: single input
    if (slot->in_tensors.front().empty()) {
        slot->in_tensors.front().push_back(slot->infer_request.get_tensor(input_name));
    }
    const size_t batch_index = slot->buffers.size();
    Image dst_img = MapOvTensorToImg(slot->in_tensors.front().front(), batch_index);
    if (src_img.planes[0] != dst_img.planes[0]) { // only convert if different buffers
        try {
            pre_processor_->Convert(src_img, dst_img, pre_proc_info, image_transform_info);
        } catch (const std::exception &e) {
            std::throw_with_nested(std::runtime_error("Failed while software frame preprocessing"));
        }
    }
}

void OpenVinoImageInference::BypassImageProcessing(const std::string &input_name, InferSlot *slot, const Image &src_img,
                                                   size_t batch_size) {
    ITT_TASK(__FUNCTION__);

    auto ov_tensor = img_to_tensor_->ImageToTensors(src_img);

    if (batch_size > 1) {
        if (ov_tensor.size() != slot->in_tensors.size())
            throw std::runtime_error("BypassImageProcessing - unexpected number of tensors!");
        size_t input_idx = 0;
        for (auto &t : ov_tensor) {
            slot->in_tensors[input_idx].emplace_back(std::move(t));
            input_idx++;
        }

        if (slot->in_tensors.front().size() == batch_size) {
            // FIXME: input name ?
            for (size_t i = 0; i < slot->in_tensors.size(); i++) {
                // FIXME: move?
                slot->infer_request.set_input_tensors(i, slot->in_tensors[i]);
            }
        }
    } else {
        if (ov_tensor.size() == 1) {
            slot->infer_request.set_tensor(input_name, ov_tensor.front());
        } else {
            // FIXME
            for (size_t i = 0; i < ov_tensor.size(); i++) {
                slot->infer_request.set_input_tensor(i, ov_tensor[i]);
            }
        }
    }
}

bool OpenVinoImageInference::DoNeedImagePreProcessing(const InferenceBackend::ImagePtr image) {
    if (pre_processor_.get() != nullptr)
        return true;

    if (image == nullptr)
        return false;

    // OV plugins exhibit low performance (serialization) when VA pre-processor generates non-contiguous tensors
    // Add OpenCV pre-processor to compress non-contiguous tensors on top of VA pre-processor which does
    // resize/crop/etc.
    if ((memory_type_ == MemoryType::SYSTEM) &&
        ((image->format == FourCC::FOURCC_RGBP) || (image->format == FourCC::FOURCC_BGRP))) {

        if (!isImageContiguous(*image)) {
            GVA_WARNING("Force OPENCV preprocessor to convert non-contiguous tensors into contiguous memory location");
            pre_processor_.reset(
                InferenceBackend::ImagePreprocessor::Create(InferenceBackend::ImagePreprocessorType::OPENCV, ""));
            return true;
        }
    }

    return false;
}

void OpenVinoImageInference::ApplyInputPreprocessors(
    InferSlot *slot, const std::map<std::string, InputLayerDesc::Ptr> &input_preprocessors) {
    ITT_TASK(__FUNCTION__);
    assert(slot && "Infer slot is null");

    const size_t batch_index = slot->buffers.size();

    for (const auto &preprocessor : input_preprocessors) {
        if (preprocessor.second == nullptr)
            continue;

        if (preprocessor.first == KEY_image) {
            if (!DoNeedImagePreProcessing(nullptr))
                continue;
        }

        const auto &model_inputs = prepared_model_.model->inputs();
        std::shared_ptr<OpenvinoInputTensor> input_tensor = nullptr;
        if ((preprocessor.first == KEY_image) && (model_inputs.size() == 1) && !slot->in_tensors.empty() &&
            !slot->in_tensors.front().empty()) {
            input_tensor = std::make_shared<OpenvinoInputTensor>(slot->in_tensors.front().front(), batch_index);
        } else if (model_inputs.size() == 1) {
            input_tensor = std::make_shared<OpenvinoInputTensor>(slot->infer_request.get_input_tensor());
        } else {
            input_tensor =
                std::make_shared<OpenvinoInputTensor>(slot->infer_request.get_tensor(preprocessor.second->name));
        }

        preprocessor.second->preprocessor(input_tensor);
    }
}

// Holds requests_mutex_ for the whole call, including the wait in Acquire() and the pre-processing. FreeSlot() and the
// completion callback never take it, so a waiting submitter cannot block them; Close() closes the pool first to wake it.
void OpenVinoImageInference::SubmitImage(
    IFrameBase::Ptr frame, const std::map<std::string, InferenceBackend::InputLayerDesc::Ptr> &input_preprocessors) {
    ITT_TASK(__FUNCTION__);

    if (!frame)
        throw std::invalid_argument("Invalid frame provided");

    std::unique_lock<std::mutex> lk(requests_mutex_);
    ++frames_in_flight_;
    InferSlot *slot = current_slot_;
    if (!slot) {
        auto acquired = free_slots_.Acquire();
        if (!acquired) {
            --frames_in_flight_;
            throw std::runtime_error("Inference is closed, cannot submit an image");
        }
        slot = *acquired;
        current_slot_ = slot;
    }

    // A partial batch already holds earlier frames: remember the sizes so that a failure rolls back only what this
    // frame added.
    const size_t prev_buffers = slot->buffers.size();
    std::vector<size_t> prev_tensors;
    for (const auto &in_vec : slot->in_tensors)
        prev_tensors.push_back(in_vec.size());

    try {
        if (DoNeedImagePreProcessing(frame->GetImage())) {
            SubmitImageProcessing(
                image_layer_, slot, *frame->GetImage(),
                GetImagePreProcInfo(input_preprocessors), // contain operations order for Custom Image PreProcessing
                frame->GetImageTransformationParams()     // during CIPP will be filling of crop and aspect-ratio
                                                          // parameters
            );
        } else {
            BypassImageProcessing(image_layer_, slot, *frame->GetImage(), safe_convert<size_t>(batch_size_));
        }

        ApplyInputPreprocessors(slot, input_preprocessors);

        slot->buffers.push_back(frame);
    } catch (const std::exception &e) {
        GVA_ERROR("Pre-processing has failed: %s", e.what());
        // The slot stays the current one, holding only the frames that were there before this one. Drop this frame
        // from the in-flight count, otherwise a later Flush() waits for it forever.
        slot->buffers.resize(prev_buffers);
        for (size_t i = 0; i < prev_tensors.size(); i++)
            slot->in_tensors[i].resize(prev_tensors[i]);
        --frames_in_flight_;
        request_processed_.notify_all();
        std::throw_with_nested(std::runtime_error("Pre-processing was failed."));
    }

    try {
        // start inference asynchronously if enough buffers for batching
        if (slot->buffers.size() >= safe_convert<size_t>(batch_size_)) {
            current_slot_ = nullptr;
            slot->StartAsync();
        }
    } catch (const std::exception &e) {
        // The slot holds the whole batch now: fail all of its frames, as Flush() and the completion callback do.
        // Not rethrown, so the caller does not report this frame twice.
        GVA_ERROR("Inference async start has failed: %s", e.what());
        // handle_error_ pushes the frames downstream, which can block: don't hold requests_mutex_ over it, as the
        // completion callback doesn't. The slot is ours until FreeSlot.
        lk.unlock();
        handle_error_(slot->buffers);
        FreeSlot(slot);
    }
}
