/*******************************************************************************
 * Copyright (C) 2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#include "color_converter.h"

#include <gtest/gtest.h>
#include <iostream>

namespace {

const Color WHITE(255, 255, 255);
const Color BLACK(0, 0, 0);
const Color RED(255, 0, 0);
const Color BLUE(0, 0, 255);

std::string describe(const Color &c) {
    return std::to_string(c[0]) + "," + std::to_string(c[1]) + "," + std::to_string(c[2]);
}

} // namespace

TEST(WatermarkContrastTextColor, black_text_on_light_colors) {
    for (const Color &background :
         {WHITE, Color(255, 255, 0), Color(0, 255, 255), Color(0, 255, 0), Color(255, 170, 0)})
        EXPECT_EQ(contrast_text_color(background), BLACK) << describe(background);
}

TEST(WatermarkContrastTextColor, white_text_on_dark_and_saturated_colors) {
    // WCAG 2 contrast would pick black text on red, magenta and orange-red
    for (const Color &background : {BLACK, RED, BLUE, Color(255, 0, 255), Color(255, 85, 0)})
        EXPECT_EQ(contrast_text_color(background), WHITE) << describe(background);
}

TEST(WatermarkContrastTextColor, gray_crossover) {
    // Black and white text have equal APCA contrast between these two grays
    EXPECT_EQ(contrast_text_color(Color(163, 163, 163)), WHITE);
    EXPECT_EQ(contrast_text_color(Color(164, 164, 164)), BLACK);
}

int main(int argc, char *argv[]) {
    std::cout << "Running Elements::Watermark::ColorConverter from " << __FILE__ << std::endl;
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
