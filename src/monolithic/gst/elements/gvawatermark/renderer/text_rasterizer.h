/*******************************************************************************
 * Copyright (C) 2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#pragma once

#include "render_prim.h"

#include <opencv2/core/version.hpp>
#include <opencv2/imgproc.hpp>

namespace render {

inline constexpr int FONT_WEIGHT = 400;
inline constexpr int FONT_WEIGHT_BOLD = 600;
inline constexpr double HERSHEY_SCALE_FACTOR = 3.7;

inline int font_weight(const Text &text) {
    return text.thick <= 1 ? FONT_WEIGHT : FONT_WEIGHT_BOLD;
}

inline int font_size(const Text &text) {
    return cvRound(100 * text.fontscale / HERSHEY_SCALE_FACTOR);
}

inline cv::Size get_text_size_compat(const Text &text, int *baseline) {
    cv::FontFace face("sans");
    const cv::Rect bounds = cv::getTextSize(cv::Size(), text.text, cv::Point(), face, font_size(text),
                                            font_weight(text), cv::PUT_TEXT_ALIGN_LEFT);
    const int base_line = bounds.y + bounds.height;
    if (baseline)
        *baseline = base_line;
    return cv::Size(bounds.width, bounds.height - base_line);
}

inline void put_text_compat(cv::Mat &image, const Text &text, cv::Point org, const cv::Scalar &color) {
    cv::FontFace face("sans");
    cv::putText(image, text.text, org, color, face, font_size(text), font_weight(text), cv::PUT_TEXT_ALIGN_LEFT);
}

} // namespace render
