/*******************************************************************************
 * Copyright (C) 2021-2025 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#include "coordinates_restorer.h"

#include "copy_blob_to_gststruct.h"
#include "gmutex_lock_guard.h"
#include "gva_utils.h"
#include "processor_types.h"

#include "inference_backend/logger.h"

#include <exception>

using namespace post_processing;

template <typename T>
void CoordinatesRestorer::restoreActualCoordinates(const FrameWrapper &frame, T &real_x, T &real_y) {
    const InferenceBackend::ImageTransformationParams::Ptr pre_proc_info = frame.image_transform_info;
    if (!(pre_proc_info && pre_proc_info->WasTransformation()))
        return;

    auto orig_img_abs_x = real_x * input_info.width;
    auto orig_img_abs_y = real_y * input_info.height;

    if (pre_proc_info->WasPadding()) {
        orig_img_abs_x -= pre_proc_info->padding_size_x;
        orig_img_abs_y -= pre_proc_info->padding_size_y;
    }
    if (pre_proc_info->WasCrop()) {
        orig_img_abs_x += pre_proc_info->croped_border_size_x;
        orig_img_abs_y += pre_proc_info->croped_border_size_y;
    }
    if (pre_proc_info->WasResize()) {
        if (pre_proc_info->resize_scale_x)
            orig_img_abs_x /= pre_proc_info->resize_scale_x;
        if (pre_proc_info->resize_scale_y)
            orig_img_abs_y /= pre_proc_info->resize_scale_y;
    }

    real_x = orig_img_abs_x / frame.roi->w;
    real_y = orig_img_abs_y / frame.roi->h;
}

void ROICoordinatesRestorer::clipNormalizedRect(double &real_x_min, double &real_y_min, double &real_x_max,
                                                double &real_y_max) {
    if (!((real_x_min >= 0) && (real_y_min >= 0) && (real_x_max < +1) && (real_y_max < +1))) {
        GST_DEBUG("ROI coordinates top_left=[%.5lf, %.5lf], right_bottom=[%.5lf, %.5lf] are out of range [0,1] and "
                  "will be clipped",
                  real_x_min, real_y_min, real_x_max, real_y_max);

        real_x_min = (real_x_min < 0) ? 0 : (real_x_min > 1) ? 1 : real_x_min;
        real_y_min = (real_y_min < 0) ? 0 : (real_y_min > 1) ? 1 : real_y_min;
        real_x_max = (real_x_max < 0) ? 0 : (real_x_max > 1) ? 1 : real_x_max;
        real_y_max = (real_y_max < 0) ? 0 : (real_y_max > 1) ? 1 : real_y_max;
    }
}

void ROICoordinatesRestorer::getAbsoluteCoordinates(int orig_image_width, int orig_image_height, double real_x_min,
                                                    double real_y_min, double real_x_max, double real_y_max,
                                                    uint32_t &abs_x, uint32_t &abs_y, uint32_t &abs_w,
                                                    uint32_t &abs_h) {
    abs_x = safe_convert<uint32_t>(real_x_min * orig_image_width + 0.5);
    abs_y = safe_convert<uint32_t>(real_y_min * orig_image_height + 0.5);
    abs_w = safe_convert<uint32_t>((real_x_max - real_x_min) * orig_image_width + 0.5);
    abs_h = safe_convert<uint32_t>((real_y_max - real_y_min) * orig_image_height + 0.5);
}

/**
 * Iterating through GstBuffer's tensors_batch and searching for meta that matching frame's ROI.
 *
 * @param[in] frame - pointer to FrameWrapper containing pointers to buffer and ROI.
 *
 * @return GstVideoRegionOfInterestMeta - meta of GstBuffer, or nullptr.
 *
 * @throw std::invalid_argument when GstBuffer is nullptr.
 */
GstVideoRegionOfInterestMeta *ROICoordinatesRestorer::findRoiMeta(const FrameWrapper &frame) {
    GstBuffer *buffer = frame.buffer;
    if (not buffer)
        throw std::invalid_argument("Inference frame's buffer is nullptr");
    auto frame_roi = frame.roi;
    GstVideoRegionOfInterestMeta *meta = nullptr;
    gpointer state = nullptr;
    while ((meta = GST_VIDEO_REGION_OF_INTEREST_META_ITERATE(buffer, &state))) {
        if (sameRegion(meta, frame_roi)) {
            return meta;
        }
    }
    return meta;
}

bool ROICoordinatesRestorer::findObjectDetectionMeta(const FrameWrapper &frame, GstAnalyticsODMtd *rlt_mtd) {
    GstBuffer *buffer = frame.buffer;
    if (not buffer)
        throw std::invalid_argument("Inference frame's buffer is nullptr");
    auto frame_roi = frame.roi;
    gpointer state = nullptr;

    GstAnalyticsRelationMeta *relation_meta = gst_buffer_get_analytics_relation_meta(buffer);
    if (relation_meta) {
        while (
            gst_analytics_relation_meta_iterate(relation_meta, &state, gst_analytics_od_mtd_get_mtd_type(), rlt_mtd)) {
            if (sameRegion(rlt_mtd, frame_roi)) {
                return true;
            }
        }
    }
    return false;
}

void ROICoordinatesRestorer::updateCoordinatesToFullFrame(double &x_min, double &y_min, double &x_max, double &y_max,
                                                          const FrameWrapper &frame) {
    /* In case of gvadetect with inference-region=roi-list we get coordinates relative to ROI.
     * We need to convert them to coordinates relative to the full frame. */
    if (attach_type == AttachType::TO_ROI) {
        GMutexLockGuard guard(frame.meta_mutex);
        GstAnalyticsODMtd od_meta;
        if (findObjectDetectionMeta(frame, &od_meta)) {
            gint od_meta_x;
            gint od_meta_y;
            gint od_meta_w;
            gint od_meta_h;

            if (!gst_analytics_od_mtd_get_location(&od_meta, &od_meta_x, &od_meta_y, &od_meta_w, &od_meta_h, nullptr)) {
                throw std::runtime_error("Error when trying to read the location of the object detection metadata");
            }

            x_min = (od_meta_x + od_meta_w * x_min) / frame.width;
            y_min = (od_meta_y + od_meta_h * y_min) / frame.height;
            x_max = (od_meta_x + od_meta_w * x_max) / frame.width;
            y_max = (od_meta_y + od_meta_h * y_max) / frame.height;
        }
    }
}

void ROICoordinatesRestorer::getRealCoordinates(GstStructure *detection_tensor, double &x_min_real, double &y_min_real,
                                                double &x_max_real, double &y_max_real) {

    if (!gst_structure_get(detection_tensor, "x_min", G_TYPE_DOUBLE, &x_min_real, "x_max", G_TYPE_DOUBLE, &x_max_real,
                           "y_min", G_TYPE_DOUBLE, &y_min_real, "y_max", G_TYPE_DOUBLE, &y_max_real, NULL)) {

        throw std::runtime_error("Invalid gst_structure_get() method field(s) requested");
    }
}

void ROICoordinatesRestorer::getCoordinates(GstStructure *detection_tensor, const FrameWrapper &frame,
                                            double &x_min_real, double &y_min_real, double &x_max_real,
                                            double &y_max_real, uint32_t &x_abs, uint32_t &y_abs, uint32_t &w_abs,
                                            uint32_t &h_abs) {

    getRealCoordinates(detection_tensor, x_min_real, y_min_real, x_max_real, y_max_real);

    if (frame.image_transform_info and frame.image_transform_info->WasTransformation()) {
        restoreActualCoordinates(frame, x_min_real, y_min_real);
        restoreActualCoordinates(frame, x_max_real, y_max_real);
    }

    updateCoordinatesToFullFrame(x_min_real, y_min_real, x_max_real, y_max_real, frame);

    clipNormalizedRect(x_min_real, y_min_real, x_max_real, y_max_real);

    getAbsoluteCoordinates(frame.width, frame.height, x_min_real, y_min_real, x_max_real, y_max_real, x_abs, y_abs,
                           w_abs, h_abs);
}

void ROICoordinatesRestorer::restore(TensorsTable &tensors_batch, const FramesWrapper &frames) {
    try {
        checkFramesAndTensorsTable(frames, tensors_batch);

        for (size_t i = 0; i < frames.size(); ++i) {
            const auto &frame = frames[i];
            const auto &tensor = tensors_batch[i];

            for (size_t j = 0; j < tensor.size(); ++j) {
                GstStructure *detection_tensor = tensor[j][DETECTION_TENSOR_ID];

                double x_min_real, y_min_real, x_max_real, y_max_real;
                uint32_t x_abs, y_abs, w_abs, h_abs;
                getCoordinates(detection_tensor, frame, x_min_real, y_min_real, x_max_real, y_max_real, x_abs, y_abs,
                               w_abs, h_abs);

                gst_structure_set(detection_tensor, "x_min", G_TYPE_DOUBLE, x_min_real, "x_max", G_TYPE_DOUBLE,
                                  x_max_real, "y_min", G_TYPE_DOUBLE, y_min_real, "y_max", G_TYPE_DOUBLE, y_max_real,
                                  "x_abs", G_TYPE_UINT, x_abs, "y_abs", G_TYPE_UINT, y_abs, "w_abs", G_TYPE_UINT, w_abs,
                                  "h_abs", G_TYPE_UINT, h_abs, NULL);
            }
        }
    } catch (const std::exception &e) {
        GVA_ERROR("An error occurred while restoring coordinates for ROI: %s", e.what());
    }
}

void KeypointsCoordinatesRestorer::restore(TensorsTable &tensors, const FramesWrapper &frames) {
    try {
        checkFramesAndTensorsTable(frames, tensors);

        for (size_t i = 0; i < frames.size(); ++i) {
            const auto &frame = frames[i];
            const auto &tensor = tensors[i];

            for (size_t j = 0; j < tensor.size(); ++j) {
                GstStructure *result_tensor_raw = tensor[j][DETECTION_TENSOR_ID];
                GVA::Tensor result_tensor(result_tensor_raw);

                auto data = result_tensor.data<float>();

                if (data.empty())
                    throw std::runtime_error("Keypoints is empty.");

                const auto &dimension = result_tensor.dims();
                size_t points_num = dimension[0];
                size_t point_dimension = dimension[1];

                if (data.size() != points_num * point_dimension)
                    throw std::logic_error("The size of the keypoints data does not match the dimension: Size=" +
                                           std::to_string(data.size()) + " Dimension=[" + std::to_string(dimension[0]) +
                                           "," + std::to_string(dimension[1]) + "].");

                auto &updated_points = data;
                for (size_t i = 0; i < points_num; ++i) {
                    if (data[i * point_dimension] == -1.0f and data[i * point_dimension + 1] == -1.0f)
                        continue;

                    restoreActualCoordinates(frame, updated_points[i * point_dimension],
                                             updated_points[i * point_dimension + 1]);
                }

                copy_buffer_to_structure(result_tensor_raw, reinterpret_cast<const void *>(updated_points.data()),
                                         updated_points.size() * sizeof(float));
            }
        }
    } catch (const std::exception &e) {
        GVA_ERROR("An error occurred while restoring coordinates for keypoints: %s", e.what());
    }
}
