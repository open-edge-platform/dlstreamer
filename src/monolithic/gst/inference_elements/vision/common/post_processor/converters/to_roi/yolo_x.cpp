/*******************************************************************************
 * Copyright (C) 2024-2025 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#include "yolo_x.h"

#include "inference_backend/image_inference.h"
#include "inference_backend/logger.h"

#include <string>
#include <vector>

using namespace post_processing;

void YOLOxConverter::parseOutputBlob(const float *data, const std::vector<size_t> &dims,
                                     std::vector<DetectedObject> &objects) const {
    // yolo_x uses strides of 32, 16, and 8 for auto-generated grids
    const std::vector<int> strides = {8, 16, 32};
    size_t dims_size = dims.size();
    size_t input_width = getModelInputImageInfo().width;
    size_t input_height = getModelInputImageInfo().height;

    if (dims_size < BlobToROIConverter::min_dims_size)
        throw std::invalid_argument("YoloX output tensor dimensions size " + std::to_string(dims_size) +
                                    " is not supported (less than " +
                                    std::to_string(BlobToROIConverter::min_dims_size) + ").");

    size_t object_size = dims[dims_size - 1];
    if (object_size != (size_t)(NUM_CLASSES + OFFSET_CS))
        throw std::invalid_argument("YoloX object size dimension is set to " + std::to_string(object_size) + ", but " +
                                    std::to_string(NUM_CLASSES + OFFSET_CS) + " expected.");

    size_t num_boxes_expected = 0;
    for (auto stride : strides) {
        num_boxes_expected += (input_width / stride) * (input_height / stride);
    }
    if (num_boxes_expected != dims[dims_size - 2])
        throw std::invalid_argument("YoloX box dimension is set to " + std::to_string(dims[dims_size - 2]) + ", but " +
                                    std::to_string(num_boxes_expected) + " expected for this model.");

    // iterate over boxes within grids
    int box_index = 0;
    for (auto stride : strides) {
        const int NUM_GRID_H = input_height / stride;
        const int NUM_GRID_W = input_width / stride;

        for (int grid_h = 0; grid_h < NUM_GRID_H; grid_h++) {
            for (int grid_w = 0; grid_w < NUM_GRID_W; grid_w++) {

                const float *box_data = &data[box_index * object_size];
                box_index++;

                float x = (box_data[OFFSET_X] + grid_w) * stride;
                float y = (box_data[OFFSET_Y] + grid_h) * stride;
                float w = std::exp(box_data[OFFSET_W]) * stride;
                float h = std::exp(box_data[OFFSET_H]) * stride;
                float confidence = box_data[OFFSET_BS];

                // early exit if entire box has low detection confidence
                if (confidence < confidence_threshold)
                    continue;

                // find main class with highest probability
                size_t main_class = 0;
                for (size_t class_index = 0; class_index < (size_t)NUM_CLASSES; ++class_index) {
                    // float class_score = box_data[OFFSET_CS + class_index];
                    if (box_data[OFFSET_CS + class_index] > box_data[OFFSET_CS + main_class]) {
                        main_class = class_index;
                    }
                }

                // update with main class confidence
                confidence *= box_data[OFFSET_CS + main_class];
                if (confidence < confidence_threshold)
                    continue;

                // add box to detected objects list
                objects.push_back(DetectedObject(x, y, w, h, 0, confidence, main_class,
                                                 BlobToMetaConverter::getLabelByLabelId(main_class), 1.0f / input_width,
                                                 1.0f / input_height, true));
            } // height loop
        } // width  loop
    } // stride loop
}

TensorsTable YOLOxConverter::convert(const OutputBlobs &output_blobs) {
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

                // InferenceBackend::Blob::Precision precision = blob->GetPrecision();

                size_t unbatched_size = blob->GetSize() / batch_size;
                parseOutputBlob(reinterpret_cast<const float *>(blob->GetData()) + unbatched_size * batch_number,
                                blob->GetDims(), objects);
            }
        }

        return storeObjects(objects_table);
    } catch (const std::exception &e) {
        std::throw_with_nested(std::runtime_error("Failed to do YoloX post-processing."));
    }
    return TensorsTable{};
}