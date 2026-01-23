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

    size_t dims_size = dims.size();
    size_t input_width = getModelInputImageInfo().width;
    size_t input_height = getModelInputImageInfo().height;

    if (dims_size < BlobToROIConverter::min_dims_size)
        throw std::invalid_argument("Output blob dimensions size " + std::to_string(dims_size) +
                                    " is not supported (less than " +
                                    std::to_string(BlobToROIConverter::min_dims_size) + ").");

    size_t object_size = dims[dims_size - 1];
    size_t max_proposal_count = dims[dims_size - 2];
    float *output_data = (float *)data;
    const size_t num_classes = BlobToMetaConverter::getLabels().size();

    if (num_classes == 0)
        throw std::invalid_argument("Num classes is zero.");

    for (size_t box_index = 0; box_index < max_proposal_count; ++box_index) {

        float box_score = output_data[YOLOV10_OFFSET_BS];
        float labelId = output_data[YOLOV10_OFFSET_L];
        size_t normLabelId = ((size_t)labelId) % num_classes;

        if (box_score > confidence_threshold) {

            float x1 = output_data[YOLOV10_OFFSET_X1];
            float y1 = output_data[YOLOV10_OFFSET_Y1];
            float x2 = output_data[YOLOV10_OFFSET_X2] - x1;
            float y2 = output_data[YOLOV10_OFFSET_Y2] - y1;
            float r = oob ? output_data[YOLOV10_OFFSET_L + 1] : 0;

            objects.push_back(DetectedObject(x1, y1, x2, y2, r, box_score, normLabelId,
                                             BlobToMetaConverter::getLabelByLabelId(normLabelId), 1.0f / input_width,
                                             1.0f / input_height, false));
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