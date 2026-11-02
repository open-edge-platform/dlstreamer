/*******************************************************************************
 * Copyright (C) 2021-2025 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#pragma once

#include "yolo_base.h"

#include "inference_backend/image_inference.h"

#include <gst/gst.h>

#include <map>
#include <memory>
#include <numeric>
#include <string>
#include <vector>

namespace post_processing {

class YOLOv2Converter : public YOLOBaseConverter {
  protected:
    size_t getIndex(size_t index, size_t offset) const;
    size_t getIndex(size_t index, size_t k, size_t i, size_t j) const;
    std::vector<float> softmax(const float *arr, size_t size, size_t common_offset) const;

    void parseOutputBlob(const float *blob_data, const std::vector<size_t> &blob_dims, size_t blob_size,
                         std::vector<DetectedObject> &objects) const override;

  public:
    YOLOv2Converter(BlobToMetaConverter::Initializer initializer, double confidence_threshold, double iou_threshold,
                    const YOLOBaseConverter::Initializer &yolo_initializer)
        : YOLOBaseConverter(std::move(initializer), confidence_threshold, iou_threshold, yolo_initializer) {
    }

    static bool checkModelProcOutputs(std::pair<size_t, size_t> cells, size_t boxes, size_t classes,
                                      const ModelOutputsInfo &outputs_info, OutputDimsLayout layout,
                                      const ModelImageInputInfo &input_info) {
        if (outputs_info.size() != 1)
            throw std::runtime_error("Yolo v2 converter can process models with only one output.");

        const auto &blob_dims = outputs_info.cbegin()->second;

        if (layout != OutputDimsLayout::NO) {
            auto desc = LayoutDesc::fromLayout(layout);
            if (!desc)
                throw std::runtime_error("Unsupported output layout.");
            if (cells.first != blob_dims[desc.Cx]) {
                GST_ERROR("Mismatch between cells_number_x: %lu - and the actual of the bounding box: %lu.",
                          cells.first, blob_dims[desc.Cx]);
                return false;
            }
            if (cells.second != blob_dims[desc.Cy]) {
                GST_ERROR("Mismatch between cells_number_y: %lu - and the actual of the bounding box: %lu.",
                          cells.second, blob_dims[desc.Cy]);
                return false;
            }
        }

        size_t batch_size = input_info.batch_size;

        size_t blob_size = std::accumulate(blob_dims.cbegin(), blob_dims.cend(), 1lu, std::multiplies<size_t>());
        size_t required_blob_size = batch_size * cells.first * cells.second * boxes * (classes + 5);

        if (blob_size != required_blob_size) {
            GST_ERROR("Size of the resulting output blob (%lu) does not match the required (%lu).", blob_size,
                      required_blob_size);
            return false;
        }

        return true;
    }

    static std::string getName() {
        return "yolo_v2";
    }
    static std::string getDeprecatedName() {
        return "tensor_to_bbox_yolo_v2";
    }
};
} // namespace post_processing
