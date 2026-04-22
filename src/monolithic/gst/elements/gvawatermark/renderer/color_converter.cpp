/*******************************************************************************
 * Copyright (C) 2021-2022 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#include "color_converter.h"

Color ColorConverter::convert(Color input_color) {
    return color_table[input_color];
}

Color SaveOriginalColorConverter::convert(Color input_color) {
    return input_color;
}

RGBtoBGRColorConverter::RGBtoBGRColorConverter(const std::vector<Color> &rgb_colors) {
    convert_colors(rgb_colors);
}

void RGBtoBGRColorConverter::convert_colors(const std::vector<Color> &rgb_colors) {
    for (const Color &rgb_c : rgb_colors) {
        color_table[rgb_c] = Color(rgb_c[2], rgb_c[1], rgb_c[0]);
    }
}

Color RGBtoBGRColorConverter::convert(Color input_color) {
    auto it = color_table.find(input_color);
    if (it != color_table.end()) {
        return it->second;
    }
    return Color(input_color[2], input_color[1], input_color[0]);
}

RGBtoYUVColorConverter::RGBtoYUVColorConverter(const std::vector<Color> &rgb_colors, double Kb, double Kr) {
    convert_colors_rgb_to_yuv(Kr, Kb, rgb_colors, color_table, coefficient_matrix);
}

Color RGBtoYUVColorConverter::convert(Color input_color) {
    auto it = color_table.find(input_color);
    if (it != color_table.end()) {
        return it->second;
    }
    return convert_color_rgb_to_yuv(input_color, coefficient_matrix);
}

void RGBtoYUVColorConverter::fill_color_conversion_matrix(double Kr, double Kb, double (&matrix)[3][3]) {
    double Kg = 1.0 - Kr - Kb;
    double k1 = 1 - Kb;
    double k2 = 1 - Kr;

    matrix[0][0] = Kr;
    matrix[0][1] = Kg;
    matrix[0][2] = Kb;

    matrix[1][0] = -Kr / (2 * k1);
    matrix[1][1] = -Kg / (2 * k1);
    matrix[1][2] = 1. / 2;

    matrix[2][0] = 1. / 2;
    matrix[2][1] = -Kg / (2 * k2);
    matrix[2][2] = -Kb / (2 * k2);
}

Color RGBtoYUVColorConverter::convert_color_rgb_to_yuv(const Color &c, double (&matrix)[3][3]) {
    Color x;
    x[0] = clamp(matrix[0][0] * c[0] + matrix[0][1] * c[1] + matrix[0][2] * c[2], 0., 255.);
    x[1] = clamp(matrix[1][0] * c[0] + matrix[1][1] * c[1] + matrix[1][2] * c[2] + 128., 0., 255.);
    x[2] = clamp(matrix[2][0] * c[0] + matrix[2][1] * c[1] + matrix[2][2] * c[2] + 128., 0., 255.);
    return x;
}

void RGBtoYUVColorConverter::convert_colors_rgb_to_yuv(double Kr, double Kb, const std::vector<Color> &rgb_colors,
                                                       std::unordered_map<Color, Color> &yuv_colors,
                                                       double (&matrix)[3][3]) {
    fill_color_conversion_matrix(Kr, Kb, matrix);
    for (const Color &rgb_c : rgb_colors) {
        yuv_colors[rgb_c] = convert_color_rgb_to_yuv(rgb_c, matrix);
    }
}

std::shared_ptr<ColorConverter> create_color_converter(dlstreamer::ImageFormat format,
                                                       const std::vector<Color> &rgb_color_table, double Kr,
                                                       double Kb) {
    switch (format) {
    case dlstreamer::ImageFormat::BGR:
    case dlstreamer::ImageFormat::BGRX:
    case dlstreamer::ImageFormat::BGRP:
        return std::make_shared<RGBtoBGRColorConverter>(RGBtoBGRColorConverter(rgb_color_table));
    case dlstreamer::ImageFormat::RGB:
    case dlstreamer::ImageFormat::RGBX:
    case dlstreamer::ImageFormat::RGBP:
        return std::make_shared<SaveOriginalColorConverter>(SaveOriginalColorConverter());
    case dlstreamer::ImageFormat::NV12:
    case dlstreamer::ImageFormat::I420:
        return std::make_shared<RGBtoYUVColorConverter>(RGBtoYUVColorConverter(rgb_color_table, Kr, Kb));
    }

    throw std::runtime_error("Unsupported format");
}
