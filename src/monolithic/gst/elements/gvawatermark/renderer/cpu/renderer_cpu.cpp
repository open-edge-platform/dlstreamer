/*******************************************************************************
 * Copyright (C) 2020-2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#include "renderer_cpu.h"

#include "render_prim.h"
#include <inference_backend/buffer_mapper.h>
#include <opencv2/opencv.hpp>

namespace {

const std::vector<cv::Vec3b> PascalVoc21ClColorPalette = {
    cv::Vec3b(0, 0, 0),       // background
    cv::Vec3b(128, 0, 0),     // aeroplane
    cv::Vec3b(0, 128, 0),     // bicycle
    cv::Vec3b(128, 128, 0),   // bird
    cv::Vec3b(0, 0, 128),     // boat
    cv::Vec3b(128, 0, 128),   // bottle
    cv::Vec3b(0, 128, 128),   // bus
    cv::Vec3b(128, 128, 128), // car
    cv::Vec3b(64, 0, 0),      // cat
    cv::Vec3b(192, 0, 0),     // chair
    cv::Vec3b(64, 128, 0),    // cow
    cv::Vec3b(192, 128, 0),   // diningtable
    cv::Vec3b(64, 0, 128),    // dog
    cv::Vec3b(192, 0, 128),   // horse
    cv::Vec3b(64, 128, 128),  // motorbike
    cv::Vec3b(192, 128, 128), // person
    cv::Vec3b(0, 64, 0),      // pottedplant
    cv::Vec3b(128, 64, 0),    // sheep
    cv::Vec3b(0, 192, 0),     // sofa
    cv::Vec3b(128, 192, 0),   // train
    cv::Vec3b(0, 64, 128)     // tvmonitor
};

template <int n>
void check_planes(const std::vector<cv::Mat> &p) {
    if (p.size() != n) {
        throw std::runtime_error("plane count != " + std::to_string(n));
    }
}

int calc_thick_for_u_v_planes(int thick) {
    if (thick <= 1)
        return thick;
    return thick / 2;
}

// cv::Point2f calc_point_for_u_v_planes(cv::Point2f pt) {
//     return pt / 2.f;
// }

cv::Point2i calc_point_for_u_v_planes(cv::Point2i pt) {
    return pt / 2;
}

} // namespace

template <typename T>
T clamp(T value, T low, T high) {
    return value < low ? low : (value > high ? high : value);
}

cv::Rect expand_box(const cv::Rect2f &box, float w_scale, float h_scale) {
    float w_half = box.width * 0.5f * w_scale, h_half = box.height * 0.5f * h_scale;
    const cv::Point2f &center = (box.tl() + box.br()) * 0.5f;
    return {cv::Point(int(center.x - w_half), int(center.y - h_half)),
            cv::Point(int(center.x + w_half), int(center.y + h_half))};
}

RendererCPU::~RendererCPU() {
}

dlstreamer::FramePtr RendererCPU::buffer_map(dlstreamer::FramePtr buffer) {
    auto result = _buffer_mapper->map(buffer, dlstreamer::AccessMode::ReadWrite);
    return result;
}

void RendererCPU::DrawRotatedRectangle(cv::InputOutputArray img, cv::Point pt1, cv::Point pt2, double rotation,
                                       const cv::Scalar &color, int thickness, int lineType, int shift) {
    if (rotation == 0.0)
        cv::rectangle(img, pt1, pt2, color, thickness, lineType, shift);
    else {
        cv::RotatedRect rotatedRectangle(cv::Point2f((pt1.x + pt2.x) / 2, (pt1.y + pt2.y) / 2),
                                         cv::Size2f(abs(pt2.x - pt1.x), abs(pt2.y - pt1.y)), rotation * 180 / CV_PI);
        cv::Point2f vertices2f[4];
        rotatedRectangle.points(vertices2f);
        for (int i = 0; i < 4; i++)
            line(img, vertices2f[i], vertices2f[(i + 1) % 4], color, thickness);
    }
}

void RendererYUV::draw_backend(std::vector<cv::Mat> &image_planes, std::vector<render::Prim> &prims) {
    for (auto &p : prims) {
        if (std::holds_alternative<render::Line>(p)) {
            draw_line(image_planes, std::get<render::Line>(p));
        } else if (std::holds_alternative<render::Rect>(p)) {
            draw_rectangle(image_planes, std::get<render::Rect>(p));
        } else if (std::holds_alternative<render::Circle>(p)) {
            draw_circle(image_planes, std::get<render::Circle>(p));
        } else if (std::holds_alternative<render::Text>(p)) {
            draw_text(image_planes, std::get<render::Text>(p));
        } else if (std::holds_alternative<render::InstanceSegmantationMask>(p)) {
            draw_instance_mask(image_planes, std::get<render::InstanceSegmantationMask>(p));
        } else if (std::holds_alternative<render::SemanticSegmantationMask>(p)) {
            draw_semantic_mask(image_planes, std::get<render::SemanticSegmantationMask>(p));
        }
    }
}

void RendererYUV::draw_rect_y_plane(cv::Mat &y, cv::Point2i pt1, cv::Point2i pt2, double rotation, double color,
                                    int thick) {
    // Half thickness
    thick = calc_thick_for_u_v_planes(thick);

    // Pixel perfect rectangle.
    // Every pixel on U & V planes corresponds to two pixels on Y plane.
    // So, to avoid shadows, there's need to fill two pixels on Y plane, for every single pixel on U & V planes.
    // Since coordinates may not be a multiple of two, the two rectangles should be drawn with half thickness on Y
    // plane.
    DrawRotatedRectangle(y, pt1, pt2, rotation, color, thick);
    float offset_min_x = pt1.x % 2 ? -1 : 1;
    float offset_min_y = pt1.y % 2 ? -1 : 1;
    float offset_max_x = pt2.x % 2 ? -1 : 1;
    float offset_max_y = pt2.y % 2 ? -1 : 1;
    cv::Point2i p1(offset_min_x + pt1.x, offset_min_y + pt1.y);
    cv::Point2i p2(offset_max_x + pt2.x, offset_max_y + pt2.y);
    DrawRotatedRectangle(y, p1, p2, rotation, color, thick);
}

void RendererI420::draw_rectangle(std::vector<cv::Mat> &mats, render::Rect rect) {
    check_planes<3>(mats);
    cv::Mat &y = mats[0];
    cv::Mat &u = mats[1];
    cv::Mat &v = mats[2];

    const cv::Point2i top_left = rect.rect.tl();
    // align with render::render behavior
    const cv::Point2i bottom_right = rect.rect.br() - cv::Point2i(1, 1);

    const int thick = calc_thick_for_u_v_planes(rect.thick);
    DrawRotatedRectangle(u, calc_point_for_u_v_planes(top_left), calc_point_for_u_v_planes(bottom_right), rect.rotation,
                         rect.color[1], thick);
    DrawRotatedRectangle(v, calc_point_for_u_v_planes(top_left), calc_point_for_u_v_planes(bottom_right), rect.rotation,
                         rect.color[2], thick);

    draw_rect_y_plane(y, top_left, bottom_right, rect.rotation, rect.color[0], rect.thick);
}

void RendererI420::draw_circle(std::vector<cv::Mat> &mats, render::Circle circle) {
    check_planes<3>(mats);
    cv::Mat &y = mats[0];
    cv::Mat &u = mats[1];
    cv::Mat &v = mats[2];

    cv::circle(y, circle.center, circle.radius, circle.color[0], cv::FILLED);
    cv::Point2i pos_u_v(calc_point_for_u_v_planes(circle.center));
    cv::circle(u, pos_u_v, circle.radius / 2, circle.color[1], cv::FILLED);
    cv::circle(v, pos_u_v, circle.radius / 2, circle.color[2], cv::FILLED);
}

void RendererI420::draw_text(std::vector<cv::Mat> &mats, render::Text text) {
    check_planes<3>(mats);
    cv::Mat &y = mats[0];
    cv::Mat &u = mats[1];
    cv::Mat &v = mats[2];

    cv::putText(y, text.text, text.org, text.fonttype, text.fontscale, text.color[0], text.thick);
    cv::Point2i pos_u_v(calc_point_for_u_v_planes(text.org));
    int thick = calc_thick_for_u_v_planes(text.thick);
    cv::putText(u, text.text, pos_u_v, text.fonttype, text.fontscale / 2.0, text.color[1], thick);
    cv::putText(v, text.text, pos_u_v, text.fonttype, text.fontscale / 2.0, text.color[2], thick);
}

void RendererI420::draw_line(std::vector<cv::Mat> &mats, render::Line line) {
    check_planes<3>(mats);
    cv::Mat &y = mats[0];
    cv::Mat &u = mats[1];
    cv::Mat &v = mats[2];

    cv::line(y, line.pt1, line.pt2, line.color[0], line.thick);

    cv::Point2f pos1_u_v(calc_point_for_u_v_planes(line.pt1));
    cv::Point2f pos2_u_v(calc_point_for_u_v_planes(line.pt2));
    int thick = calc_thick_for_u_v_planes(line.thick);
    cv::line(u, pos1_u_v, pos2_u_v, line.color[1], thick);
    cv::line(v, pos1_u_v, pos2_u_v, line.color[2], thick);
}

void RendererI420::draw_instance_mask(std::vector<cv::Mat> &mats, render::InstanceSegmantationMask mask) {
    (void)mats;
    (void)mask;
    throw std::logic_error("Drawing instance segmentation masks is not yet supported for I420 video format. "
                           "Currently supported formats: BGR, RGB, BGRx, RGBx, BGRA and NV12.");
}

void RendererI420::draw_semantic_mask(std::vector<cv::Mat> &mats, render::SemanticSegmantationMask mask) {
    (void)mats;
    (void)mask;
    throw std::logic_error("Drawing semantic segmentation masks is not yet supported for I420 video format. "
                           "Currently supported formats: BGR, RGB, BGRx, RGBx, BGRA only.");
}

void RendererNV12::draw_rectangle(std::vector<cv::Mat> &mats, render::Rect rect) {
    check_planes<2>(mats);
    cv::Mat &y = mats[0];
    cv::Mat &u_v = mats[1];

    const cv::Point2i top_left = rect.rect.tl();
    // align with render::render behavior
    const cv::Point2i bottom_right = rect.rect.br() - cv::Point2i(1, 1);

    DrawRotatedRectangle(u_v, calc_point_for_u_v_planes(top_left), calc_point_for_u_v_planes(bottom_right),
                         rect.rotation, {rect.color[1], rect.color[2]}, calc_thick_for_u_v_planes(rect.thick));

    draw_rect_y_plane(y, top_left, bottom_right, rect.rotation, rect.color[0], rect.thick);
}

void RendererNV12::draw_circle(std::vector<cv::Mat> &mats, render::Circle circle) {
    check_planes<2>(mats);
    cv::Mat &y = mats[0];
    cv::Mat &u_v = mats[1];

    cv::circle(y, circle.center, circle.radius, circle.color[0], cv::FILLED);
    cv::Point2i pos_u_v(calc_point_for_u_v_planes(circle.center));
    cv::circle(u_v, pos_u_v, circle.radius / 2, {circle.color[1], circle.color[2]}, cv::FILLED);
}

void RendererNV12::draw_text(std::vector<cv::Mat> &mats, render::Text text) {
    check_planes<2>(mats);
    cv::Mat &y = mats[0];
    cv::Mat &u_v = mats[1];

    cv::putText(y, text.text, text.org, text.fonttype, text.fontscale, text.color[0], text.thick);
    cv::Point2i pos_u_v(calc_point_for_u_v_planes(text.org));
    cv::putText(u_v, text.text, pos_u_v, text.fonttype, text.fontscale / 2.0, {text.color[1], text.color[2]},
                calc_thick_for_u_v_planes(text.thick));
}

void RendererNV12::draw_line(std::vector<cv::Mat> &mats, render::Line line) {
    check_planes<2>(mats);
    cv::Mat &y = mats[0];
    cv::Mat &u_v = mats[1];

    cv::line(y, line.pt1, line.pt2, line.color[0], line.thick);
    cv::Point2f pos1_u_v(calc_point_for_u_v_planes(line.pt1));
    cv::Point2f pos2_u_v(calc_point_for_u_v_planes(line.pt2));
    cv::line(u_v, pos1_u_v, pos2_u_v, {line.color[1], line.color[2]}, calc_thick_for_u_v_planes(line.thick));
}

void RendererNV12::draw_instance_mask(std::vector<cv::Mat> &mats, render::InstanceSegmantationMask mask) {
    check_planes<2>(mats);
    cv::Mat &y = mats[0];
    cv::Mat &u_v = mats[1];

    cv::Mat unpadded{mask.size, CV_32F, mask.data.data()};
    cv::Mat raw_cls_mask;
    cv::copyMakeBorder(unpadded, raw_cls_mask, 1, 1, 1, 1, cv::BORDER_CONSTANT, {0});
    cv::Rect extended_box = expand_box(mask.box, float(raw_cls_mask.cols) / (raw_cls_mask.cols - 2),
                                       float(raw_cls_mask.rows) / (raw_cls_mask.rows - 2));

    int w = std::max(extended_box.width + 1, 1);
    int h = std::max(extended_box.height + 1, 1);
    int x0_y = clamp(extended_box.x, 0, y.cols);
    int y0_y = clamp(extended_box.y, 0, y.rows);
    int x1_y = clamp(extended_box.x + extended_box.width + 1, 0, y.cols);
    int y1_y = clamp(extended_box.y + extended_box.height + 1, 0, y.rows);

    auto p0_u_v = calc_point_for_u_v_planes(cv::Point(x0_y, y0_y));
    auto p1_u_v = calc_point_for_u_v_planes(cv::Point(x1_y, y1_y));

    int x0_u_v = p0_u_v.x;
    int y0_u_v = p0_u_v.y;
    int x1_u_v = p1_u_v.x;
    int y1_u_v = p1_u_v.y;

    cv::Mat resized_y;
    cv::resize(raw_cls_mask, resized_y, {w, h});

    cv::Mat resized_u_v;
    auto max_point = calc_point_for_u_v_planes(cv::Point(w, h));
    cv::resize(raw_cls_mask, resized_u_v, {max_point.x + 1, max_point.y + 1});

    cv::Mat binaryMask_y;
    cv::threshold(resized_y({cv::Point(x0_y - extended_box.x, y0_y - extended_box.y),
                             cv::Point(x1_y - extended_box.x, y1_y - extended_box.y)}),
                  binaryMask_y, 0.5f, 1.0f, cv::THRESH_BINARY);
    binaryMask_y.convertTo(binaryMask_y, y.type());

    cv::Mat binaryMask_u_v;
    auto extended_box_u_v = calc_point_for_u_v_planes(cv::Point(extended_box.x, extended_box.y));
    cv::threshold(resized_u_v({cv::Point((x0_u_v - extended_box_u_v.x), (y0_u_v - extended_box_u_v.y)),
                               cv::Point((x1_u_v - extended_box_u_v.x), (y1_u_v - extended_box_u_v.y))}),
                  binaryMask_u_v, 0.5f, 1.0f, cv::THRESH_BINARY);
    binaryMask_u_v.convertTo(binaryMask_u_v, u_v.type());

    cv::Rect roi_y(x0_y, y0_y, x1_y - x0_y, y1_y - y0_y);
    cv::Rect roi_u_v(x0_u_v, y0_u_v, x1_u_v - x0_u_v, y1_u_v - y0_u_v);
    cv::Mat colorMask_u_v(roi_u_v.size(), u_v.type(), mask.color[1]);

    cv::Mat roiSrc_y = y(roi_y);
    cv::Mat roiSrc_u_v = u_v(roi_u_v);
    cv::Mat dst_y, dst_u_v;
    float alpha = 0.5f;
    cv::addWeighted(colorMask_u_v, alpha, roiSrc_u_v, 1.0 - alpha, 0.0, dst_u_v);

    dst_u_v.copyTo(roiSrc_u_v, binaryMask_u_v);
}

void RendererNV12::draw_semantic_mask(std::vector<cv::Mat> &mats, render::SemanticSegmantationMask mask) {
    (void)mats;
    (void)mask;
    throw std::logic_error("Drawing semantic segmentation masks is not yet supported for NV12 video format. "
                           "Currently supported formats: BGR, RGB, BGRx, RGBx, BGRA only.");
}

void RendererBGR::draw_rectangle(std::vector<cv::Mat> &mats, render::Rect rect) {
    DrawRotatedRectangle(mats[0], rect.rect.tl(), rect.rect.br(), rect.rotation, rect.color, rect.thick);
}

void RendererBGR::draw_circle(std::vector<cv::Mat> &mats, render::Circle circle) {
    cv::circle(mats[0], circle.center, circle.radius, circle.color, cv::FILLED);
}

void RendererBGR::draw_text(std::vector<cv::Mat> &mats, render::Text text) {
    cv::putText(mats[0], text.text, text.org, text.fonttype, text.fontscale, text.color, text.thick);
}

void RendererBGR::draw_line(std::vector<cv::Mat> &mats, render::Line line) {
    cv::line(mats[0], line.pt1, line.pt2, line.color, line.thick);
}

void RendererBGR::draw_instance_mask(std::vector<cv::Mat> &mats, render::InstanceSegmantationMask mask) {
    cv::Mat unpadded{mask.size, CV_32F, mask.data.data()};
    cv::Mat raw_cls_mask;
    cv::copyMakeBorder(unpadded, raw_cls_mask, 1, 1, 1, 1, cv::BORDER_CONSTANT, {0});
    cv::Rect extended_box = expand_box(mask.box, float(raw_cls_mask.cols) / (raw_cls_mask.cols - 2),
                                       float(raw_cls_mask.rows) / (raw_cls_mask.rows - 2));

    int w = std::max(extended_box.width + 1, 1);
    int h = std::max(extended_box.height + 1, 1);
    int x0 = clamp(extended_box.x, 0, mats[0].cols);
    int y0 = clamp(extended_box.y, 0, mats[0].rows);
    int x1 = clamp(extended_box.x + extended_box.width + 1, 0, mats[0].cols);
    int y1 = clamp(extended_box.y + extended_box.height + 1, 0, mats[0].rows);

    cv::Mat resized;
    cv::resize(raw_cls_mask, resized, {w, h});

    cv::Mat binaryMask;
    cv::threshold(resized({cv::Point(x0 - extended_box.x, y0 - extended_box.y),
                           cv::Point(x1 - extended_box.x, y1 - extended_box.y)}),
                  binaryMask, 0.5f, 1.0f, cv::THRESH_BINARY);
    binaryMask.convertTo(binaryMask, mats[0].type());

    cv::Rect roi(x0, y0, x1 - x0, y1 - y0);
    cv::Mat colorMask(roi.size(), mats[0].type(), mask.color);

    cv::Mat roiSrc = mats[0](roi);
    cv::Mat dst;
    float alpha = 0.5f;
    cv::addWeighted(colorMask, alpha, roiSrc, 1.0 - alpha, 0.0, dst);

    dst.copyTo(roiSrc, binaryMask);
}

cv::Mat convertClassIndicesToBGR(const cv::Mat &classMap, const std::vector<cv::Vec3b> &colorPalette) {
    CV_Assert(classMap.channels() == 1);
    cv::Mat colorMap(classMap.size(), CV_8UC3);
    for (int i = 0; i < classMap.rows; ++i) {
        const double *classRowPtr = classMap.ptr<double>(i);
        cv::Vec3b *colorRowPtr = colorMap.ptr<cv::Vec3b>(i);
        for (int j = 0; j < classMap.cols; ++j) {
            int64_t classIdx = static_cast<int64_t>(classRowPtr[j]);
            colorRowPtr[j] = colorPalette[classIdx];
        }
    }
    return colorMap;
}

void RendererBGR::draw_semantic_mask(std::vector<cv::Mat> &mats, render::SemanticSegmantationMask mask) {
    cv::Mat class_mask{mask.size, CV_64FC1, mask.data.data()};

    cv::Rect2i roi(cv::Point2i(cvRound(mask.box.x), cvRound(mask.box.y)),
                   cv::Size2i(cvRound(mask.box.width), cvRound(mask.box.height)));

    cv::Mat resized;
    cv::resize(class_mask, resized, {roi.width, roi.height}, 0, 0, cv::INTER_NEAREST);

    cv::Mat colorMap = convertClassIndicesToBGR(resized, PascalVoc21ClColorPalette);
    colorMap.convertTo(colorMap, mats[0].type());
    if (mats[0].channels() == 4) {
        cv::cvtColor(colorMap, colorMap, cv::COLOR_BGR2BGRA);
    }

    cv::Mat roiSrc = mats[0](roi);
    cv::Mat dst;
    float alpha = 0.5f;
    cv::addWeighted(colorMap, alpha, roiSrc, 1.0 - alpha, 0.0, dst);

    dst.copyTo(roiSrc);
}
