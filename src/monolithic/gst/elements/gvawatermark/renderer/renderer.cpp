/*******************************************************************************
 * Copyright (C) 2022-2025 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#include "renderer.h"

#include "cpu/renderer_cpu.h"

std::vector<cv::Mat> Renderer::convertBufferToCvMats(dlstreamer::Frame &buffer) {
    static std::array<int, 4> channels_to_cvtype_map = {CV_8UC1, CV_8UC2, CV_8UC3, CV_8UC4};

    assert(buffer.media_type() == dlstreamer::MediaType::Image);
    assert(buffer.num_tensors() > 0);

    std::vector<cv::Mat> image_planes;
    image_planes.reserve(buffer.num_tensors());

    // Check for supported formats
    switch (static_cast<dlstreamer::ImageFormat>(buffer.format())) {
    case dlstreamer::ImageFormat::BGRX:
    case dlstreamer::ImageFormat::BGR:
    case dlstreamer::ImageFormat::RGBX:
    case dlstreamer::ImageFormat::RGB:
    case dlstreamer::ImageFormat::I420:
    case dlstreamer::ImageFormat::NV12:
        break;
    default:
        throw std::runtime_error("Unsupported image format");
    }

    // Go through planes and create cv::Mat for every plane
    for (auto &tensor : buffer) {
        // Verify number of channels
        dlstreamer::ImageInfo image_info(tensor->info());
        assert(image_info.channels() > 0 && image_info.channels() <= channels_to_cvtype_map.size());
        const int cv_type = channels_to_cvtype_map[image_info.channels() - 1];

        image_planes.emplace_back(image_info.height(), image_info.width(), cv_type, tensor->data(),
                                  image_info.width_stride());
    }

    return image_planes;
}

void Renderer::convert_prims_color(std::vector<render::Prim> &prims) {
    for (auto &p : prims) {
        if (std::holds_alternative<render::Line>(p)) {
            render::Line line = std::get<render::Line>(p);
            line.color = _color_converter->convert(line.color);
            p = line;
        } else if (std::holds_alternative<render::Rect>(p)) {
            render::Rect rect = std::get<render::Rect>(p);
            rect.color = _color_converter->convert(rect.color);
            p = rect;
        } else if (std::holds_alternative<render::Text>(p)) {
            render::Text text = std::get<render::Text>(p);
            text.color = _color_converter->convert(text.color);
            p = text;
        } else if (std::holds_alternative<render::Circle>(p)) {
            render::Circle circle = std::get<render::Circle>(p);
            circle.color = _color_converter->convert(circle.color);
            p = circle;
        }
    }
}

void Renderer::draw(dlstreamer::FramePtr buffer, std::vector<render::Prim> prims) {
    auto mapped_buf = buffer_map(buffer);
    std::vector<cv::Mat> image_planes = convertBufferToCvMats(*mapped_buf);
    convert_prims_color(prims);
    draw_backend(image_planes, prims);
}

void Renderer::draw_va(cv::Mat buffer, std::vector<render::Prim> prims) {
    std::vector<cv::Mat> image_planes = {buffer};
    convert_prims_color(prims);
    draw_backend(image_planes, prims);
}
