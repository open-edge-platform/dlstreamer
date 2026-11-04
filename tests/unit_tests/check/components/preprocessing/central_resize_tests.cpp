/*******************************************************************************
 * Copyright (C) 2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#include "inference_backend/pre_proc.h"

#include <gtest/gtest.h>
#include <opencv2/opencv.hpp>

#include <algorithm>
#include <memory>
#include <ostream>
#include <vector>

using namespace InferenceBackend;

namespace {

constexpr int frame_size = 640;
constexpr int roi_x = 37;
constexpr int roi_y = 53;

struct CentralResizeCase {
    const char *name;
    int roi_width;
    int roi_height;
    int target_width;
    int target_height;
    InputImageLayerDesc::CropRect expected_crop;
};

std::ostream &operator<<(std::ostream &os, const CentralResizeCase &c) {
    return os << c.name;
}

const std::vector<CentralResizeCase> central_resize_cases = {
    {"PortraitNarrowerThanTarget", 174, 424, 224, 224, {0, 125, 174, 174}},
    {"LandscapeLowerThanTarget", 424, 174, 224, 224, {125, 0, 174, 174}},
    {"SmallerThanTarget", 100, 150, 224, 224, {0, 25, 100, 100}},
    {"LargerThanTarget", 300, 500, 224, 224, {0, 100, 300, 300}},
    {"LandscapeTarget", 300, 300, 224, 112, {0, 75, 300, 150}},
    {"PortraitTargetSmallLandscapeRoi", 120, 80, 112, 224, {40, 0, 40, 80}},
};

InputImageLayerDesc::Ptr CreateCentralResizeDesc(InputImageLayerDesc::Resize resize) {
    return std::make_shared<InputImageLayerDesc>(
        resize, InputImageLayerDesc::Crop::CENTRAL_RESIZE, InputImageLayerDesc::ColorSpace::NO,
        InputImageLayerDesc::RangeNormalization(), InputImageLayerDesc::DistribNormalization());
}

struct PlanarOutput {
    int width;
    int height;
    std::vector<uint8_t> data;
    Image image;

    PlanarOutput(int width, int height) : width(width), height(height), data(3 * width * height, 0) {
        image.type = MemoryType::SYSTEM;
        image.format = FOURCC_RGBP;
        image.width = width;
        image.height = height;
        for (int i = 0; i < 3; i++) {
            image.planes[i] = data.data() + i * width * height;
            image.stride[i] = width;
        }
    }

    cv::Mat ToMat() const {
        std::vector<cv::Mat> channels;
        for (int i = 0; i < 3; i++)
            channels.emplace_back(height, width, CV_8UC1, const_cast<uint8_t *>(data.data()) + i * width * height);
        cv::Mat merged;
        cv::merge(channels, merged);
        return merged;
    }
};

Image WrapRoi(const cv::Mat &frame, int roi_width, int roi_height) {
    Image src;
    src.type = MemoryType::SYSTEM;
    src.format = FOURCC_BGR;
    src.width = frame.cols;
    src.height = frame.rows;
    src.planes[0] = frame.data;
    src.stride[0] = static_cast<uint32_t>(frame.step);
    src.rect = Rectangle<uint32_t>(roi_x, roi_y, roi_width, roi_height);
    return src;
}

// Reference: resize the shortest edge preserving aspect ratio, then center-crop to the target
cv::Mat ReferenceCentralResize(const cv::Mat &roi, const cv::Size &target) {
    const double scale =
        std::max(static_cast<double>(target.width) / roi.cols, static_cast<double>(target.height) / roi.rows);
    const cv::Size resized_size(std::max(target.width, cvRound(roi.cols * scale)),
                                std::max(target.height, cvRound(roi.rows * scale)));
    cv::Mat resized;
    cv::resize(roi, resized, resized_size, 0, 0, cv::INTER_CUBIC);
    const cv::Rect crop((resized_size.width - target.width) / 2, (resized_size.height - target.height) / 2,
                        target.width, target.height);
    return resized(crop).clone();
}

std::string CaseName(const testing::TestParamInfo<std::tuple<CentralResizeCase, InputImageLayerDesc::Resize>> &info) {
    static const char *resize_names[] = {"No", "NoAspectRatio", "AspectRatio", "AspectRatioPad",
                                         "AspectRatioMultipleOf"};
    return std::string(std::get<0>(info.param).name) + "_Resize" +
           resize_names[static_cast<int>(std::get<1>(info.param))];
}

} // namespace

TEST(CentralResizeCropTest, ZeroSizeThrows) {
    ASSERT_THROW(InputImageLayerDesc::CalculateCentralResizeCrop(0, 10, 10, 10), std::invalid_argument);
    ASSERT_THROW(InputImageLayerDesc::CalculateCentralResizeCrop(10, 10, 10, 0), std::invalid_argument);
}

TEST(CentralResizeCropTest, NoAspectRatioDisablesOnlyOtherCropTypes) {
    const auto desc = [](InputImageLayerDesc::Crop crop) {
        return InputImageLayerDesc(InputImageLayerDesc::Resize::NO_ASPECT_RATIO, crop,
                                   InputImageLayerDesc::ColorSpace::NO, InputImageLayerDesc::RangeNormalization(),
                                   InputImageLayerDesc::DistribNormalization());
    };

    EXPECT_TRUE(desc(InputImageLayerDesc::Crop::CENTRAL_RESIZE).doNeedCrop());
    EXPECT_FALSE(desc(InputImageLayerDesc::Crop::CENTRAL).doNeedCrop());
    EXPECT_FALSE(desc(InputImageLayerDesc::Crop::TOP_LEFT).doNeedCrop());
    EXPECT_FALSE(desc(InputImageLayerDesc::Crop::NO).doNeedCrop());
}

class CentralResizeCropGeometryTest : public testing::TestWithParam<CentralResizeCase> {};

TEST_P(CentralResizeCropGeometryTest, CropKeepsTargetAspectRatio) {
    const auto &c = GetParam();
    const auto crop =
        InputImageLayerDesc::CalculateCentralResizeCrop(c.roi_width, c.roi_height, c.target_width, c.target_height);

    EXPECT_EQ(crop.x, c.expected_crop.x);
    EXPECT_EQ(crop.y, c.expected_crop.y);
    EXPECT_EQ(crop.width, c.expected_crop.width);
    EXPECT_EQ(crop.height, c.expected_crop.height);
    EXPECT_LE(crop.x + crop.width, static_cast<size_t>(c.roi_width));
    EXPECT_LE(crop.y + crop.height, static_cast<size_t>(c.roi_height));
}

INSTANTIATE_TEST_SUITE_P(CentralResize, CentralResizeCropGeometryTest, testing::ValuesIn(central_resize_cases),
                         [](const testing::TestParamInfo<CentralResizeCase> &info) { return info.param.name; });

class OpenCVCentralResizeTest
    : public testing::TestWithParam<std::tuple<CentralResizeCase, InputImageLayerDesc::Resize>> {
  protected:
    std::unique_ptr<ImagePreprocessor> preprocessor{ImagePreprocessor::Create(ImagePreprocessorType::OPENCV, "")};
};

TEST_P(OpenCVCentralResizeTest, FillsTargetWithCenteredCrop) {
    const auto &c = std::get<0>(GetParam());
    const auto resize = std::get<1>(GetParam());

    constexpr uint8_t background = 50;
    constexpr uint8_t crop_value = 200;
    cv::Mat frame(frame_size, frame_size, CV_8UC3, cv::Scalar::all(background));
    frame(cv::Rect(roi_x + static_cast<int>(c.expected_crop.x), roi_y + static_cast<int>(c.expected_crop.y),
                   static_cast<int>(c.expected_crop.width), static_cast<int>(c.expected_crop.height)))
        .setTo(cv::Scalar::all(crop_value));

    PlanarOutput dst(c.target_width, c.target_height);
    auto transform = std::make_shared<ImageTransformationParams>();
    ASSERT_NO_THROW(preprocessor->Convert(WrapRoi(frame, c.roi_width, c.roi_height), dst.image,
                                          CreateCentralResizeDesc(resize), transform));

    // Only the centered crop may reach the output: no padding and nothing from outside the crop
    double min_value = 0;
    double max_value = 0;
    cv::minMaxLoc(dst.ToMat().reshape(1), &min_value, &max_value);
    EXPECT_GE(min_value, crop_value - 1);
    EXPECT_LE(max_value, crop_value + 1);

    const double scale_x = static_cast<double>(c.target_width) / c.expected_crop.width;
    const double scale_y = static_cast<double>(c.target_height) / c.expected_crop.height;
    EXPECT_DOUBLE_EQ(transform->resize_scale_x, scale_x);
    EXPECT_DOUBLE_EQ(transform->resize_scale_y, scale_y);
    EXPECT_EQ(transform->padding_size_x, 0u);
    EXPECT_EQ(transform->padding_size_y, 0u);
    EXPECT_EQ(transform->croped_border_size_x, static_cast<size_t>(c.expected_crop.x * scale_x + 0.5));
    EXPECT_EQ(transform->croped_border_size_y, static_cast<size_t>(c.expected_crop.y * scale_y + 0.5));
}

TEST_P(OpenCVCentralResizeTest, MatchesShortestEdgeResizeThenCenterCrop) {
    const auto &c = std::get<0>(GetParam());
    const auto resize = std::get<1>(GetParam());

    cv::Mat frame(frame_size, frame_size, CV_8UC3);
    for (int y = 0; y < frame.rows; y++)
        for (int x = 0; x < frame.cols; x++)
            frame.at<cv::Vec3b>(y, x) = cv::Vec3b(static_cast<uint8_t>(x * 255 / (frame.cols - 1)),
                                                  static_cast<uint8_t>(y * 255 / (frame.rows - 1)), 128);

    PlanarOutput dst(c.target_width, c.target_height);
    ASSERT_NO_THROW(preprocessor->Convert(WrapRoi(frame, c.roi_width, c.roi_height), dst.image,
                                          CreateCentralResizeDesc(resize), nullptr));

    const cv::Mat reference = ReferenceCentralResize(frame(cv::Rect(roi_x, roi_y, c.roi_width, c.roi_height)),
                                                     cv::Size(c.target_width, c.target_height));
    cv::Mat diff;
    cv::absdiff(dst.ToMat(), reference, diff);
    double max_diff = 0;
    cv::minMaxLoc(diff.reshape(1), nullptr, &max_diff);
    EXPECT_LE(max_diff, 2);
}

INSTANTIATE_TEST_SUITE_P(CentralResize, OpenCVCentralResizeTest,
                         testing::Combine(testing::ValuesIn(central_resize_cases),
                                          testing::Values(InputImageLayerDesc::Resize::NO,
                                                          InputImageLayerDesc::Resize::NO_ASPECT_RATIO,
                                                          InputImageLayerDesc::Resize::ASPECT_RATIO,
                                                          InputImageLayerDesc::Resize::ASPECT_RATIO_PAD)),
                         CaseName);
