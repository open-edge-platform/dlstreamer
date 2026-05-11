/*******************************************************************************
 * Copyright (C) 2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#include "semantic_segmentation.h"

#include "inference_backend/image_inference.h"

#include <algorithm>
#include <cstdint>
#include <stdexcept>
#include <vector>

using namespace post_processing;

namespace {

using Blob = InferenceBackend::Blob;
using OutputBlob = InferenceBackend::OutputBlob;

std::vector<size_t> getUnbatchedDims(const std::vector<size_t> &dims, size_t batch_size) {
    if (dims.empty()) {
        throw std::invalid_argument("SemanticSegmentationConverter received a blob with empty dimensions");
    }
    if (batch_size == 0) {
        throw std::invalid_argument("SemanticSegmentationConverter expects a non-zero batch size");
    }
    if (dims.front() != batch_size) {
        throw std::invalid_argument(
            "SemanticSegmentationConverter expects the first output dimension to match batch size");
    }

    return std::vector<size_t>(dims.begin() + 1, dims.end());
}

std::vector<guint> normalizeMaskDims(const std::vector<size_t> &unbatched_dims) {
    if (unbatched_dims.size() == 2) {
        return {1u, static_cast<guint>(unbatched_dims[0]), static_cast<guint>(unbatched_dims[1])};
    }
    if (unbatched_dims.size() == 3 && unbatched_dims[0] == 1) {
        return {1u, static_cast<guint>(unbatched_dims[1]), static_cast<guint>(unbatched_dims[2])};
    }

    throw std::invalid_argument("SemanticSegmentationConverter expects [H,W], [1,H,W], or score maps shaped [C,H,W]");
}

template <typename T>
void copyMaskDataAsI64(GVA::Tensor &tensor, const T *data, size_t elements, const std::vector<size_t> &unbatched_dims) {
    if (!data) {
        throw std::invalid_argument("SemanticSegmentationConverter mask data is nullptr");
    }

    std::vector<int64_t> mask(elements);
    std::transform(data, data + elements, mask.begin(), [](T value) { return static_cast<int64_t>(value); });

    tensor.set_precision(GVA::Tensor::Precision::I64);
    tensor.set_layout(GVA::Tensor::Layout::ANY);
    tensor.set_dims(normalizeMaskDims(unbatched_dims));
    tensor.set_data(mask.data(), mask.size() * sizeof(int64_t));
}

void convertScoreMapToI64Mask(GVA::Tensor &tensor, const float *data, const std::vector<size_t> &unbatched_dims) {
    if (!data) {
        throw std::invalid_argument("SemanticSegmentationConverter score map data is nullptr");
    }
    if (unbatched_dims.size() != 3 || unbatched_dims[0] == 0) {
        throw std::invalid_argument("SemanticSegmentationConverter expects FP32 score maps shaped [C,H,W]");
    }

    const size_t channels = unbatched_dims[0];
    const size_t height = unbatched_dims[1];
    const size_t width = unbatched_dims[2];
    const size_t plane_size = height * width;

    std::vector<int64_t> mask(plane_size, 0);
    for (size_t pixel = 0; pixel < plane_size; ++pixel) {
        size_t best_channel = 0;
        float best_value = data[pixel];
        for (size_t channel = 1; channel < channels; ++channel) {
            const float candidate = data[channel * plane_size + pixel];
            if (candidate > best_value) {
                best_value = candidate;
                best_channel = channel;
            }
        }
        mask[pixel] = static_cast<int64_t>(best_channel);
    }

    tensor.set_precision(GVA::Tensor::Precision::I64);
    tensor.set_layout(GVA::Tensor::Layout::ANY);
    tensor.set_dims({1u, static_cast<guint>(height), static_cast<guint>(width)});
    tensor.set_data(mask.data(), mask.size() * sizeof(int64_t));
}

GstStructure *createSemanticMaskStructure(const OutputBlob::Ptr &blob, const std::string &layer_name, size_t batch_size,
                                          size_t frame_index, const std::string &model_name,
                                          const std::string &format) {
    const auto unbatched_dims = getUnbatchedDims(blob->GetDims(), batch_size);
    const size_t unbatched_size = blob->GetSize() / batch_size;

    GstStructure *tensor_data = gst_structure_new_empty(layer_name.c_str());
    GVA::Tensor tensor(tensor_data);
    tensor.set_name(layer_name);
    tensor.set_layer_name(layer_name);
    tensor.set_model_name(model_name);
    tensor.set_int("tensor_id", safe_convert<int>(frame_index));
    tensor.set_format(format);

    switch (blob->GetPrecision()) {
    case Blob::Precision::FP32: {
        const auto *typed_data = reinterpret_cast<const float *>(blob->GetData()) + frame_index * unbatched_size;
        convertScoreMapToI64Mask(tensor, typed_data, unbatched_dims);
        break;
    }
    case Blob::Precision::I64: {
        const auto *typed_data = reinterpret_cast<const int64_t *>(blob->GetData()) + frame_index * unbatched_size;
        copyMaskDataAsI64(tensor, typed_data, unbatched_size, unbatched_dims);
        break;
    }
    case Blob::Precision::I32: {
        const auto *typed_data = reinterpret_cast<const int32_t *>(blob->GetData()) + frame_index * unbatched_size;
        copyMaskDataAsI64(tensor, typed_data, unbatched_size, unbatched_dims);
        break;
    }
    case Blob::Precision::U8: {
        const auto *typed_data = reinterpret_cast<const uint8_t *>(blob->GetData()) + frame_index * unbatched_size;
        copyMaskDataAsI64(tensor, typed_data, unbatched_size, unbatched_dims);
        break;
    }
    default:
        throw std::runtime_error("SemanticSegmentationConverter supports FP32 score maps or integer class-index masks");
    }

    return tensor_data;
}

} // namespace

// Semantic segmentation models reach this converter in two forms:
// 1. An already interpreted class-index mask, typically [B,H,W] or [B,1,H,W], where each pixel stores the predicted
//    class id directly.
// 2. A per-class score map, typically FP32 [B,C,H,W], where each pixel stores C logits or scores and the predicted
//    class must be derived by taking argmax across the channel dimension.
//
// Regardless of the original representation, we normalize the metadata contract to a frame-level GVA tensor with:
// - format="semantic_segmentation"
// - precision=I64
// - dims=[1,H,W]
// - data containing one int64 class id per pixel
//
// This keeps downstream consumers independent from the model-specific output layout while still letting renderers
// distinguish semantic-segmentation results from legacy semantic-mask tensors when they need different visualization.

TensorsTable SemanticSegmentationConverter::convert(const OutputBlobs &output_blobs) {
    TensorsTable tensors_table;
    try {
        const size_t batch_size = getModelInputImageInfo().batch_size;
        tensors_table.resize(batch_size);

        for (const auto &output_layer : output_blobs) {
            const std::string &output_name = output_layer.first;
            const OutputBlob::Ptr &output_blob = output_layer.second;

            if (not output_blob) {
                throw std::invalid_argument("Output blob is empty");
            }
            if (output_blob->GetData() == nullptr) {
                throw std::invalid_argument("Output blob data is nullptr");
            }

            for (size_t batch_index = 0; batch_index < batch_size; ++batch_index) {
                GstStructure *tensor_data = createSemanticMaskStructure(
                    output_blob, output_name, batch_size, batch_index, BlobToMetaConverter::getModelName(), format);

                std::vector<GstStructure *> tensors{tensor_data};
                tensors_table[batch_index].push_back(tensors);
            }
        }
    } catch (const std::exception &e) {
        std::throw_with_nested(std::runtime_error("Failed to do \"SemanticSegmentationConverter\" post-processing"));
    }

    return tensors_table;
}