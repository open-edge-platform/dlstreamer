/*******************************************************************************
 * Copyright (C) 2024-2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#include "yolo_v10.h"

#include "inference_backend/image_inference.h"
#include "inference_backend/logger.h"
#include "safe_arithmetic.hpp"

#include <gst/gst.h>

#include <map>
#include <memory>
#include <string>
#include <vector>

using namespace post_processing;

void YOLOv10Converter::parseOutputBlob(const float *data, const std::vector<size_t> &dims,
                                       std::vector<DetectedObject> &objects, bool oob) const {
    const size_t num_classes = BlobToMetaConverter::getLabels().size();

    if (num_classes == 0) [[unlikely]] {
        throw std::invalid_argument("Num classes is zero.");
    }

    size_t dims_size = dims.size();
    const float *__restrict output_data = data;

    const auto [input_width, input_height] =
        std::make_pair(getModelInputImageInfo().width, getModelInputImageInfo().height);

    const auto [object_size, max_proposal_count] = std::make_pair(dims[dims_size - 1], dims[dims_size - 2]);

    const float inv_width = 1.0f / static_cast<float>(input_width);
    const float inv_height = 1.0f / static_cast<float>(input_height);

    if (dims_size < BlobToROIConverter::min_dims_size)
        throw std::invalid_argument("Output blob dimensions size " + std::to_string(dims_size) +
                                    " is not supported (less than " +
                                    std::to_string(BlobToROIConverter::min_dims_size) + ").");

    for (size_t box_index = 0; box_index < max_proposal_count; ++box_index) {
        const float box_score = output_data[YOLOV10_OFFSET_BS];

        if (box_score > confidence_threshold) {
            const auto [X, Y, width, height, rotation, labelId] = std::make_tuple(
                output_data[YOLOV10_OFFSET_X1], output_data[YOLOV10_OFFSET_Y1],
                oob ? output_data[YOLOV10_OFFSET_X2] : output_data[YOLOV10_OFFSET_X2] - output_data[YOLOV10_OFFSET_X1],
                oob ? output_data[YOLOV10_OFFSET_Y2] : output_data[YOLOV10_OFFSET_Y2] - output_data[YOLOV10_OFFSET_Y1],
                oob ? output_data[YOLOV10_OFFSET_L + 1] : 0, output_data[YOLOV10_OFFSET_L]);

            size_t normLabelId = ((size_t)labelId) % num_classes;

            objects.emplace_back(X, Y, width, height, rotation, box_score, normLabelId,
                                 BlobToMetaConverter::getLabelByLabelId(normLabelId), inv_width, inv_height, oob);
        }

        output_data += object_size;
    }
}

TensorsTable YOLOv10Converter::convert(const OutputBlobs &output_blobs) {
    ITT_TASK(__FUNCTION__);
    try {
        const auto &model_input_image_info = getModelInputImageInfo();
        size_t batch_size = model_input_image_info.batch_size;

        DetectedObjectsTable objects_table(batch_size);

        for (size_t batch_number = 0; batch_number < batch_size; ++batch_number) {
            auto &objects = objects_table[batch_number];

            for (const auto &blob_iter : output_blobs) {
                const InferenceBackend::OutputBlob::Ptr &blob = blob_iter.second;
                if (not blob)
                    throw std::invalid_argument("Output blob is nullptr.");

                size_t unbatched_size = blob->GetSize() / batch_size;
                parseOutputBlob(reinterpret_cast<const float *>(blob->GetData()) + unbatched_size * batch_number,
                                blob->GetDims(), objects, false);
            }
        }

        return storeObjects(objects_table);
    } catch (const std::exception &e) {
        std::throw_with_nested(std::runtime_error("Failed to do YoloV10 post-processing."));
    }
    return TensorsTable{};
}
