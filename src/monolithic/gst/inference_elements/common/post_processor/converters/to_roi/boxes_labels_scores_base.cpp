/*******************************************************************************
 * Copyright (C) 2021-2025 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#include "boxes_labels_scores_base.h"

#include "inference_backend/image_inference.h"
#include "inference_backend/logger.h"
#include "safe_arithmetic.hpp"

#include <gst/gst.h>

#include <map>
#include <memory>
#include <string>
#include <vector>

using namespace post_processing;

const std::string BoxesLabelsScoresConverter::boxes_layer_name = "boxes";

bool BoxesLabelsScoresConverter::isValidModelBoxesOutput(
    const std::map<std::string, std::vector<size_t>> &model_outputs_info) {
    if (!model_outputs_info.count(boxes_layer_name))
        return false;

    const std::vector<size_t> &boxes_dims = model_outputs_info.at(boxes_layer_name);
    if (boxes_dims.back() != bbox_size_coordinates_confidence)
        return false;

    if (boxes_dims.size() < BlobToROIConverter::min_dims_size)
        return false;

    return true;
}

bool BoxesLabelsScoresConverter::isValidModelAdditionalOutput(
    const std::map<std::string, std::vector<size_t>> &model_outputs_info, const std::string &additional_layer_name) {

    if (!model_outputs_info.count(additional_layer_name))
        return false;
    const std::vector<size_t> &boxes_dims = model_outputs_info.at(boxes_layer_name);
    size_t max_proposal_count = boxes_dims[0];
    if (boxes_dims.size() == batched_model_dimentions_size) {
        max_proposal_count = boxes_dims[1];
    }

    const std::vector<size_t> &scores_dims = model_outputs_info.at(additional_layer_name);
    if (max_proposal_count != scores_dims[0])
        return false;

    return true;
}

std::tuple<float, float, float, float>
BoxesLabelsScoresConverter::getBboxCoordinates(const float *bbox_data, size_t width, size_t height) const {
    float bbox_x = bbox_data[0] / width;
    float bbox_y = bbox_data[1] / height;
    float bbox_w = bbox_data[2] / width - bbox_x;
    float bbox_h = bbox_data[3] / height - bbox_y;

    return std::make_tuple(bbox_x, bbox_y, bbox_w, bbox_h);
}

void BoxesLabelsScoresConverter::parseOutputBlob(const float *boxes_data, const std::vector<size_t> &boxes_dims,
                                                 const InferenceBackend::OutputBlob::Ptr &labels_scores_blob,
                                                 std::vector<DetectedObject> &objects,
                                                 const ModelImageInputInfo &model_input_image_info,
                                                 double roi_scale) const {
    if (!boxes_data)
        throw std::invalid_argument("Output blob data is nullptr.");

    size_t boxes_dims_size = boxes_dims.size();

    if (boxes_dims_size < BlobToROIConverter::min_dims_size)
        throw std::invalid_argument("Output blob dimentions size " + std::to_string(boxes_dims_size) +
                                    " is not supported (less than " +
                                    std::to_string(BlobToROIConverter::min_dims_size) + ").");

    size_t object_size = boxes_dims[boxes_dims_size - 1];
    if (object_size != BoxesLabelsScoresConverter::bbox_size_coordinates_confidence &&
        object_size != BoxesLabelsScoresConverter::bbox_size_coordinates)
        throw std::invalid_argument(
            "Object size dimension of output blob is set to " + std::to_string(object_size) + ", but only " +
            std::to_string(BoxesLabelsScoresConverter::bbox_size_coordinates) + " or " +
            std::to_string(BoxesLabelsScoresConverter::bbox_size_coordinates_confidence) + " are supported.");

    size_t max_proposal_count = boxes_dims[0];
    if (boxes_dims_size == batched_model_dimentions_size) {
        max_proposal_count = boxes_dims[1];
    }

    for (size_t i = 0; i < max_proposal_count; ++i) {

        const float *bbox_data = boxes_data + i * object_size;

        /* discard inference results that do not match 'confidence_threshold' */
        const float bbox_confidence = (object_size == BoxesLabelsScoresConverter::bbox_size_coordinates_confidence)
                                          ? bbox_data[BoxesLabelsScoresConverter::bbox_size_coordinates_confidence - 1]
                                          : 1.0f;

        const auto label_id_conf = getLabelIdConfidence(labels_scores_blob, i, bbox_confidence);
        const size_t label_id = label_id_conf.first;
        const float confidence = label_id_conf.second;

        if (confidence < confidence_threshold) {
            continue;
        }

        float bbox_x, bbox_y, bbox_w, bbox_h;
        std::tie(bbox_x, bbox_y, bbox_w, bbox_h) =
            getBboxCoordinates(bbox_data, model_input_image_info.width, model_input_image_info.height);

        // apply roi_scale if set
        if (roi_scale > 0 && roi_scale != 1) {
            bbox_x = bbox_x + bbox_w / 2 * (1 - roi_scale);
            bbox_y = bbox_y + bbox_h / 2 * (1 - roi_scale);
            bbox_w = bbox_w * roi_scale;
            bbox_h = bbox_h * roi_scale;
        }

        DetectedObject bbox(bbox_x, bbox_y, bbox_w, bbox_h, 0, confidence, label_id, getLabelByLabelId(label_id));
        objects.push_back(bbox);
    }
}

TensorsTable BoxesLabelsScoresConverter::convert(const OutputBlobs &output_blobs) {
    ITT_TASK(__FUNCTION__);
    try {
        const auto &model_input_image_info = getModelInputImageInfo();
        size_t batch_size = model_input_image_info.batch_size;
        DetectedObjectsTable objects_table(batch_size);
        const auto &detection_result = getModelProcOutputInfo();

        if (detection_result == nullptr)
            throw std::invalid_argument("detection_result tensor is nullptr");
        double roi_scale = 1.0;
        gst_structure_get_double(detection_result.get(), "roi_scale", &roi_scale);
        InferenceBackend::OutputBlob::Ptr boxes_blob = output_blobs.at(boxes_layer_name);
        InferenceBackend::OutputBlob::Ptr labels_scores_blob = getLabelsScoresBlob(output_blobs);
        for (size_t batch_number = 0; batch_number < batch_size; ++batch_number) {
            auto &objects = objects_table[batch_number];
            size_t unbatched_size = boxes_blob->GetSize() / batch_size;
            parseOutputBlob(reinterpret_cast<const float *>(boxes_blob->GetData()) + unbatched_size * batch_number,
                            boxes_blob->GetDims(), labels_scores_blob, objects, model_input_image_info, roi_scale);
        }

        return storeObjects(objects_table);
    } catch (const std::exception &e) {
        fprintf(stderr, "ATSS inner exception: %s\n", e.what());
        std::throw_with_nested(std::runtime_error("Failed to do ATSS post-processing."));
    }
    return TensorsTable{};
}
