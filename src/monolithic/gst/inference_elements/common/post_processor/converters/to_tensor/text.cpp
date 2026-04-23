/*******************************************************************************
 * Copyright (C) 2021-2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#include "text.h"

#include "copy_blob_to_gststruct.h"
#include "inference_backend/image_inference.h"
#include "inference_backend/logger.h"
#include "safe_arithmetic.hpp"
#include "tensor.h"

#include <exception>
#include <gst/gst.h>
#include <iomanip>
#include <sstream>
#include <vector>

using namespace post_processing;
using namespace InferenceBackend;

TensorsTable TextConverter::convert(const OutputBlobs &output_blobs) {
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

            const float *data = reinterpret_cast<const float *>(blob->GetData());
            if (not data)
                throw std::invalid_argument("Output blob data is nullptr");

            const size_t data_size = blob->GetSize();

            const std::string layer_name = blob_iter.first;

            for (size_t frame_index = 0; frame_index < batch_size; ++frame_index) {
                GVA::Tensor classification_result = createTensor();

                if (!skipRawTensors())
                    CopyOutputBlobToGstStructure(blob, classification_result.gst_structure(),
                                                 BlobToMetaConverter::getModelName().c_str(), layer_name.c_str(),
                                                 batch_size, frame_index);

                const auto item = get_data_by_batch_index(data, data_size, batch_size, frame_index);
                const float *item_data = item.first;
                const size_t item_size = item.second;

                // Convert the data to a comma-separated string with fixed precision.
                std::stringstream stream;
                stream << std::fixed << std::setprecision(precision); // Set fixed precision for the stream.
                for (size_t i = 0; i < item_size; ++i) {
                    if (i)
                        stream << ", ";             // Add a comma separator between values.
                    stream << item_data[i] * scale; // Scale the data and add it to the stream.
                }
                classification_result.set_string(
                    "label", stream.str()); // Set the label in the tensor as the string representation.

                /* tensor_id - In different GStreamer versions tensors_batch are attached to the buffer in a different
                 * order. */
                /* type - To identify classification tensors among others. */
                /* element_id - To identify model_instance_id. */
                gst_structure_set(classification_result.gst_structure(), "tensor_id", G_TYPE_INT,
                                  safe_convert<int>(frame_index), "type", G_TYPE_STRING, "classification_result", NULL);
                std::vector<GstStructure *> tensors{classification_result.gst_structure()};
                tensors_table[frame_index].push_back(tensors);
            }
        }
    } catch (const std::exception &e) {
        GVA_ERROR("An error occurred in to-text converter: %s", e.what());
    }

    return tensors_table;
}
