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

class YOLOv3Converter : public YOLOBaseConverter {
  public:
    using MaskType = std::map<size_t, std::vector<size_t>>;

  protected:
    const MaskType masks;
    const size_t coords = 4;

    size_t entryIndex(size_t side, size_t location, size_t entry) const;
    std::vector<float> softmax(const float *arr, size_t arr_size, size_t size, size_t common_offset, size_t side) const;

    void parseOutputBlob(const float *blob_data, const std::vector<size_t> &blob_dims, size_t blob_size,
                         std::vector<DetectedObject> &objects) const override;

    virtual YOLOv3Converter::DetectedObject calculateBoundingBox(size_t col, size_t row, float raw_x, float raw_y,
                                                                 float raw_w, float raw_h, size_t side_w, size_t side_h,
                                                                 float input_width, float input_height, size_t mask_0,
                                                                 size_t bbox_cell_num, float confidence,
                                                                 float bbox_class_first) const;

  public:
    YOLOv3Converter(BlobToMetaConverter::Initializer initializer, double confidence_threshold, double iou_threshold,
                    const YOLOBaseConverter::Initializer &yolo_initializer, const MaskType &masks)
        : YOLOBaseConverter(std::move(initializer), confidence_threshold, iou_threshold, yolo_initializer),
          masks(masks) {
    }

    static MaskType getMask(GstStructure *s, size_t bbox_number_on_cell, size_t cells_number, size_t layers_num) {
        if (!gst_structure_has_field(s, "masks"))
            throw std::runtime_error("model proc does not have \"masks\" parameter.");

        GValueArray *arr = nullptr;
        gst_structure_get_array(s, "masks", &arr);

        if (!arr)
            throw std::runtime_error("\"masks\" array is null.");

        std::vector<size_t> masks;
        try {
            if (arr->n_values != layers_num * bbox_number_on_cell)
                throw std::runtime_error("Mask size must be equal layers_number * bbox_number_on_cell.");

            masks.reserve(arr->n_values);
            for (guint i = 0; i < arr->n_values; ++i)
                masks.push_back(g_value_get_int(g_value_array_get_nth(arr, i)));

        } catch (const std::exception &e) {
            g_value_array_free(arr);
            throw e;
        }
        g_value_array_free(arr);

        MaskType mask;

        std::vector<size_t> one_side_mask;
        one_side_mask.reserve(bbox_number_on_cell);
        for (size_t i = 0; i < masks.size(); ++i) {
            if (i != 0 && i % bbox_number_on_cell == 0) {
                mask.insert({cells_number, one_side_mask});
                one_side_mask.clear();

                cells_number *= 2;
            }
            one_side_mask.push_back(masks[i]);
        }

        mask.insert({cells_number, one_side_mask});
        return mask;
    }

    static bool checkModelProcOutputs(std::pair<size_t, size_t> cells, size_t boxes, size_t classes,
                                      const MaskType &masks, const ModelOutputsInfo &outputs_info,
                                      OutputDimsLayout layout, const ModelImageInputInfo &input_info) {
        auto min_size_dims = outputs_info.cbegin()->second;
        size_t min_blob_size = std::numeric_limits<size_t>::max();

        auto desc = LayoutDesc::fromLayout(layout);
        if (!desc)
            throw std::runtime_error("Unsupported output layout.");
        size_t cells_x_i = desc.Cx;
        size_t cells_y_i = desc.Cy;

        for (const auto &output_info_pair : outputs_info) {
            const auto &blob_dims = output_info_pair.second;

            if (cells_x_i && cells_y_i) {
                size_t min_side = std::min(blob_dims[cells_x_i], blob_dims[cells_y_i]);
                const auto it = masks.find(min_side);
                if (it == masks.cend()) {
                    GST_ERROR("Mismatch between the size of the bounding box in the mask: %lu - and the actual of the "
                              "bounding box: %lu.",
                              masks.cbegin()->first, min_side);
                    return false;
                }
            }

            size_t blob_size = std::accumulate(blob_dims.cbegin(), blob_dims.cend(), 1lu, std::multiplies<size_t>());
            min_blob_size = std::min(min_blob_size, blob_size);

            if (blob_size == min_blob_size)
                min_size_dims = blob_dims;
        }

        if (cells_x_i && cells_y_i) {
            if (cells.first != min_size_dims[cells_x_i]) {
                GST_ERROR("Mismatch between cells_number_x: %lu - and the actual of the bounding box: %lu.",
                          cells.first, min_size_dims[cells_x_i]);
                return false;
            }
            if (cells.second != min_size_dims[cells_y_i]) {
                GST_ERROR("Mismatch between cells_number_y: %lu - and the actual of the bounding box: %lu.",
                          cells.second, min_size_dims[cells_y_i]);
                return false;
            }
        }

        size_t batch_size = input_info.batch_size;

        size_t required_blob_size = batch_size * cells.first * cells.second * boxes * (classes + 5);
        if (min_blob_size != required_blob_size) {
            GST_ERROR("Size of the resulting output blob (%lu) does not match the required (%lu).", min_blob_size,
                      required_blob_size);
            return false;
        }

        return true;
    }

    static std::string getName() {
        return "yolo_v3";
    }
    static std::string getDeprecatedName() {
        return "tensor_to_bbox_yolo_v3";
    }
};
} // namespace post_processing
