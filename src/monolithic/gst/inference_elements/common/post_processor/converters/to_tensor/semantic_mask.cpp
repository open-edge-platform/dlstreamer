/*******************************************************************************
 * Copyright (C) 2024-2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#include "semantic_mask.h"

#include "copy_blob_to_gststruct.h"
#include "inference_backend/image_inference.h"

#include <vector>

using namespace post_processing;

TensorsTable SemanticMaskConverter::convert(const OutputBlobs &output_blobs) {
    ITT_TASK(__FUNCTION__);
    TensorsTable tensors_table;
    try {
        const size_t batch_size = getModelInputImageInfo().batch_size;
        tensors_table.resize(batch_size);

        for (const auto &blob_iter : output_blobs) {
            InferenceBackend::OutputBlob::Ptr blob = blob_iter.second;
            if (not blob) {
                throw std::invalid_argument("Output blob is empty");
            }

            const std::string &layer_name = blob_iter.first;

            for (size_t frame_index = 0; frame_index < batch_size; ++frame_index) {
                GstStructure *tensor_data = BlobToTensorConverter::createTensor().gst_structure();

                CopyOutputBlobToGstStructure(blob, tensor_data, BlobToMetaConverter::getModelName().c_str(),
                                             layer_name.c_str(), batch_size, frame_index);

                gst_structure_set(tensor_data, "tensor_id", G_TYPE_INT, safe_convert<int>(frame_index), NULL);
                gst_structure_set(tensor_data, "format", G_TYPE_STRING, format.c_str(), NULL);
                gst_structure_set(tensor_data, "type", G_TYPE_STRING, GVA::GST_ANALYTICS_SEGMENTATION_2_TENSOR, NULL);

                std::vector<GstStructure *> tensors{tensor_data};
                tensors_table[frame_index].push_back(tensors);
            }
        }
    } catch (const std::exception &e) {
        std::throw_with_nested(std::runtime_error("Failed to do \"SemanticMaskConverter\" post-processing"));
    }

    return tensors_table;
}
