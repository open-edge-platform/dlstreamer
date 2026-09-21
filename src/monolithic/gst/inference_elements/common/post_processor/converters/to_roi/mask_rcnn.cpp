/*******************************************************************************
 * Copyright (C) 2024-2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#include "mask_rcnn.h"

#include "copy_blob_to_gststruct.h"
#include "inference_backend/image_inference.h"
#include "inference_backend/logger.h"
#include "safe_arithmetic.hpp"

#include <gst/gst.h>

#include <map>
#include <memory>
#include <string>
#include <vector>

using namespace post_processing;

TensorsTable MaskRCNNConverter::convert(const OutputBlobs &output_blobs) {
    ITT_TASK(__FUNCTION__);
    try {
        const auto &model_input_image_info = getModelInputImageInfo();
        size_t batch_size = model_input_image_info.batch_size;

        size_t input_width = getModelInputImageInfo().width;
        size_t input_height = getModelInputImageInfo().height;

        DetectedObjectsTable objects_table(batch_size);

        bool three_output_tensors = false;
        std::string BOXES_KEY = TWO_TENSORS_BOXES_KEY;
        std::string MASKS_KEY = TWO_TENSORS_MASKS_KEY;
        std::string LABELS_KEY = "";
        if (output_blobs.size() == 3) {
            three_output_tensors = true;

            BOXES_KEY = THREE_TENSORS_BOXES_KEY;
            LABELS_KEY = THREE_TENSORS_LABELS_KEY;
            MASKS_KEY = THREE_TENSORS_MASKS_KEY;
        }

        for (size_t batch_number = 0; batch_number < batch_size; ++batch_number) {
            auto &objects = objects_table[batch_number];

            const InferenceBackend::OutputBlob::Ptr &boxes_blob = output_blobs.at(BOXES_KEY);
            if (not boxes_blob)
                throw std::invalid_argument("Boxes output blob is nullptr.");

            const std::vector<size_t> &dims = boxes_blob->GetDims();
            size_t dims_size = dims.size();

            if (dims_size < BlobToROIConverter::min_dims_size)
                throw std::invalid_argument("Boxes output blob dimensions size " + std::to_string(dims_size) +
                                            " is not supported (less than " +
                                            std::to_string(BlobToROIConverter::min_dims_size) + ").");

            size_t unbatched_size = boxes_blob->GetSize() / batch_size;
            const float *boxes_data =
                reinterpret_cast<const float *>(boxes_blob->GetData()) + unbatched_size * batch_number;

            size_t object_size = dims[dims_size - 1];
            size_t max_proposal_count = dims[dims_size - 2];

            const InferenceBackend::OutputBlob::Ptr &masks_blob = output_blobs.at(MASKS_KEY);
            if (not masks_blob)
                throw std::invalid_argument("Masks output blob is nullptr.");

            const std::vector<size_t> &masks_dims = masks_blob->GetDims();
            dims_size = masks_dims.size();

            if (dims_size < BlobToROIConverter::min_dims_size)
                throw std::invalid_argument("Masks output blob dimensions size " + std::to_string(dims_size) +
                                            " is not supported (less than " +
                                            std::to_string(BlobToROIConverter::min_dims_size) + ").");

            unbatched_size = masks_blob->GetSize() / batch_size;
            const float *masks_data =
                reinterpret_cast<const float *>(masks_blob->GetData()) + unbatched_size * batch_number;

            size_t masks_width = masks_dims[dims_size - 1];
            size_t masks_height = masks_dims[dims_size - 2];
            size_t number_of_classes = three_output_tensors ? 1 : masks_dims[dims_size - 3];
            size_t box_stride = masks_width * masks_height * number_of_classes;

            const int64_t *labels_data = nullptr;
            if (three_output_tensors) {
                const InferenceBackend::OutputBlob::Ptr &labels_blob = output_blobs.at(LABELS_KEY);
                if (not labels_blob)
                    throw std::invalid_argument("Labels output blob is nullptr.");

                if (labels_blob->GetDims().size() < BlobToROIConverter::min_dims_size)
                    throw std::invalid_argument(
                        "Labels output blob dimensions size " + std::to_string(labels_blob->GetDims().size()) +
                        " is not supported (less than " + std::to_string(BlobToROIConverter::min_dims_size) + ").");

                unbatched_size = labels_blob->GetSize() / batch_size;
                labels_data = reinterpret_cast<const int64_t *>(labels_blob->GetData()) + unbatched_size * batch_number;
            }

            for (size_t box_index = 0; box_index < max_proposal_count; ++box_index) {

                const float *output_data = &boxes_data[box_index * object_size];

                float x, y, w, h, confidence;
                x = y = w = h = confidence = 0;
                int64_t main_class = 0;
                const float *mask = nullptr;

                if (three_output_tensors) {
                    x = output_data[THREE_TENSORS_OFFSET_X1];
                    y = output_data[THREE_TENSORS_OFFSET_Y1];
                    w = output_data[THREE_TENSORS_OFFSET_X2] - x;
                    h = output_data[THREE_TENSORS_OFFSET_Y2] - y;
                    confidence = output_data[THREE_TENSORS_OFFSET_BS];

                    main_class = labels_data[box_index] + 1;
                    mask = &masks_data[box_index * box_stride];
                } else {
                    x = output_data[TWO_TENSORS_OFFSET_X1] * input_width;
                    y = output_data[TWO_TENSORS_OFFSET_Y1] * input_height;
                    w = output_data[TWO_TENSORS_OFFSET_X2] * input_width - x;
                    h = output_data[TWO_TENSORS_OFFSET_Y2] * input_height - y;
                    confidence = output_data[TWO_TENSORS_OFFSET_BS];

                    main_class = output_data[TWO_TENSORS_OFFSET_CS];
                    mask = masks_data + box_stride * box_index + masks_width * masks_height * (main_class - 1);
                }

                // early exit if entire box has low detection confidence
                if (confidence < confidence_threshold) {
                    continue;
                }

                auto detected_object = DetectedObject(x, y, w, h, 0, confidence, main_class,
                                                      BlobToMetaConverter::getLabelByLabelId(main_class),
                                                      1.0f / input_width, 1.0f / input_height, false);

                // create segmentation mask tensor
                GstStructure *tensor = gst_structure_copy(getModelProcOutputInfo().get());
                gst_structure_set_name(tensor, "mask_rcnn");
                gst_structure_set(tensor, "precision", G_TYPE_INT, GVA_PRECISION_FP32, NULL);
                gst_structure_set(tensor, "format", G_TYPE_STRING, GVA::TENSOR_FORMAT_INSTANCE_SEGMENTATION, NULL);
                gst_structure_set(tensor, "type", G_TYPE_STRING, GVA::GST_ANALYTICS_SEGMENTATION_2_TENSOR, NULL);

                GValueArray *data = g_value_array_new(2);
                GValue gvalue = G_VALUE_INIT;
                g_value_init(&gvalue, G_TYPE_UINT);
                g_value_set_uint(&gvalue, safe_convert<uint32_t>(masks_height));
                g_value_array_append(data, &gvalue);
                g_value_set_uint(&gvalue, safe_convert<uint32_t>(masks_width));
                g_value_array_append(data, &gvalue);
                gst_structure_set_array(tensor, "dims", data);
                g_value_array_free(data);

                copy_buffer_to_structure(tensor, reinterpret_cast<const void *>(mask),
                                         masks_height * masks_width * sizeof(float));
                detected_object.tensors.push_back(tensor);

                objects.push_back(detected_object);
            }
        }

        return storeObjects(objects_table);
    } catch (const std::exception &e) {
        std::throw_with_nested(std::runtime_error("Failed to do Mask-RCNN post-processing."));
    }
    return TensorsTable{};
}
