/*******************************************************************************
 * Copyright (C) 2024-2025 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#include "yolo_v7.h"

#include "inference_backend/image_inference.h"
#include "inference_backend/logger.h"
#include "safe_arithmetic.hpp"

#include <gst/gst.h>

#include <map>
#include <memory>
#include <string>
#include <vector>

using namespace post_processing;

void YOLOv7Converter::parseOutputBlob(const float *data, const std::vector<size_t> &dims,
                                      std::vector<DetectedObject> &objects) const {

    size_t dims_size = dims.size();
    size_t input_width = getModelInputImageInfo().width;
    size_t input_height = getModelInputImageInfo().height;

    if (dims_size < BlobToROIConverter::min_dims_size)
        throw std::invalid_argument("Output blob dimensions size " + std::to_string(dims_size) +
                                    " is not supported (less than " +
                                    std::to_string(BlobToROIConverter::min_dims_size) + ").");

    size_t object_size = dims[dims_size - 1];
    const int NUM_CLASSES = object_size - YOLOV7_OFFSET_CS;

    size_t max_proposal_count = dims[dims_size - 2];

    for (size_t box_index = 0; box_index < max_proposal_count; ++box_index) {

        const float *output_data = &data[box_index * object_size];

        float x = output_data[YOLOV7_OFFSET_X];
        float y = output_data[YOLOV7_OFFSET_Y];
        float w = output_data[YOLOV7_OFFSET_W];
        float h = output_data[YOLOV7_OFFSET_H];
        float confidence = output_data[YOLOV7_OFFSET_BS];

        // early exit if entire box has low detection confidence
        if (confidence < confidence_threshold) {
            continue;
        }

        // find main class with highest probability
        size_t main_class = 0;
        for (size_t class_index = 0; class_index < (size_t)NUM_CLASSES; ++class_index) {
            if (output_data[YOLOV7_OFFSET_CS + class_index] > output_data[YOLOV7_OFFSET_CS + main_class]) {
                main_class = class_index;
            }
        }

        // update with main class confidence
        confidence *= output_data[YOLOV7_OFFSET_CS + main_class];
        if (confidence < confidence_threshold) {
            continue;
        }

        objects.push_back(DetectedObject(x, y, w, h, 0, confidence, main_class,
                                         BlobToMetaConverter::getLabelByLabelId(main_class), 1.0f / input_width,
                                         1.0f / input_height, true));
    }
}

TensorsTable YOLOv7Converter::convert(const OutputBlobs &output_blobs) {
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
                                blob->GetDims(), objects);
            }
        }

        return storeObjects(objects_table);
    } catch (const std::exception &e) {
        std::throw_with_nested(std::runtime_error("Failed to do YoloV7 post-processing."));
    }
    return TensorsTable{};
}
