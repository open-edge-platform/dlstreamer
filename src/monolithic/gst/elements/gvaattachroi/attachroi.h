/*******************************************************************************
 * Copyright (C) 2018-2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#pragma once

#include <gst/gst.h>
#include <nlohmann/json.hpp>

#include "video_frame.h"

#define ROI_FORMAT_STRING "x_top_left,y_top_left,x_bottom_right,y_bottom_right"

class AttachRoi {
  public:
    enum class Mode : gint {
        InOrder = 0,
        InLoop,
        ByTimestamp,
    };

  public:
    AttachRoi(const char *filepath, const char *roi_str, Mode mode);

    void attachMetas(GVA::VideoFrame &vframe, GstBuffer *buffer, GstClockTime timestamp);

  private:
    void loadJsonFromFile(const char *filepath);
    void setRoiFromString(const char *roi_str);

    void addStaticRoi(GVA::VideoFrame &vframe) const;
    void addRoiFromJson(GVA::VideoFrame &vframe, GstClockTime timestamp) const;
    void addTensorFromJson(GVA::VideoFrame &vframe, GstBuffer *buffer, GstClockTime timestamp) const;

    std::pair<bool, size_t> findJsonIndex(GstClockTime timestamp) const;

  private:
    Mode _mode;
    // Current frame number
    size_t _frame_num = 0;
    nlohmann::json _roi_json;
    // Maps timestamp to index in JSON
    std::unordered_map<GstClockTime, size_t> _ts_map;

    // Static ROI information. Will be added to every frame.
    struct {
        guint x_top_left = 0, y_top_left = 0, x_bottom_right = 0, y_bottom_right = 0;

        bool empty() const {
            return !x_top_left && !y_top_left && !x_bottom_right && !y_bottom_right;
        }

        bool valid() const {
            return x_bottom_right > x_top_left && y_bottom_right > y_top_left;
        }

        guint width() const {
            return x_bottom_right - x_top_left;
        }

        guint height() const {
            return y_bottom_right - y_top_left;
        }
    } _roi;
};
