/*******************************************************************************
 * Copyright (C) 2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#pragma once

#include "blob_to_roi_converter.h"
#include "inference_backend/image_inference.h"

#include <array>
#include <string>
#include <vector>

namespace post_processing {

/*
MonoDETR monocular 3D detection post-processing ("mono3d").

The model has three inputs (image, calibs=KITTI P2 [N,3,4], img_sizes=original (W,H) [N,2]) and
five outputs. Because OpenVINO's runtime exposes outputs through a name-keyed map, the exported IR
MUST assign distinct, recognizable names to the outputs. The expected names (substring match) are:
    pred_logits : [B, Q, num_classes]      classification logits
    pred_boxes  : [B, Q, 6]                2D box in normalized cxcylrtb
    pred_3d_dim : [B, Q, 3]                3D size (h, w, l)
    pred_depth  : [B, Q, 2]                (depth, sigma_raw)
    pred_angle  : [B, Q, 24]               12 heading-bin logits + 12 residuals

Camera calibration (P2) and the original image size are supplied through the post-processing
parameters (injected from the gvadetect "intrinsics-file" property). If absent, sensible defaults
derived from the model input size are used.
*/
class Mono3DConverter : public BlobToROIConverter {
  public:
    Mono3DConverter(BlobToMetaConverter::Initializer initializer, double confidence_threshold)
        : BlobToROIConverter(std::move(initializer), confidence_threshold, false, 0.0) {
    }

    TensorsTable convert(const OutputBlobs &output_blobs) override;

    static std::string getName() {
        return "mono3d";
    }

  private:
    // P2 projection matrix (row-major 3x4) and original image size used for 3D back-projection.
    struct Calibration {
        std::array<double, 12> p2; // [fu 0 cu tx; 0 fv cv ty; 0 0 1 0] row-major
        double orig_width;
        double orig_height;
    };

    Calibration readCalibration() const;
    size_t readTopK() const;
    std::vector<float> readClassMeanSizes(size_t num_classes) const;

    void parseOutputs(const float *logits, const std::vector<size_t> &logits_dims, const float *boxes,
                      const float *dim3d, const float *depth, const float *angle, const Calibration &calib,
                      const std::vector<float> &cls_mean_size, std::vector<DetectedObject> &objects) const;
};

} // namespace post_processing
