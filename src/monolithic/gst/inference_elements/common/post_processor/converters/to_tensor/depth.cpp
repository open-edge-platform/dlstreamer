/*******************************************************************************
 * Copyright (C) 2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#include "depth.h"

#include "copy_blob_to_gststruct.h"
#include "inference_backend/image_inference.h"
#include "inference_backend/logger.h"
#include "safe_arithmetic.hpp"
#include "tensor.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <limits>
#include <string>
#include <type_traits>
#include <vector>

using namespace post_processing;
using namespace InferenceBackend;

namespace {

constexpr size_t CENTER_REGION_SIZE = 5;

struct DepthMetrics {
    double center = -1.0;
    double mean = -1.0;
    double median = -1.0;
    double min = -1.0;
    double max = -1.0;
    double stddev = -1.0;
    uint32_t valid_count = 0;
    double valid_ratio = 0.0;
};

// Treat positive finite values as valid depth samples.
template <typename T>
bool isValidDepthValue(T value) {
    if constexpr (std::is_floating_point_v<T>) {
        return std::isfinite(value) && value > 0;
    }

    return value > 0;
}

// Pick the center pixel when no spatial center window can be formed.
size_t getCenterIndex(const std::vector<size_t> &dims, size_t item_size) {
    if (item_size == 0) {
        return 0;
    }

    if (dims.size() >= 2) {
        const size_t height = dims[dims.size() - 2];
        const size_t width = dims[dims.size() - 1];
        const size_t spatial_size = height * width;
        if (spatial_size != 0 && item_size % spatial_size == 0) {
            const size_t center_offset = (height / 2) * width + (width / 2);
            return center_offset;
        }
    }

    return item_size / 2;
}

// Compute the median of a value set, returning -1 when there is no valid sample.
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

// Remove the batch dimension so the remaining shape describes a single depth map.
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

// Use the median over a fixed center window to get a more stable central depth estimate.
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

// Summarize one depth map into scalar metrics used by the output tensor.
template <typename T>
DepthMetrics calculateDepthMetrics(const T *item_data, size_t item_size, const std::vector<size_t> &unbatched_dims) {
    DepthMetrics metrics;
    if (!item_data || item_size == 0) {
        return metrics;
    }

    metrics.center = calculateCenterRegionMedian(item_data, item_size, unbatched_dims);

    std::vector<double> valid_values;
    valid_values.reserve(item_size);
    for (size_t index = 0; index < item_size; ++index) {
        if (isValidDepthValue(item_data[index])) {
            valid_values.push_back(static_cast<double>(item_data[index]));
        }
    }

    // Report how many samples contributed to the summary and what fraction of the map they represent.
    metrics.valid_count = safe_convert<uint32_t>(valid_values.size());
    metrics.valid_ratio =
        item_size == 0 ? 0.0 : static_cast<double>(valid_values.size()) / static_cast<double>(item_size);

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

    return metrics;
}

// Populate the interpreted tensor fields for one batch item.
template <typename T>
void setDepthMetrics(GVA::Tensor &summary_tensor, const T *data, size_t size, size_t batch_size, size_t batch_index,
                     const std::vector<size_t> &unbatched_dims) {
    const auto item = get_data_by_batch_index<T>(data, size, batch_size, batch_index);
    const T *item_data = item.first;
    const size_t item_size = item.second;
    const DepthMetrics metrics = calculateDepthMetrics(item_data, item_size, unbatched_dims);

    summary_tensor.set_string("label", "depth");
    summary_tensor.set_double("depth_center", metrics.center);
    summary_tensor.set_double("depth_mean", metrics.mean);
    summary_tensor.set_double("depth_median", metrics.median);
    summary_tensor.set_double("depth_min", metrics.min);
    summary_tensor.set_double("depth_max", metrics.max);
    summary_tensor.set_double("depth_stddev", metrics.stddev);
    summary_tensor.set_int("depth_valid_count", metrics.valid_count);
    summary_tensor.set_double("depth_valid_ratio", metrics.valid_ratio);
}

} // namespace

// Convert a single depth output into summary metadata and optionally copy raw tensor data into it.
TensorsTable DepthConverter::convert(const OutputBlobs &model_outputs) {
    ITT_TASK(__FUNCTION__);
    TensorsTable tensors_table;
    try {
        const size_t batch_size = getModelInputImageInfo().batch_size;
        tensors_table.resize(batch_size);

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

        const auto unbatched_dims = getUnbatchedDims(output_blob->GetDims(), batch_size);

        if (output_blob->GetPrecision() != Blob::Precision::FP32) {
            throw std::runtime_error("DepthConverter expects FP32 output blob precision");
        }

        const float *typed_data = reinterpret_cast<const float *>(output_blob->GetData());
        for (size_t batch_index = 0; batch_index < batch_size; ++batch_index) {
            GVA::Tensor depth_summary = createTensor();
            depth_summary.set_type("depth_result");
            depth_summary.set_format("depth_summary");

            if (!skipRawTensors()) {
                CopyOutputBlobToGstStructure(output_blob, depth_summary.gst_structure(),
                                             BlobToMetaConverter::getModelName().c_str(), output_name.c_str(),
                                             batch_size, batch_index);
            }

            setDepthMetrics<float>(depth_summary, typed_data, output_blob->GetSize(), batch_size, batch_index,
                                   unbatched_dims);

            gst_structure_set(depth_summary.gst_structure(), "tensor_id", G_TYPE_INT, safe_convert<int>(batch_index),
                              NULL);

            std::vector<GstStructure *> tensors{depth_summary.gst_structure()};

            tensors_table[batch_index].push_back(tensors);
        }
    } catch (const std::exception &e) {
        GVA_ERROR("An error occurred while processing output BLOBs: %s", e.what());
    }
    return tensors_table;
}