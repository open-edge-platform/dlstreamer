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

inline bool uses_native_triplex(const Text &text) {
    return text.fonttype == cv::FONT_HERSHEY_TRIPLEX;
}

#if CV_VERSION_MAJOR >= 6
inline constexpr int TRIPLEX_FONT_WEIGHT = 400;
inline constexpr double HERSHEY_SCALE_FACTOR = 3.7;

inline int triplex_font_weight(const Text &text) {
    return text.thick <= 1 ? TRIPLEX_FONT_WEIGHT : 600;
}

inline int triplex_font_size(const Text &text) {
    return cvRound(100 * text.fontscale / HERSHEY_SCALE_FACTOR);
}
#endif

inline cv::Size get_text_size_compat(const Text &text, int *baseline) {
#if CV_VERSION_MAJOR >= 6
    if (uses_native_triplex(text)) {
        cv::FontFace face("serif");
        const cv::Rect bounds = cv::getTextSize(cv::Size(), text.text, cv::Point(), face, triplex_font_size(text),
                                                triplex_font_weight(text), cv::PUT_TEXT_ALIGN_LEFT);
        const int base_line = bounds.y + bounds.height;
        if (baseline)
            *baseline = base_line;
        return cv::Size(bounds.width, bounds.height - base_line);
    }
#endif
    return cv::getTextSize(text.text, text.fonttype, text.fontscale, text.thick, baseline);
}

inline void put_text_compat(cv::Mat &image, const Text &text, cv::Point org, const cv::Scalar &color) {
#if CV_VERSION_MAJOR >= 5
    if (uses_native_triplex(text)) {
        cv::FontFace face("serif");
        cv::Mat mask = cv::Mat::zeros(image.size(), CV_8UC1);
        cv::putText(mask, text.text, org, cv::Scalar(255), face, triplex_font_size(text), triplex_font_weight(text),
                    cv::PUT_TEXT_ALIGN_LEFT);
        cv::threshold(mask, mask, 127, 255, cv::THRESH_BINARY);
        image.setTo(color, mask);
        return;
    }
#endif

    cv::putText(image, text.text, org, text.fonttype, text.fontscale, color, text.thick, cv::LINE_8);
}

} // namespace render
