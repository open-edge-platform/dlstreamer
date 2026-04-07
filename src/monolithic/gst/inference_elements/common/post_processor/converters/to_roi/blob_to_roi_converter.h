/*******************************************************************************
 * Copyright (C) 2021-2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#pragma once

#include "post_processor/blob_to_meta_converter.h"
#include "post_processor/post_proc_common.h"

#include <gst/gst.h>

#include <map>
#include <memory>
#include <string>

namespace post_processing {

const double DEFAULT_IOU_THRESHOLD = 0.4;

class BlobToROIConverter : public BlobToMetaConverter {
  protected:
    struct DetectedObject {
        double x;
        double y;
        double w;
        double h;
        double r;

        double confidence;

        size_t label_id;
        std::string label;

        std::vector<GstStructure *> tensors;

        DetectedObject(double x, double y, double w, double h, double r, double confidence, size_t label_id,
                       const std::string &label, double w_scale = 1.f, double h_scale = 1.f,
                       bool relative_to_center = false)
            : confidence(confidence), label_id(label_id), label(label) {
            if (relative_to_center) {
                this->x = (x - w / 2) * w_scale;
                this->y = (y - h / 2) * h_scale;
            } else {
                this->x = x * w_scale;
                this->y = y * h_scale;
            }

            this->w = w * w_scale;
            this->h = h * h_scale;

            this->r = r;
        }

        bool operator<(const DetectedObject &other) const {
            return this->confidence < other.confidence;
        }

        bool operator>(const DetectedObject &other) const {
            return this->confidence > other.confidence;
        }

        std::vector<GstStructure *> toTensor(const GstStructureUniquePtr &detection_result) const {
            GstStructure *detection_tensor = gst_structure_copy(detection_result.get());

            gst_structure_set_name(detection_tensor, "detection"); // make sure name="detection"
            gst_structure_set(detection_tensor, "label_id", G_TYPE_INT, label_id, "confidence", G_TYPE_DOUBLE,
                              confidence, "x_min", G_TYPE_DOUBLE, x, "x_max", G_TYPE_DOUBLE, x + w, "y_min",
                              G_TYPE_DOUBLE, y, "y_max", G_TYPE_DOUBLE, y + h, "rotation", G_TYPE_DOUBLE, r, NULL);

            if (not label.empty())
                gst_structure_set(detection_tensor, "label", G_TYPE_STRING, label.c_str(), NULL);

            std::vector<GstStructure *> results{detection_tensor};
            for (size_t i = 0; i < tensors.size(); i++)
                results.push_back(tensors[i]);

            return results;
        }

        bool isDetectionValid() const {
            const double x_min = this->x - this->w * 0.5;
            const double x_max = this->x + this->w * 0.5;
            const double y_min = this->y - this->h * 0.5;
            const double y_max = this->y + this->h * 0.5;

            const bool out_of_bounds = x_min > 1.0     // x_min beyond max image boundary
                                       || y_min > 1.0  // y_min beyond max image boundary
                                       || x_max < 0.0  // x_max beyond min image boundary
                                       || y_max < 0.0; // y_max beyond min image boundary

            const bool zero_area = this->w <= 0.0     // invalid width
                                   || this->h <= 0.0; // invalid height

            return !(out_of_bounds || zero_area);
        }

        void validateKeypoints() {
            for (auto &structure : this->tensors) {
                GVA::Tensor tensor(structure);

                if (tensor.format() != "keypoints") {
                    continue;
                };

                const auto keypoints = tensor.data<float>();
                auto confidences = tensor.get_vector<float>("confidence");

                const auto &dimensions = tensor.dims();
                size_t points_num = dimensions[0];
                size_t point_dimension = dimensions[1];

                for (size_t i = 0; i < points_num; ++i) {
                    float x_real = keypoints[point_dimension * i];
                    float y_real = keypoints[point_dimension * i + 1];

                    // Check whether the keypoint is within the image/ROI.
                    if (x_real > 1.0 || x_real < 0.0 || y_real > 1.0 || y_real < 0.0) {
                        confidences[i] = 0.0;
                    }
                }

                tensor.set_vector<float>("confidence", confidences);
            }
        }
    };
    using DetectedObjectsTable = std::vector<std::vector<DetectedObject>>;

    TensorsTable storeObjects(DetectedObjectsTable &objects) const;
    void runNms(std::vector<DetectedObject> &candidates) const;
    TensorsTable toTensorsTable(const DetectedObjectsTable &bboxes_table) const;

    const double confidence_threshold;
    const bool need_nms;
    const double iou_threshold;

  public:
    BlobToROIConverter() = delete;

    BlobToROIConverter(BlobToMetaConverter::Initializer initializer, double confidence_threshold, bool need_nms,
                       double iou_threshold)
        : BlobToMetaConverter(std::move(initializer)), confidence_threshold(confidence_threshold), need_nms(need_nms),
          iou_threshold(iou_threshold) {
    }

    TensorsTable convert(const OutputBlobs &output_blobs) = 0;

    static BlobToMetaConverter::Ptr create(BlobToMetaConverter::Initializer initializer,
                                           const std::string &converter_name, const std::string &custom_postproc_lib);
    static const size_t min_dims_size = 2;
};

} // namespace post_processing
