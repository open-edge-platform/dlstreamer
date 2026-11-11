/*******************************************************************************
 * Copyright (C) 2021-2025 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#pragma once

#include "blob_to_roi_converter.h"

#include "inference_backend/image_inference.h"

#include <gst/gst.h>

#include <cmath>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace post_processing {

class YOLOBaseConverter : public BlobToROIConverter {
  public:
    struct OutputLayerShapeConfig {
        const size_t classes_number;
        const size_t cells_number_x;
        const size_t cells_number_y;
        const size_t bbox_number_on_cell;

        const size_t one_bbox_blob_size;
        const size_t common_cells_number;
        const size_t one_scale_bboxes_blob_size;
        const size_t required_blob_size;

        enum Index : size_t { X = 0, Y = 1, W = 2, H = 3, CONFIDENCE = 4, FIRST_CLASS_PROB = 5 };
        OutputLayerShapeConfig() = delete;
        OutputLayerShapeConfig(size_t classes_number, size_t cells_number_x, size_t cells_number_y,
                               size_t bbox_number_on_cell)
            : classes_number(classes_number), cells_number_x(cells_number_x), cells_number_y(cells_number_y),
              bbox_number_on_cell(bbox_number_on_cell),
              one_bbox_blob_size(classes_number + 5), // classes prob + x, y, w, h, confidence
              common_cells_number(cells_number_x * cells_number_y),
              one_scale_bboxes_blob_size(one_bbox_blob_size * common_cells_number),
              required_blob_size(one_scale_bboxes_blob_size * bbox_number_on_cell) {
        }
    };
    enum class OutputDimsLayout { NCxCyB, NBCxCy, CxCyB, BCxCy, NCyCxB, NBCyCx, CyCxB, BCyCx, NO };

    struct LayoutDesc {
        int N = -1;
        int B = -1;
        int Cx = -1;
        int Cy = -1;
        bool operator==(const LayoutDesc &desc) const;
        explicit operator bool() const;

        static LayoutDesc fromLayout(OutputDimsLayout layout);
    };

    struct Initializer {
        std::vector<float> anchors;
        OutputLayerShapeConfig output_shape_info;
        bool do_cls_softmax;
        bool output_sigmoid_activation;

        OutputDimsLayout output_dims_layout;
    };

  protected:
    static constexpr size_t default_downsample_degree = 32;

    const std::vector<float> anchors;
    const OutputLayerShapeConfig output_shape_info;
    const bool do_cls_softmax;
    const bool output_sigmoid_activation;

    const OutputDimsLayout output_dims_layout;

    inline float sigmoid(float x) const {
        return 1 / (1 + std::exp(-x));
    }

    static OutputDimsLayout getLayoutFromDims(const ModelOutputsInfo &outputs_info, const std::vector<float> &anchors,
                                              size_t classes, std::pair<size_t, size_t> cells_number);
    static size_t tryAutomaticConfigWithDims(const std::vector<size_t> &dims, OutputDimsLayout layout, size_t boxes,
                                             size_t classes, std::pair<size_t, size_t> &cells);
    static std::pair<std::vector<size_t>, size_t> getMinBlobDims(const ModelOutputsInfo &outputs_info);

    virtual void parseOutputBlob(const float *blob_data, const std::vector<size_t> &blob_dims, size_t blob_size,
                                 std::vector<DetectedObject> &objects) const = 0;

  public:
    YOLOBaseConverter(BlobToMetaConverter::Initializer initializer, double confidence_threshold, double iou_threshold,
                      const YOLOBaseConverter::Initializer &yolo_initializer)
        : BlobToROIConverter(std::move(initializer), confidence_threshold, true, iou_threshold),
          anchors(yolo_initializer.anchors), output_shape_info(yolo_initializer.output_shape_info),
          do_cls_softmax(yolo_initializer.do_cls_softmax),
          output_sigmoid_activation(yolo_initializer.output_sigmoid_activation),
          output_dims_layout(yolo_initializer.output_dims_layout) {
    }
    virtual ~YOLOBaseConverter() = default;

    TensorsTable convert(const OutputBlobs &output_blobs);

    static bool tryAutomaticConfig(const ModelImageInputInfo &input_info, const ModelOutputsInfo &outputs_info,
                                   OutputDimsLayout dims_layout, size_t classes, const std::vector<float> &anchors,
                                   std::pair<size_t, size_t> &cells, size_t &boxes);
    static BlobToMetaConverter::Ptr create(BlobToMetaConverter::Initializer initializer,
                                           const std::string &converter_name, double confidence_thresholds);
};
} // namespace post_processing
