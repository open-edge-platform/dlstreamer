/*******************************************************************************
 * Copyright (C) 2021-2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#include "raw_data_copy.h"
#include "copy_blob_to_gststruct.h"
#include "inference_backend/image_inference.h"
#include "inference_backend/logger.h"
#include "safe_arithmetic.hpp"

#include <exception>
#include <string>
#include <vector>

using namespace post_processing;
using namespace InferenceBackend;

TensorsTable RawDataCopyConverter::convert(const OutputBlobs &output_blobs) {
    ITT_TASK(__FUNCTION__);
    TensorsTable tensors_table;
    try {
        const size_t batch_size = getModelInputImageInfo().batch_size;
        tensors_table.resize(batch_size);

        for (const auto &blob_iter : output_blobs) {
            OutputBlob::Ptr blob = blob_iter.second;
            if (not blob) {
                throw std::invalid_argument("Output blob is empty");
            }

            const std::string &layer_name = blob_iter.first;

            for (size_t frame_index = 0; frame_index < batch_size; ++frame_index) {
                GstStructure *tensor_data = BlobToTensorConverter::createTensor().gst_structure();

                CopyOutputBlobToGstStructure(blob, tensor_data, BlobToMetaConverter::getModelName().c_str(),
                                             layer_name.c_str(), batch_size, frame_index);

                // Mark as a generic raw tensor so it is mirrored as a GstAnalyticsTensorMtd.
                gst_structure_set(tensor_data, "type", G_TYPE_STRING, GVA::GST_ANALYTICS_TENSOR_2_TENSOR, NULL);

                // In different versions of GStreamer, tensors_batch are attached to the buffer in a different order.
                // Thus, we identify our meta using tensor_id.
                gst_structure_set(tensor_data, "tensor_id", G_TYPE_INT, safe_convert<int>(frame_index), NULL);

                std::vector<GstStructure *> tensors{tensor_data};
                tensors_table[frame_index].push_back(tensors);
            }
        }
    } catch (const std::exception &e) {
        GVA_ERROR("An error occurred while processing output BLOBs: %s", e.what());
    }
    return tensors_table;
}
