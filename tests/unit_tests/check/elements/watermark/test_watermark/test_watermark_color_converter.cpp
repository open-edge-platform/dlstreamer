/*******************************************************************************
 * Copyright (C) 2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#include "color_converter.h"

#include <gtest/gtest.h>
#include <iostream>

namespace {

constexpr double BT601_KR = 0.299, BT601_KB = 0.114;
constexpr double BT709_KR = 0.2126, BT709_KB = 0.0722;

const Color WHITE(255, 255, 255);
const Color BLACK(0, 0, 0);
const Color RED(255, 0, 0);
const Color BLUE(0, 0, 255);

void expect_yuv(const Color &actual, int y, int u, int v) {
    EXPECT_EQ(actual[0], y) << "Y";
    EXPECT_EQ(actual[1], u) << "U";
    EXPECT_EQ(actual[2], v) << "V";
}

std::shared_ptr<ColorConverter> make_yuv_converter(double Kr, double Kb, bool full_range) {
    return create_color_converter(dlstreamer::ImageFormat::NV12, {WHITE, BLACK, RED}, Kr, Kb, full_range);
}

} // namespace

TEST(WatermarkColorConverter, full_range_bt601) {
    auto converter = make_yuv_converter(BT601_KR, BT601_KB, true);
    expect_yuv(converter->convert(WHITE), 255, 128, 128);
    expect_yuv(converter->convert(BLACK), 0, 128, 128);
    expect_yuv(converter->convert(RED), 76, 85, 255);
    expect_yuv(converter->convert(BLUE), 29, 255, 107);
}

TEST(WatermarkColorConverter, limited_range_bt601) {
    auto converter = make_yuv_converter(BT601_KR, BT601_KB, false);
    expect_yuv(converter->convert(WHITE), 235, 128, 128);
    expect_yuv(converter->convert(BLACK), 16, 128, 128);
    expect_yuv(converter->convert(RED), 81, 90, 240);
    expect_yuv(converter->convert(BLUE), 41, 240, 110);
}

TEST(WatermarkColorConverter, limited_range_bt709) {
    auto converter = make_yuv_converter(BT709_KR, BT709_KB, false);
    expect_yuv(converter->convert(WHITE), 235, 128, 128);
    expect_yuv(converter->convert(BLACK), 16, 128, 128);
    expect_yuv(converter->convert(RED), 63, 102, 240);
    expect_yuv(converter->convert(BLUE), 32, 240, 118);
}

TEST(WatermarkColorConverter, kr_kb_not_swapped) {
    // With swapped coefficients red would get blue's luma and vice versa
    auto converter = make_yuv_converter(BT601_KR, BT601_KB, true);
    EXPECT_GT(converter->convert(RED)[0], converter->convert(BLUE)[0]);
}

TEST(WatermarkColorConverter, gray_colors_have_neutral_chroma) {
    // Floating point error must not tint gray colors, e.g. white must not become V=127
    constexpr double coefficients[][2] = {{BT601_KR, BT601_KB}, {BT709_KR, BT709_KB}, {0.2627, 0.0593}};
    for (const auto &k : coefficients) {
        for (bool full_range : {true, false}) {
            auto converter = make_yuv_converter(k[0], k[1], full_range);
            for (int i = 0; i <= 255; i++) {
                Color yuv = converter->convert(Color(i, i, i));
                EXPECT_EQ(yuv[1], 128) << "Kr=" << k[0] << " full_range=" << full_range << " gray=" << i;
                EXPECT_EQ(yuv[2], 128) << "Kr=" << k[0] << " full_range=" << full_range << " gray=" << i;
            }
            EXPECT_EQ(converter->convert(WHITE)[0], full_range ? 255 : 235);
        }
    }
}

TEST(WatermarkColorConverter, cached_and_uncached_colors_match) {
    auto cached = make_yuv_converter(BT709_KR, BT709_KB, false);
    auto uncached = create_color_converter(dlstreamer::ImageFormat::I420, {}, BT709_KR, BT709_KB, false);
    for (const Color &c : {WHITE, BLACK, RED}) {
        EXPECT_EQ(cached->convert(c), uncached->convert(c));
    }
}

TEST(WatermarkColorConverter, rgb_formats_ignore_range) {
    auto bgr = create_color_converter(dlstreamer::ImageFormat::BGR, {RED}, BT601_KR, BT601_KB, false);
    EXPECT_EQ(bgr->convert(RED), Color(0, 0, 255));
    auto rgb = create_color_converter(dlstreamer::ImageFormat::RGB, {RED}, BT601_KR, BT601_KB, false);
    EXPECT_EQ(rgb->convert(RED), RED);
}

int main(int argc, char *argv[]) {
    std::cout << "Running Elements::Watermark::ColorConverter from " << __FILE__ << std::endl;
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
