/*******************************************************************************
 * Copyright (C) 2021-2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#pragma once

#include "blob_to_roi_converter.h"

#include <mutex>
#include <optional>
#include <string>

namespace post_processing {

/*
 * Detections split across separate output tensors:
 *   - "boxes"  [N,4|5] or [B,N,4|5]: x1,y1,x2,y2[,confidence] in model input pixels, or
 *   - "bboxes" [N,4|5] or [B,N,4|5]: the same, normalized to [0,1];
 *   - optional "labels" [N] or [B,N]: class id per box;
 *   - optional "scores" [N] or [B,N]: confidence per box (takes precedence over the 5th box value).
 */
class BoxesLabelsScoresConverter : public BlobToROIConverter {
  public:
    struct Layout {
        std::string boxes_name;
        bool normalized;
        size_t box_size;
        bool has_labels;
        bool has_scores;
    };

    BoxesLabelsScoresConverter(Initializer initializer, double confidence_threshold, double iou_threshold);

    TensorsTable convert(const OutputBlobs &output_blobs) override;
    TensorsTable convert(const OutputBlobs &output_blobs, FramesWrapper &frames) override;

    static std::optional<Layout> findLayout(const ModelOutputsInfo &outputs_info);

    static bool isValidModelOutputs(const ModelOutputsInfo &outputs_info) {
        return findLayout(outputs_info).has_value();
    }

    static std::string getName() {
        return "boxes_labels_scores";
    }

  private:
    BoxesLabelsScoresConverter(Layout layout, std::optional<bool> nms_execute, Initializer &&initializer,
                               double confidence_threshold, double iou_threshold);

    TensorsTable convertBlobs(const OutputBlobs &output_blobs, bool &only_non_finite);
    void warnNonFinite(GstElement *element);

    const Layout layout;
    std::once_flag non_finite_warning;
};

} // namespace post_processing
