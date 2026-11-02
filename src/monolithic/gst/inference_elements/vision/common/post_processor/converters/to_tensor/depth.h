/*******************************************************************************
 * Copyright (C) 2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#pragma once

#include "blob_to_tensor_converter.h"

#include "inference_backend/image_inference.h"
#include "inference_backend/logger.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <gst/gst.h>

#include <map>
#include <memory>
#include <string>
#include <vector>

namespace post_processing::depth_converter {

// Depth output interpretation stays owned by the converter module even when ROI output
// reuses the same reduction rules on ROI-selected pixels.
// Each ROI is summarized by center depth, whole-region mean/median/min/max/stddev, and the count and ratio of valid
// depth pixels.
struct Metrics {
    double center = -1.0;
    double mean = -1.0;
    double median = -1.0;
    double min = -1.0;
    double max = -1.0;
    double stddev = -1.0;
    uint32_t valid_count = 0;
    double valid_ratio = 0.0;
};

// Serialized ROI metrics tensor schema.
// format = "depth_metrics"
// layout = NC
// dims = {1, Count}
// payload order = Center, Mean, Median, Min, Max, Stddev, ValidCount, ValidRatio
enum class DepthMetricsField : size_t {
    Center = 0,
    Mean,
    Median,
    Min,
    Max,
    Stddev,
    ValidCount,
    ValidRatio,
    Count,
};

std::string formatLabel(double value);

bool isValidValue(float value);

Metrics buildMetrics(std::vector<double> valid_values, std::vector<double> center_values, size_t region_size);

std::array<float, static_cast<size_t>(DepthMetricsField::Count)> toTensorValues(const Metrics &metrics);

Metrics summarizeDepthMap(const float *depth_data, size_t element_count, const std::vector<size_t> &unbatched_dims);

} // namespace post_processing::depth_converter

namespace post_processing {

// Depth is different from ordinary tensor converters: it can still publish a
// frame-level raw depth tensor, but it also needs per-frame context to reinterpret
// the dense map as per-ROI label and metrics tensors.
class DepthConverter : public BlobToTensorConverter {
  public:
    DepthConverter(BlobToMetaConverter::Initializer initializer);

    TensorsTable convert(const OutputBlobs &output_blobs, FramesWrapper &frames) override;

    static std::string getName() {
        return "depth_estimation";
    }

  protected:
    TensorsTable convert([[maybe_unused]] const OutputBlobs &output_blobs) override;
};
} // namespace post_processing