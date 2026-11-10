/*******************************************************************************
 * Copyright (C) 2021-2024 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#include "yolo_v2.h"

#include "common/post_processor/blob_to_meta_converter.h"

#include "inference_backend/image_inference.h"
#include "inference_backend/logger.h"

#include <gst/gst.h>

#include <map>
#include <memory>
#include <string>
#include <vector>

using namespace post_processing;

size_t YOLOv2Converter::getIndex(size_t index, size_t offset) const {
    return index * output_shape_info.common_cells_number + offset;
}

std::vector<float> YOLOv2Converter::softmax(const float *arr, size_t size, size_t common_offset) const {
    std::vector<float> sftm_arr(size);
    float sum = 0;
    for (size_t i = 0; i < size; ++i) {
        sftm_arr[i] = std::exp(arr[getIndex((OutputLayerShapeConfig::Index::FIRST_CLASS_PROB + i), common_offset)]);
        sum += sftm_arr[i];
    }
    for (size_t i = 0; i < size; ++i) {
        sftm_arr[i] /= sum;
    }
    return sftm_arr;
}

void YOLOv2Converter::parseOutputBlob(const float *blob_data, const std::vector<size_t> &, size_t,
                                      std::vector<DetectedObject> &objects) const {

    if (not blob_data)
        throw std::invalid_argument("Output blob data is nullptr");

    for (size_t bbox_scale_index = 0; bbox_scale_index < output_shape_info.bbox_number_on_cell; ++bbox_scale_index) {
        const float anchor_scale_w = anchors[bbox_scale_index * 2];
        const float anchor_scale_h = anchors[bbox_scale_index * 2 + 1];

        for (size_t cell_index_x = 0; cell_index_x < output_shape_info.cells_number_x; ++cell_index_x) {
            for (size_t cell_index_y = 0; cell_index_y < output_shape_info.cells_number_y; ++cell_index_y) {

                const size_t common_offset = bbox_scale_index * output_shape_info.one_scale_bboxes_blob_size +
                                             cell_index_y * output_shape_info.cells_number_y + cell_index_x;

                using Index = YOLOv2Converter::OutputLayerShapeConfig::Index;

                float bbox_confidence = blob_data[getIndex(Index::CONFIDENCE, common_offset)];
                if (output_sigmoid_activation)
                    bbox_confidence = sigmoid(bbox_confidence);
                if (bbox_confidence <= confidence_threshold)
                    continue;

                std::pair<size_t, float> bbox_class = std::make_pair(0, 0.f);
                if (do_cls_softmax) {
                    const auto cls_confs = softmax(blob_data, output_shape_info.classes_number, common_offset);

                    for (size_t bbox_class_id = 0; bbox_class_id < output_shape_info.classes_number; ++bbox_class_id) {
                        const float bbox_class_prob = cls_confs[bbox_class_id];

                        if (bbox_class_prob > 1.f || bbox_class_prob < 0.f) {
                            GST_WARNING("bbox_class_prob %f.is out of range [0,1].", bbox_class_prob);
                        }
                        if (bbox_class_prob > bbox_class.second) {
                            bbox_class.first = bbox_class_id;
                            bbox_class.second = bbox_class_prob;
                        }
                    }
                } else {
                    for (size_t bbox_class_id = 0; bbox_class_id < output_shape_info.classes_number; ++bbox_class_id) {
                        const float bbox_class_prob =
                            blob_data[getIndex((Index::FIRST_CLASS_PROB + bbox_class_id), common_offset)];

                        if (bbox_class_prob > 1.f || bbox_class_prob < 0.f) {
                            GST_WARNING("bbox_class_prob %f.is out of range [0,1].", bbox_class_prob);
                        }
                        if (bbox_class_prob > bbox_class.second) {
                            bbox_class.first = bbox_class_id;
                            bbox_class.second = bbox_class_prob;
                        }
                    }
                }

                bbox_confidence *= bbox_class.second;
                if (bbox_confidence > 1.f || bbox_confidence < 0.f) {
                    GST_WARNING("bbox_confidence %f.is out of range [0,1].", bbox_confidence);
                }
                if (bbox_confidence <= confidence_threshold)
                    continue;

                const float raw_x = blob_data[getIndex(Index::X, common_offset)];
                const float raw_y = blob_data[getIndex(Index::Y, common_offset)];
                const float raw_w = blob_data[getIndex(Index::W, common_offset)];
                const float raw_h = blob_data[getIndex(Index::H, common_offset)];

                // scale back to image width/height
                const float bbox_x = (cell_index_x + (output_sigmoid_activation ? sigmoid(raw_x) : raw_x)) /
                                     output_shape_info.cells_number_x;
                const float bbox_y = (cell_index_y + (output_sigmoid_activation ? sigmoid(raw_y) : raw_y)) /
                                     output_shape_info.cells_number_y;
                const float bbox_w = (std::exp(raw_w) * anchor_scale_w) / output_shape_info.cells_number_x;
                const float bbox_h = (std::exp(raw_h) * anchor_scale_h) / output_shape_info.cells_number_y;

                DetectedObject bbox(bbox_x, bbox_y, bbox_w, bbox_h, 0, bbox_confidence, bbox_class.first,
                                    BlobToMetaConverter::getLabelByLabelId(bbox_class.first), 1.0, 1.0, true);
                objects.push_back(bbox);
            }
        }
    }
}
