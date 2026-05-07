/*******************************************************************************
 * Copyright (C) 2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#include "depth.h"

#include "copy_blob_to_gststruct.h"
#include "gva_base_inference.h"
#include "gva_utils.h"
#include "inference_backend/image_inference.h"
#include "inference_backend/logger.h"
#include "safe_arithmetic.hpp"
#include "tensor.h"

#include <gst/video/gstvideometa.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <iomanip>
#include <limits>
#include <memory>
#include <sstream>
#include <string>
#include <type_traits>
#include <vector>

using namespace post_processing;
using namespace InferenceBackend;

namespace {

constexpr size_t CENTER_REGION_SIZE = 5;
constexpr double OWNERSHIP_EPSILON = 1e-6;
constexpr double MIN_DEPTH_SCALE = 1e-3;
constexpr double RELATIVE_DEPTH_SCALE = 0.05;
constexpr double COMPATIBLE_DEPTH_Z_SCORE = 2.5;
constexpr double FRONT_LAYER_Z_BAND = 1.0;
constexpr double SPATIAL_WEIGHT = 0.35;
constexpr double OCCLUDED_LAYER_PENALTY = 0.5;

// Region is expressed in depth-map coordinates after the original ROI has been
// mapped through the preprocessing geometry into model-output space.
struct Region {
    size_t x0 = 0;
    size_t y0 = 0;
    size_t x1 = 0;
    size_t y1 = 0;
};

// Candidate stores the precomputed data needed to decide which ROI should own a
// depth pixel when mapped ROI regions overlap in the depth map.
struct Candidate {
    gint roi_id = -1;
    Region region;
    double layer_depth = -1.0;
    double depth_scale = MIN_DEPTH_SCALE;
    double center_x = 0.0;
    double center_y = 0.0;
    double spatial_scale_sq = 1.0;
    size_t area = 0;
    std::vector<size_t> overlapping_candidates;
};

struct DepthROIResult {
    gint roi_id = -1;
    depth_converter::Metrics metrics;
};

struct OwnershipDecision {
    size_t candidate_index = 0;
    bool has_depth_model = false;
    bool compatible = false;
    double layer_depth = std::numeric_limits<double>::infinity();
    double depth_scale = MIN_DEPTH_SCALE;
    double depth_score = std::numeric_limits<double>::infinity();
    double spatial_score = std::numeric_limits<double>::infinity();
    double total_score = std::numeric_limits<double>::infinity();
};

template <typename T>
bool isValidDepthValue(T value) {
    if constexpr (std::is_floating_point_v<T>) {
        return std::isfinite(value) && value > 0;
    }

    return value > 0;
}

size_t getCenterIndex(const std::vector<size_t> &dims, size_t item_size) {
    if (item_size == 0) {
        return 0;
    }

    if (dims.size() >= 2) {
        const size_t height = dims[dims.size() - 2];
        const size_t width = dims[dims.size() - 1];
        const size_t spatial_size = height * width;
        if (spatial_size != 0 && item_size % spatial_size == 0) {
            return (height / 2) * width + (width / 2);
        }
    }

    return item_size / 2;
}

double calculateMedian(std::vector<double> values) {
    if (values.empty()) {
        return -1.0;
    }

    auto middle = values.begin() + values.size() / 2;
    std::nth_element(values.begin(), middle, values.end());
    double median = *middle;
    if (values.size() % 2 == 0) {
        auto lower_middle = std::max_element(values.begin(), middle);
        median = (*lower_middle + *middle) * 0.5;
    }

    return median;
}

std::string trim(const std::string &value) {
    const auto begin = value.find_first_not_of(" \t\n\r");
    if (begin == std::string::npos) {
        return {};
    }

    const auto end = value.find_last_not_of(" \t\n\r");
    return value.substr(begin, end - begin + 1);
}

std::vector<size_t> getUnbatchedDims(const std::vector<size_t> &dims, size_t batch_size) {
    if (dims.empty()) {
        throw std::invalid_argument("DepthConverter received a blob with empty dimensions");
    }
    if (batch_size == 0) {
        throw std::invalid_argument("DepthConverter expects a non-zero batch size");
    }
    if (dims.front() != batch_size) {
        throw std::invalid_argument("DepthConverter expects the first output dimension to match batch size");
    }

    return std::vector<size_t>(dims.begin() + 1, dims.end());
}

// Depth conversion always starts from the model output blob. Reduce the batched
// dimensions to the per-frame shape and then validate that the last two axes can
// be interpreted as a depth map.
bool getDepthMapShape(const std::vector<size_t> &unbatched_dims, size_t element_count, size_t &depth_width,
                      size_t &depth_height) {
    if (unbatched_dims.size() < 2) {
        return false;
    }

    depth_height = unbatched_dims[unbatched_dims.size() - 2];
    depth_width = unbatched_dims[unbatched_dims.size() - 1];
    return depth_width != 0 && depth_height != 0 && element_count >= depth_width * depth_height;
}

template <typename T>
double calculateCenterRegionMedian(const T *item_data, size_t item_size, const std::vector<size_t> &unbatched_dims) {
    if (!item_data || item_size == 0) {
        return -1.0;
    }

    if (unbatched_dims.size() >= 2) {
        const size_t height = unbatched_dims[unbatched_dims.size() - 2];
        const size_t width = unbatched_dims[unbatched_dims.size() - 1];
        const size_t spatial_size = height * width;
        if (spatial_size != 0 && item_size % spatial_size == 0) {
            const size_t region_height = std::min(height, CENTER_REGION_SIZE);
            const size_t region_width = std::min(width, CENTER_REGION_SIZE);
            size_t row_begin = height / 2;
            size_t col_begin = width / 2;

            row_begin = row_begin >= region_height / 2 ? row_begin - region_height / 2 : 0;
            col_begin = col_begin >= region_width / 2 ? col_begin - region_width / 2 : 0;

            if (row_begin + region_height > height) {
                row_begin = height - region_height;
            }
            if (col_begin + region_width > width) {
                col_begin = width - region_width;
            }

            std::vector<double> center_values;
            center_values.reserve(region_height * region_width);
            for (size_t row = row_begin; row < row_begin + region_height; ++row) {
                for (size_t col = col_begin; col < col_begin + region_width; ++col) {
                    const T value = item_data[row * width + col];
                    if (isValidDepthValue(value)) {
                        center_values.push_back(static_cast<double>(value));
                    }
                }
            }

            return calculateMedian(std::move(center_values));
        }
    }

    const size_t center_index = std::min(getCenterIndex(unbatched_dims, item_size), item_size - 1);
    if (isValidDepthValue(item_data[center_index])) {
        return static_cast<double>(item_data[center_index]);
    }

    return -1.0;
}

bool matchesObjectClass(const FrameWrapper &frame, const GstVideoRegionOfInterestMeta &roi) {
    if (!frame.gva_base_inference || !frame.gva_base_inference->object_class ||
        !frame.gva_base_inference->object_class[0]) {
        return true;
    }

    const gchar *roi_type = roi.roi_type ? g_quark_to_string(roi.roi_type) : "";
    const std::string object_classes = frame.gva_base_inference->object_class;
    size_t start = 0;
    while (start <= object_classes.size()) {
        const size_t separator = object_classes.find(',', start);
        const std::string token =
            trim(object_classes.substr(start, separator == std::string::npos ? std::string::npos : separator - start));
        if (!token.empty() && token == roi_type) {
            return true;
        }
        if (separator == std::string::npos) {
            break;
        }
        start = separator + 1;
    }

    return false;
}

bool isValidDepthMetric(double value) {
    return std::isfinite(value) && value > 0.0;
}

Region getCenterRegion(size_t x0, size_t y0, size_t x1, size_t y1) {
    Region center_region;
    if (x0 >= x1 || y0 >= y1) {
        return center_region;
    }

    const size_t region_height = y1 - y0;
    const size_t region_width = x1 - x0;
    const size_t center_height = std::min(region_height, CENTER_REGION_SIZE);
    const size_t center_width = std::min(region_width, CENTER_REGION_SIZE);
    size_t center_y = y0 + region_height / 2;
    size_t center_x = x0 + region_width / 2;
    center_y = center_y >= center_height / 2 ? center_y - center_height / 2 : y0;
    center_x = center_x >= center_width / 2 ? center_x - center_width / 2 : x0;
    center_y = std::min(center_y, y1 - center_height);
    center_x = std::min(center_x, x1 - center_width);

    center_region.x0 = center_x;
    center_region.y0 = center_y;
    center_region.x1 = center_x + center_width;
    center_region.y1 = center_y + center_height;
    return center_region;
}

double selectLayerDepth(const depth_converter::Metrics &metrics) {
    if (isValidDepthMetric(metrics.median)) {
        return metrics.median;
    }
    if (isValidDepthMetric(metrics.center)) {
        return metrics.center;
    }
    if (isValidDepthMetric(metrics.mean)) {
        return metrics.mean;
    }
    return -1.0;
}

double selectDepthScale(const depth_converter::Metrics &metrics, double layer_depth) {
    double scale = isValidDepthMetric(metrics.stddev) ? metrics.stddev : -1.0;

    if (!(scale > 0.0) && isValidDepthMetric(metrics.max) && isValidDepthMetric(metrics.min) &&
        metrics.max > metrics.min) {
        scale = (metrics.max - metrics.min) * 0.25;
    }

    if (!(scale > 0.0) && isValidDepthMetric(layer_depth)) {
        scale = std::abs(layer_depth) * RELATIVE_DEPTH_SCALE;
    }

    return std::max(scale, MIN_DEPTH_SCALE);
}

bool regionsOverlap(const Region &lhs, const Region &rhs) {
    return lhs.x0 < rhs.x1 && rhs.x0 < lhs.x1 && lhs.y0 < rhs.y1 && rhs.y0 < lhs.y1;
}

bool regionContains(const Region &region, size_t row, size_t col) {
    return col >= region.x0 && col < region.x1 && row >= region.y0 && row < region.y1;
}

double distanceToRegionCenterSquared(const Candidate &candidate, size_t row, size_t col) {
    const double pixel_x = static_cast<double>(col) + 0.5;
    const double pixel_y = static_cast<double>(row) + 0.5;
    const double dx = pixel_x - candidate.center_x;
    const double dy = pixel_y - candidate.center_y;
    return dx * dx + dy * dy;
}

double calculateSpatialScaleSquared(const Region &region) {
    const double half_width = static_cast<double>(region.x1 - region.x0) * 0.5;
    const double half_height = static_cast<double>(region.y1 - region.y0) * 0.5;
    return std::max(half_width * half_width + half_height * half_height, 1.0);
}

OwnershipDecision buildOwnershipDecision(const Candidate &candidate, size_t candidate_index, double depth_value,
                                         size_t row, size_t col) {
    OwnershipDecision decision;
    decision.candidate_index = candidate_index;
    decision.spatial_score = distanceToRegionCenterSquared(candidate, row, col) / candidate.spatial_scale_sq;
    decision.has_depth_model = isValidDepthMetric(candidate.layer_depth);

    if (!decision.has_depth_model) {
        decision.compatible = true;
        decision.total_score = 1.0 + SPATIAL_WEIGHT * decision.spatial_score;
        return decision;
    }

    decision.layer_depth = candidate.layer_depth;
    decision.depth_scale = std::max(candidate.depth_scale, MIN_DEPTH_SCALE);
    decision.depth_score = std::abs(depth_value - candidate.layer_depth) / decision.depth_scale;
    decision.compatible = decision.depth_score <= COMPATIBLE_DEPTH_Z_SCORE;
    decision.total_score = decision.depth_score + SPATIAL_WEIGHT * decision.spatial_score;

    // Pixels that are much closer than the candidate's modeled layer are likely
    // occluded by a foreground ROI and should be biased away from farther layers.
    if (depth_value + decision.depth_scale < candidate.layer_depth) {
        decision.total_score +=
            OCCLUDED_LAYER_PENALTY * ((candidate.layer_depth - depth_value) / decision.depth_scale);
    }

    return decision;
}

bool betterOwnershipDecision(const OwnershipDecision &candidate_decision, const OwnershipDecision &current_decision,
                             const std::vector<Candidate> &candidates) {
    if (candidate_decision.compatible != current_decision.compatible) {
        return candidate_decision.compatible;
    }

    if (candidate_decision.has_depth_model != current_decision.has_depth_model) {
        return candidate_decision.has_depth_model;
    }

    if (std::abs(candidate_decision.total_score - current_decision.total_score) > OWNERSHIP_EPSILON) {
        return candidate_decision.total_score < current_decision.total_score;
    }

    if (std::abs(candidate_decision.layer_depth - current_decision.layer_depth) > OWNERSHIP_EPSILON) {
        return candidate_decision.layer_depth < current_decision.layer_depth;
    }

    const auto &candidate = candidates[candidate_decision.candidate_index];
    const auto &current = candidates[current_decision.candidate_index];
    if (candidate.area != current.area) {
        return candidate.area < current.area;
    }

    return candidate.roi_id < current.roi_id;
}

size_t chooseDepthOwnerIndex(const std::vector<Candidate> &candidates, const std::vector<size_t> &candidate_indices,
                             size_t row, size_t col, double depth_value) {
    std::vector<OwnershipDecision> decisions;
    decisions.reserve(candidate_indices.size());

    bool front_layer_found = false;
    double front_layer_depth = std::numeric_limits<double>::infinity();
    double front_layer_scale = MIN_DEPTH_SCALE;

    for (size_t candidate_index : candidate_indices) {
        const auto decision = buildOwnershipDecision(candidates[candidate_index], candidate_index, depth_value, row, col);
        if (decision.compatible && decision.has_depth_model && decision.layer_depth < front_layer_depth) {
            front_layer_found = true;
            front_layer_depth = decision.layer_depth;
            front_layer_scale = decision.depth_scale;
        }
        decisions.push_back(decision);
    }

    size_t owner_index = decisions.front().candidate_index;
    OwnershipDecision best_decision = decisions.front();
    bool best_decision_initialized = false;

    for (const auto &decision : decisions) {
        if (front_layer_found) {
            if (!decision.compatible || !decision.has_depth_model) {
                continue;
            }

            const double layer_gap_scale = std::max(decision.depth_scale, front_layer_scale);
            const double normalized_layer_gap = (decision.layer_depth - front_layer_depth) / layer_gap_scale;
            if (normalized_layer_gap > FRONT_LAYER_Z_BAND) {
                continue;
            }
        }

        if (!best_decision_initialized || betterOwnershipDecision(decision, best_decision, candidates)) {
            best_decision = decision;
            owner_index = decision.candidate_index;
            best_decision_initialized = true;
        }
    }

    if (best_decision_initialized) {
        return owner_index;
    }

    for (const auto &decision : decisions) {
        if (!best_decision_initialized || betterOwnershipDecision(decision, best_decision, candidates)) {
            best_decision = decision;
            owner_index = decision.candidate_index;
            best_decision_initialized = true;
        }
    }

    return owner_index;
}

bool isOwnedDepthPixel(const std::vector<Candidate> &candidates, size_t current_index, size_t row, size_t col,
                       double depth_value) {
    const auto &current_candidate = candidates[current_index];
    std::vector<size_t> pixel_candidates;
    pixel_candidates.reserve(current_candidate.overlapping_candidates.size());

    for (size_t candidate_index : current_candidate.overlapping_candidates) {
        if (regionContains(candidates[candidate_index].region, row, col)) {
            pixel_candidates.push_back(candidate_index);
        }
    }

    if (pixel_candidates.empty()) {
        return false;
    }

    return chooseDepthOwnerIndex(candidates, pixel_candidates, row, col, depth_value) == current_index;
}

double mapCoordinateToModel(double coordinate, double scale, size_t cropped_border, size_t padding) {
    double mapped = coordinate;
    mapped *= scale;
    mapped -= static_cast<double>(cropped_border);
    mapped += static_cast<double>(padding);
    return mapped;
}

// ROIs arrive in original frame coordinates. This helper replays the image
// preprocessing transform so the ROI can be compared against the model's depth map.
bool mapRoiToDepthRegion(const FrameWrapper &frame, const BlobToMetaConverter &blob_to_meta,
                         const GstVideoRegionOfInterestMeta &roi, size_t depth_width, size_t depth_height, size_t &x0,
                         size_t &y0, size_t &x1, size_t &y1) {
    const auto &input_info = blob_to_meta.getInputImageInfo();
    const double input_width =
        input_info.width ? static_cast<double>(input_info.width) : static_cast<double>(frame.width);
    const double input_height =
        input_info.height ? static_cast<double>(input_info.height) : static_cast<double>(frame.height);
    if (input_width <= 0.0 || input_height <= 0.0) {
        return false;
    }

    double left = static_cast<double>(roi.x);
    double top = static_cast<double>(roi.y);
    double right = static_cast<double>(roi.x + roi.w);
    double bottom = static_cast<double>(roi.y + roi.h);

    const auto &transform = frame.image_transform_info;
    if (transform) {
        left = mapCoordinateToModel(left, transform->resize_scale_x, transform->croped_border_size_x,
                                    transform->padding_size_x);
        right = mapCoordinateToModel(right, transform->resize_scale_x, transform->croped_border_size_x,
                                     transform->padding_size_x);
        top = mapCoordinateToModel(top, transform->resize_scale_y, transform->croped_border_size_y,
                                   transform->padding_size_y);
        bottom = mapCoordinateToModel(bottom, transform->resize_scale_y, transform->croped_border_size_y,
                                      transform->padding_size_y);
    }

    left = left / input_width * static_cast<double>(depth_width);
    right = right / input_width * static_cast<double>(depth_width);
    top = top / input_height * static_cast<double>(depth_height);
    bottom = bottom / input_height * static_cast<double>(depth_height);

    left = std::clamp(left, 0.0, static_cast<double>(depth_width));
    right = std::clamp(right, 0.0, static_cast<double>(depth_width));
    top = std::clamp(top, 0.0, static_cast<double>(depth_height));
    bottom = std::clamp(bottom, 0.0, static_cast<double>(depth_height));

    x0 = static_cast<size_t>(std::floor(left));
    y0 = static_cast<size_t>(std::floor(top));
    x1 = static_cast<size_t>(std::ceil(right));
    y1 = static_cast<size_t>(std::ceil(bottom));

    x0 = std::min(x0, depth_width);
    x1 = std::min(x1, depth_width);
    y0 = std::min(y0, depth_height);
    y1 = std::min(y1, depth_height);

    return x0 < x1 && y0 < y1;
}

depth_converter::Metrics summarizeDepthRegion(const float *depth_data, size_t depth_width, size_t depth_height,
                                              size_t x0, size_t y0, size_t x1, size_t y1) {
    if (!depth_data || x0 >= x1 || y0 >= y1 || depth_width == 0 || depth_height == 0) {
        return {};
    }

    std::vector<double> valid_values;
    valid_values.reserve((x1 - x0) * (y1 - y0));

    for (size_t row = y0; row < y1; ++row) {
        for (size_t col = x0; col < x1; ++col) {
            const float value = depth_data[row * depth_width + col];
            if (depth_converter::isValidValue(value)) {
                valid_values.push_back(static_cast<double>(value));
            }
        }
    }

    const Region center_region = getCenterRegion(x0, y0, x1, y1);
    std::vector<double> center_values;
    center_values.reserve((center_region.x1 - center_region.x0) * (center_region.y1 - center_region.y0));
    for (size_t row = center_region.y0; row < center_region.y1; ++row) {
        for (size_t col = center_region.x0; col < center_region.x1; ++col) {
            const float value = depth_data[row * depth_width + col];
            if (depth_converter::isValidValue(value)) {
                center_values.push_back(static_cast<double>(value));
            }
        }
    }

    return depth_converter::buildMetrics(std::move(valid_values), std::move(center_values), (x1 - x0) * (y1 - y0));
}

depth_converter::Metrics summarizeOwnedDepthRegion(const float *depth_data, size_t depth_width, size_t depth_height,
                                                   const std::vector<Candidate> &candidates,
                                                   size_t current_index) {
    const auto &candidate = candidates[current_index];
    const auto &region = candidate.region;
    if (!depth_data || region.x0 >= region.x1 || region.y0 >= region.y1 || depth_width == 0 || depth_height == 0) {
        return {};
    }

    std::vector<double> valid_values;
    valid_values.reserve(candidate.area);

    for (size_t row = region.y0; row < region.y1; ++row) {
        for (size_t col = region.x0; col < region.x1; ++col) {
            const float value = depth_data[row * depth_width + col];
            if (!depth_converter::isValidValue(value)) {
                continue;
            }
            if (isOwnedDepthPixel(candidates, current_index, row, col, static_cast<double>(value))) {
                valid_values.push_back(static_cast<double>(value));
            }
        }
    }

    const Region center_region = getCenterRegion(region.x0, region.y0, region.x1, region.y1);
    std::vector<double> center_values;
    center_values.reserve((center_region.x1 - center_region.x0) * (center_region.y1 - center_region.y0));
    for (size_t row = center_region.y0; row < center_region.y1; ++row) {
        for (size_t col = center_region.x0; col < center_region.x1; ++col) {
            const float value = depth_data[row * depth_width + col];
            if (!depth_converter::isValidValue(value)) {
                continue;
            }
            if (isOwnedDepthPixel(candidates, current_index, row, col, static_cast<double>(value))) {
                center_values.push_back(static_cast<double>(value));
            }
        }
    }

    depth_converter::Metrics metrics =
        depth_converter::buildMetrics(std::move(valid_values), std::move(center_values), candidate.area);
    if (metrics.valid_count != 0) {
        return metrics;
    }

    // If overlap ownership filtering removed every valid sample, fall back to the
    // center crop of this ROI so attachment still produces a meaningful label.
    return summarizeDepthRegion(depth_data, depth_width, depth_height, center_region.x0, center_region.y0,
                                center_region.x1, center_region.y1);
}

// Build one candidate per existing ROI, precompute overlap relations once, and
// then summarize each ROI using only the pixels it wins from the ownership rule.
std::vector<DepthROIResult> summarizeExistingROIs(const FrameWrapper &frame, const BlobToMetaConverter &blob_to_meta,
                                                  const float *depth_data, size_t depth_width,
                                                  size_t depth_height) {
    std::vector<DepthROIResult> results;
    if (!frame.buffer || !depth_data || depth_width == 0 || depth_height == 0) {
        return results;
    }

    std::vector<Candidate> candidates;
    gpointer state = nullptr;
    GstVideoRegionOfInterestMeta *roi_meta = nullptr;
    while ((roi_meta = GST_VIDEO_REGION_OF_INTEREST_META_ITERATE(frame.buffer, &state))) {
        if (!matchesObjectClass(frame, *roi_meta)) {
            continue;
        }

        size_t x0 = 0;
        size_t y0 = 0;
        size_t x1 = 0;
        size_t y1 = 0;
        if (!mapRoiToDepthRegion(frame, blob_to_meta, *roi_meta, depth_width, depth_height, x0, y0, x1, y1)) {
            continue;
        }

        const depth_converter::Metrics initial_metrics =
            summarizeDepthRegion(depth_data, depth_width, depth_height, x0, y0, x1, y1);
        Candidate candidate;
        candidate.roi_id = roi_meta->id;
        candidate.region = {x0, y0, x1, y1};
        candidate.layer_depth = selectLayerDepth(initial_metrics);
        candidate.depth_scale = selectDepthScale(initial_metrics, candidate.layer_depth);
        candidate.center_x = (static_cast<double>(x0) + static_cast<double>(x1)) * 0.5;
        candidate.center_y = (static_cast<double>(y0) + static_cast<double>(y1)) * 0.5;
        candidate.spatial_scale_sq = calculateSpatialScaleSquared(candidate.region);
        candidate.area = (x1 - x0) * (y1 - y0);
        candidates.push_back(candidate);
    }

    for (size_t current_index = 0; current_index < candidates.size(); ++current_index) {
        auto &candidate = candidates[current_index];
        candidate.overlapping_candidates.push_back(current_index);
        for (size_t other_index = 0; other_index < candidates.size(); ++other_index) {
            if (current_index == other_index) {
                continue;
            }
            if (regionsOverlap(candidate.region, candidates[other_index].region)) {
                candidate.overlapping_candidates.push_back(other_index);
            }
        }
    }

    results.reserve(candidates.size());
    for (size_t candidate_index = 0; candidate_index < candidates.size(); ++candidate_index) {
        results.push_back({candidates[candidate_index].roi_id,
                           summarizeOwnedDepthRegion(depth_data, depth_width, depth_height, candidates,
                                                     candidate_index)});
    }

    return results;
}

GstStructure *createDepthLabelStructure(const GVA::Tensor &source_tensor, gint roi_id,
                                        const depth_converter::Metrics &metrics) {
    // ROI label tensors mirror the classic depth label field so downstream code can
    // consume ROI depth as a normal classifier-style label.
    GstStructure *label_structure = gst_structure_new_empty(source_tensor.name().c_str());
    GVA::Tensor label_tensor(label_structure);
    label_tensor.set_string("label", depth_converter::formatLabel(metrics.mean));
    label_tensor.set_int("tensor_id", roi_id);

    if (source_tensor.has_field("layer_name")) {
        label_tensor.set_layer_name(source_tensor.layer_name());
    }
    if (source_tensor.has_field("model_name")) {
        label_tensor.set_model_name(source_tensor.model_name());
    }

    return label_structure;
}

GstStructure *createDepthMetricsStructure(const GVA::Tensor &source_tensor, gint roi_id,
                                          const depth_converter::Metrics &metrics) {
    // Metrics are emitted as a second raw tensor attached to the ROI. This stays
    // within the standard tensor contract: an FP32 payload with dims/layout/
    // precision metadata, not a depth-specific GstMeta type.
    GstStructure *metrics_structure = gst_structure_new_empty(source_tensor.name().c_str());
    GVA::Tensor metrics_tensor(metrics_structure);
    const auto metrics_values = depth_converter::toTensorValues(metrics);

    metrics_tensor.set_int("tensor_id", roi_id);
    metrics_tensor.set_format("depth_metrics");
    metrics_tensor.set_precision(GVA::Tensor::Precision::FP32);
    metrics_tensor.set_layout(GVA::Tensor::Layout::NC);
    metrics_tensor.set_dims({1, static_cast<guint>(metrics_values.size())});
    metrics_tensor.set_data(metrics_values.data(), metrics_values.size() * sizeof(float));

    if (source_tensor.has_field("layer_name")) {
        metrics_tensor.set_layer_name(source_tensor.layer_name());
    }
    if (source_tensor.has_field("model_name")) {
        metrics_tensor.set_model_name(source_tensor.model_name());
    }

    return metrics_structure;
}

} // namespace

namespace post_processing::depth_converter {

std::string formatLabel(double value) {
    if (value < 0.0) {
        return "n/a";
    }

    std::ostringstream stream;
    stream << std::fixed << std::setprecision(2) << value;
    return stream.str();
}

bool isValidValue(float value) {
    return isValidDepthValue(value);
}

Metrics buildMetrics(std::vector<double> valid_values, std::vector<double> center_values, size_t region_size) {
    Metrics metrics;
    metrics.valid_count = safe_convert<uint32_t>(valid_values.size());
    metrics.valid_ratio =
        region_size ? static_cast<double>(valid_values.size()) / static_cast<double>(region_size) : 0.0;

    if (valid_values.empty()) {
        return metrics;
    }

    auto [min_it, max_it] = std::minmax_element(valid_values.begin(), valid_values.end());
    metrics.min = *min_it;
    metrics.max = *max_it;

    double sum = 0.0;
    for (double value : valid_values) {
        sum += value;
    }
    metrics.mean = sum / static_cast<double>(valid_values.size());

    metrics.median = calculateMedian(valid_values);

    double variance = 0.0;
    for (double value : valid_values) {
        const double diff = value - metrics.mean;
        variance += diff * diff;
    }
    metrics.stddev = std::sqrt(variance / static_cast<double>(valid_values.size()));
    metrics.center = calculateMedian(std::move(center_values));

    return metrics;
}

std::array<float, static_cast<size_t>(DepthMetricsField::Count)> toTensorValues(const Metrics &metrics) {
    return {static_cast<float>(metrics.center), static_cast<float>(metrics.mean),
            static_cast<float>(metrics.median), static_cast<float>(metrics.min),
            static_cast<float>(metrics.max), static_cast<float>(metrics.stddev),
            static_cast<float>(metrics.valid_count), static_cast<float>(metrics.valid_ratio)};
}

Metrics summarizeDepthMap(const float *depth_data, size_t element_count, const std::vector<size_t> &unbatched_dims) {
    if (!depth_data || element_count == 0) {
        return {};
    }

    const double center = calculateCenterRegionMedian(depth_data, element_count, unbatched_dims);

    std::vector<double> valid_values;
    valid_values.reserve(element_count);
    for (size_t index = 0; index < element_count; ++index) {
        if (isValidDepthValue(depth_data[index])) {
            valid_values.push_back(static_cast<double>(depth_data[index]));
        }
    }

    std::vector<double> center_values;
    if (center >= 0.0) {
        center_values.push_back(center);
    }

    return buildMetrics(std::move(valid_values), std::move(center_values), element_count);
}

} // namespace post_processing::depth_converter

DepthConverter::DepthConverter(BlobToMetaConverter::Initializer initializer)
    : BlobToTensorConverter(std::move(initializer)) {
}

TensorsTable DepthConverter::convert([[maybe_unused]] const OutputBlobs &model_outputs) {
    // Depth conversion depends on frame context for ROI-aware operation, so the
    // blob-only entry point is kept only as a defensive guard.
    throw std::logic_error("DepthConverter must be called with FramesWrapper context");
}

TensorsTable DepthConverter::convert(const OutputBlobs &model_outputs, FramesWrapper &frames) {
    ITT_TASK(__FUNCTION__);
    TensorsTable tensors_table;
    try {
        const size_t batch_size = getModelInputImageInfo().batch_size;
        tensors_table.resize(batch_size);

        if (frames.size() != batch_size) {
            throw std::invalid_argument("DepthConverter expects frames count to match batch size");
        }

        if (model_outputs.empty()) {
            throw std::invalid_argument("Output blobs are empty");
        }
        if (model_outputs.size() != 1) {
            throw std::invalid_argument("DepthConverter expects a single output blob");
        }

        const auto &output_layer = *model_outputs.begin();
        const std::string &output_name = output_layer.first;
        const OutputBlob::Ptr &output_blob = output_layer.second;

        if (not output_blob) {
            throw std::invalid_argument("Output blob is empty");
        }
        if (output_blob->GetData() == nullptr) {
            throw std::invalid_argument("Output blob data is nullptr");
        }

        if (output_blob->GetPrecision() != Blob::Precision::FP32) {
            throw std::runtime_error("DepthConverter expects FP32 output blob precision");
        }

        const auto unbatched_dims = getUnbatchedDims(output_blob->GetDims(), batch_size);
        const float *typed_data = reinterpret_cast<const float *>(output_blob->GetData());

        for (size_t batch_index = 0; batch_index < batch_size; ++batch_index) {
            const auto item = get_data_by_batch_index<float>(typed_data, output_blob->GetSize(), batch_size, batch_index);
            const float *depth_data = item.first;
            const size_t element_count = item.second;
            size_t depth_width = 0;
            size_t depth_height = 0;
            if (!depth_data || !getDepthMapShape(unbatched_dims, element_count, depth_width, depth_height)) {
                GST_WARNING("Depth ROI conversion skipped because depth map shape is unavailable");
                continue;
            }

            if (!skipRawTensors()) {
                // Publish the full-frame depth map to the frame before generating ROI summaries.
                GVA::Tensor full_frame_tensor = createTensor();
                CopyOutputBlobToGstStructure(output_blob, full_frame_tensor.gst_structure(),
                                             BlobToMetaConverter::getModelName().c_str(), output_name.c_str(),
                                             batch_size, batch_index);
                tensors_table[batch_index].push_back({full_frame_tensor.gst_structure()});
            }

            // The converter always emits ROI-ready tensors from the same dense map:
            // one label tensor with mean depth and one metrics tensor per ROI.
            GVA::Tensor roi_tensor_template = createTensor();
            roi_tensor_template.set_layer_name(output_name);
            roi_tensor_template.set_model_name(BlobToMetaConverter::getModelName());

            const auto roi_results = summarizeExistingROIs(frames[batch_index], *this, depth_data, depth_width,
                                                           depth_height);
            for (const auto &roi_result : roi_results) {
                if (roi_result.roi_id < 0) {
                    continue;
                }

                GstStructure *label_structure =
                    createDepthLabelStructure(roi_tensor_template, roi_result.roi_id, roi_result.metrics);
                GstStructure *metrics_structure =
                    createDepthMetricsStructure(roi_tensor_template, roi_result.roi_id, roi_result.metrics);
                tensors_table[batch_index].push_back({label_structure, metrics_structure});
            }
        }
    } catch (const std::exception &e) {
        GVA_ERROR("An error occurred while processing output BLOBs: %s", e.what());
    }

    return tensors_table;
}