/*******************************************************************************
 * Copyright (C) 2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#include "frame_utils.hpp"

#include <opencv2/opencv.hpp>

#include <array>
#include <cassert>
#include <cstring>
#include <stdexcept>
#include <vector>

namespace genai {

ov::Tensor gst_buffer_to_rgb_tensor(dlstreamer::MemoryMapperGSTToCPU &mapper, GstBuffer *buffer, GstVideoInfo *info) {
    // Create a GSTFrame and map to CPU memory
    auto gst_frame = std::make_shared<dlstreamer::GSTFrame>(buffer, info);
    auto mapped_frame = mapper.map(gst_frame, dlstreamer::AccessMode::Read);

    // Convert to Mat, code from gvawatermark
    static constexpr std::array<int, 4> channels_to_cvtype_map = {CV_8UC1, CV_8UC2, CV_8UC3, CV_8UC4};
    std::vector<cv::Mat> image_planes;
    image_planes.reserve(mapped_frame->num_tensors());

    // Go through planes and create cv::Mat for every plane
    for (auto &tensor : *mapped_frame) {
        // Verify number of channels
        dlstreamer::ImageInfo image_info(tensor->info());
        assert(image_info.channels() > 0 && image_info.channels() <= channels_to_cvtype_map.size());
        const int cv_type = channels_to_cvtype_map[image_info.channels() - 1];
        image_planes.emplace_back(image_info.height(), image_info.width(), cv_type, tensor->data(),
                                  image_info.width_stride());
    }

    auto check_planes = [&image_planes](size_t n) {
        if (image_planes.size() != n)
            throw std::runtime_error("Image format error, plane count != " + std::to_string(n));
    };

    // Convert Mat to RGB format
    cv::Mat frame;
    switch (GST_VIDEO_INFO_FORMAT(info)) {
    case GST_VIDEO_FORMAT_RGB:
        check_planes(1);
        frame = image_planes[0];
        break;
    case GST_VIDEO_FORMAT_RGBA:
    case GST_VIDEO_FORMAT_RGBx:
        check_planes(1);
        cv::cvtColor(image_planes[0], frame, cv::COLOR_RGBA2RGB);
        break;
    case GST_VIDEO_FORMAT_BGR:
        check_planes(1);
        cv::cvtColor(image_planes[0], frame, cv::COLOR_BGR2RGB);
        break;
    case GST_VIDEO_FORMAT_BGRA:
    case GST_VIDEO_FORMAT_BGRx:
        check_planes(1);
        cv::cvtColor(image_planes[0], frame, cv::COLOR_BGRA2RGB);
        break;
    case GST_VIDEO_FORMAT_NV12: {
        check_planes(2);
        cv::cvtColorTwoPlane(image_planes[0], image_planes[1], frame, cv::COLOR_YUV2RGB_NV12);
        break;
    }
    case GST_VIDEO_FORMAT_I420: {
        check_planes(3);
        // For I420, need to create a single Mat with the layout Y+U+V
        uint8_t *y_data = image_planes[0].data;
        uint8_t *u_data = image_planes[1].data;
        uint8_t *v_data = image_planes[2].data;
        int y_size = image_planes[0].rows * image_planes[0].step;
        int u_size = image_planes[1].rows * image_planes[1].step;

        // Check if planes are contiguous
        if (u_data == y_data + y_size && v_data == u_data + u_size) {
            // Planes are contiguous (typical)
            cv::Mat yuv(info->height * 3 / 2, info->width, CV_8UC1, y_data);
            cv::cvtColor(yuv, frame, cv::COLOR_YUV2RGB_I420);
        } else {
            // Planes are not contiguous, need to copy (fallback)
            cv::Mat yuv(info->height * 3 / 2, info->width, CV_8UC1);
            image_planes[0].copyTo(yuv.rowRange(0, info->height));
            image_planes[1].copyTo(yuv.rowRange(info->height, info->height + info->height / 4));
            image_planes[2].copyTo(yuv.rowRange(info->height + info->height / 4, info->height * 3 / 2));
            cv::cvtColor(yuv, frame, cv::COLOR_YUV2RGB_I420);
        }
        break;
    }
    default:
        throw std::runtime_error("Unsupported video format: " + std::to_string(GST_VIDEO_INFO_FORMAT(info)));
    }

    // Create an owning tensor and copy the RGB pixels into it
    auto tensor = ov::Tensor(ov::element::u8, {1, static_cast<size_t>(frame.rows), static_cast<size_t>(frame.cols),
                                               static_cast<size_t>(frame.channels())});
    size_t expected_size = frame.total() * frame.elemSize();
    if (tensor.get_byte_size() != expected_size) {
        throw std::runtime_error("Tensor size mismatch: expected " + std::to_string(expected_size) + ", got " +
                                 std::to_string(tensor.get_byte_size()));
    }
    memcpy(tensor.data(), frame.data, expected_size);

    return tensor;
}

} // namespace genai
