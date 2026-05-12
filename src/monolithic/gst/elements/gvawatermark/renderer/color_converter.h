/*******************************************************************************
 * Copyright (C) 2021-2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#pragma once

#include <dlstreamer/image_info.h>
#include <opencv2/opencv.hpp>
#include <string>
#include <unordered_map>

using Color = cv::Scalar_<uint8_t>;

namespace std {

template <>
struct hash<Color> {
    std::size_t operator()(const Color &color) const {
        auto h1 = std::hash<Color::value_type>{}(color[0]);
        auto h2 = std::hash<Color::value_type>{}(color[1]);
        auto h3 = std::hash<Color::value_type>{}(color[2]);
        return h1 ^ (h2 << 1) ^ (h3 << 2);
    }
};

} // namespace std

class ColorConverter {
  protected:
    std::unordered_map<Color, Color> color_table;
    ColorConverter() = default;

  public:
    virtual Color convert(Color input_color);
    virtual ~ColorConverter() = default;
};

class SaveOriginalColorConverter : public ColorConverter {
  public:
    Color convert(Color input_color) override;
};

class RGBtoBGRColorConverter : public ColorConverter {
  public:
    RGBtoBGRColorConverter(const std::vector<Color> &rgb_colors);
    Color convert(Color input_color) override;

  private:
    void convert_colors(const std::vector<Color> &rgb_colors);
};

class RGBtoYUVColorConverter : public ColorConverter {
  public:
    RGBtoYUVColorConverter(const std::vector<Color> &rgb_colors, double Kb, double Kr);
    Color convert(Color input_color) override;

  private:
    double coefficient_matrix[3][3];

    void fill_color_conversion_matrix(double Kr, double Kb, double (&matrix)[3][3]);

    template <typename T>
    T clamp(T val, T low, T high) {
        return (val > high) ? high : ((val < low) ? low : val);
    }

    Color convert_color_rgb_to_yuv(const Color &c, double (&matrix)[3][3]);

    void convert_colors_rgb_to_yuv(double Kr, double Kb, const std::vector<Color> &rgb_colors,
                                   std::unordered_map<Color, Color> &yuv_colors, double (&matrix)[3][3]);
};

std::shared_ptr<ColorConverter> create_color_converter(dlstreamer::ImageFormat format,
                                                       const std::vector<Color> &rgb_color_table, double Kr, double Kb);
