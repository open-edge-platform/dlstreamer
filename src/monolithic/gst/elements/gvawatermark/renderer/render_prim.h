/*******************************************************************************
 * Copyright (C) 2020-2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#pragma once

#include <string>
#include <variant>

#include <opencv2/opencv.hpp>

namespace render {

struct Text {
    std::string text;
    cv::Point org;
    int fonttype;
    double fontscale;
    cv::Scalar color;
    int thick;

    Text() = default;

    Text(const std::string &text, const cv::Point &org, int fonttype, double fontscale, const cv::Scalar &color,
         int thick = 1)
        : text(text), org(org), fonttype(fonttype), fontscale(fontscale), color(color), thick(thick) {
    }
};

struct Rect {
    cv::Rect rect;
    cv::Scalar color;
    int thick;
    double rotation;

    Rect() = default;

    Rect(const cv::Rect &rect, const cv::Scalar &color, int thick = 1, double r = 0.0)
        : rect(rect), color(color), thick(thick), rotation(r) {
    }
};

struct Circle {
    cv::Point center;
    int radius;
    cv::Scalar color;
    int thick;

    Circle() = default;

    Circle(const cv::Point &center, int radius, const cv::Scalar &color, int thick = 1)
        : center(center), radius(radius), color(color), thick(thick) {
    }
};

struct Line {
    cv::Point pt1;
    cv::Point pt2;
    cv::Scalar color;
    int thick;

    Line() = default;

    Line(const cv::Point &pt1, const cv::Point &pt2, const cv::Scalar &color, int thick = 1)
        : pt1(pt1), pt2(pt2), color(color), thick(thick) {
    }
};

struct InstanceSegmantationMask {
    std::vector<float> data;
    cv::Size size;
    cv::Scalar color;
    cv::Rect2f box;

    InstanceSegmantationMask() = default;

    InstanceSegmantationMask(const std::vector<float> &data, const cv::Size &size, const cv::Scalar &color,
                             const cv::Rect2f &box)
        : data(data), size(size), color(color), box(box) {
    }
};

struct SemanticSegmantationMask {
    std::vector<int64_t> data;
    cv::Size size;
    cv::Rect2f box;

    SemanticSegmantationMask() = default;

    SemanticSegmantationMask(const std::vector<int64_t> &data, const cv::Size &size, const cv::Rect2f &box)
        : data(data), size(size), box(box) {
    }
};

using Prim = std::variant<Text, Rect, Circle, Line, InstanceSegmantationMask, SemanticSegmantationMask>;

} // namespace render
