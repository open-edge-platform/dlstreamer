/*******************************************************************************
 * Copyright (C) 2021-2025 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#pragma once

#include "post_proc_common.h"

#include <gst/gst.h>
#include <gst/video/gstvideometa.h>

#include <memory>

namespace post_processing {
class CoordinatesRestorer {
  protected:
    const ModelImageInputInfo input_info;
    AttachType attach_type;

    template <typename T>
    void restoreActualCoordinates(const FrameWrapper &frame, T &real_x, T &real_y);

  public:
    CoordinatesRestorer(const ModelImageInputInfo &input_info, AttachType type)
        : input_info(input_info), attach_type(type) {
    }

    virtual void restore(TensorsTable &tensors_batch, const FramesWrapper &frames) = 0;

    using Ptr = std::unique_ptr<CoordinatesRestorer>;

    virtual ~CoordinatesRestorer() = default;
};

class ROICoordinatesRestorer : public CoordinatesRestorer {
  protected:
    void clipNormalizedRect(double &real_x_min, double &real_y_min, double &real_x_max, double &real_y_max);
    GstVideoRegionOfInterestMeta *findRoiMeta(const FrameWrapper &frame);
    bool findObjectDetectionMeta(const FrameWrapper &frame, GstAnalyticsODMtd *rlt_mtd);
    void updateCoordinatesToFullFrame(double &x_min, double &y_min, double &x_max, double &y_max,
                                      const FrameWrapper &frame);
    void getAbsoluteCoordinates(int orig_image_width, int orig_image_height, double real_x_min, double real_y_min,
                                double real_x_max, double real_y_max, uint32_t &abs_x, uint32_t &abs_y, uint32_t &abs_w,
                                uint32_t &abs_h);
    void getRealCoordinates(GstStructure *detection_tensor, double &x_min_real, double &y_min_real, double &x_max_real,
                            double &y_max_real);
    void getCoordinates(GstStructure *detection_tensor, const FrameWrapper &frame, double &x_min_real,
                        double &y_min_real, double &x_max_real, double &y_max_real, uint32_t &x_abs, uint32_t &y_abs,
                        uint32_t &w_abs, uint32_t &h_abs);

  public:
    ROICoordinatesRestorer(const ModelImageInputInfo &input_info, AttachType type)
        : CoordinatesRestorer(input_info, type) {
    }

    virtual void restore(TensorsTable &tensors_batch, const FramesWrapper &frames) override;
};

class KeypointsCoordinatesRestorer : public CoordinatesRestorer {
  protected:
    std::vector<float> extractKeypointsData(GstStructure *s);

    GstVideoRegionOfInterestMeta *findDetectionMeta(const FrameWrapper &frame);
    void updateCoordinatesToFullFrame(float &x, float &y, const FrameWrapper &frame);

  public:
    KeypointsCoordinatesRestorer(const ModelImageInputInfo &input_info, AttachType type)
        : CoordinatesRestorer(input_info, type) {
    }

    void restore(TensorsTable &tensors_batch, const FramesWrapper &frames) override;
};

} // namespace post_processing
