/*******************************************************************************
 * Copyright (C) 2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

// OpenCV preprocessing with a model-proc resize: the resized ROI must fill the model input. For many ROI sizes
// w * (target / w) is 111.99... rather than 112, and truncating it left a row and a column of the input unfilled.

#include "inference_backend/pre_proc.h"

#include <gtest/gtest.h>
#include <opencv2/opencv.hpp>

#include <memory>
#include <ostream>
#include <string>
#include <tuple>
#include <vector>

using namespace InferenceBackend;

namespace {

constexpr int frame_size = 320;
constexpr int roi_x = 13;
constexpr int roi_y = 7;
constexpr int target_size = 112;

struct RoiSize {
    int width;
    int height;
};

std::ostream &operator<<(std::ostream &os, const RoiSize &r) {
    return os << r.width << "x" << r.height;
}

// Up to 222x107, each side s gives s * (112.0 / s) just below 112 in double; the rest scale exactly
const std::vector<RoiSize> roi_sizes = {{55, 55}, {71, 71},   {83, 83}, {95, 95}, {107, 107}, {159, 159}, {194, 194},
                                        {55, 83}, {222, 107}, {56, 56}, {64, 64}, {100, 100}, {112, 112}, {224, 224}};

const std::vector<RoiSize> square_roi_sizes = {{55, 55},   {71, 71}, {83, 83},   {107, 107},
                                               {194, 194}, {64, 64}, {112, 112}, {224, 224}};

std::string RoiName(const RoiSize &roi) {
    return "Roi" + std::to_string(roi.width) + "x" + std::to_string(roi.height);
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

Image WrapRoi(const cv::Mat &frame, const RoiSize &roi) {
    Image src;
    src.type = MemoryType::SYSTEM;
    src.format = FOURCC_BGR;
    src.width = frame.cols;
    src.height = frame.rows;
    src.planes[0] = frame.data;
    src.stride[0] = static_cast<uint32_t>(frame.step);
    // Qualified: on Windows wingdi.h also declares a Rectangle() function
    src.rect = InferenceBackend::Rectangle<uint32_t>(roi_x, roi_y, roi.width, roi.height);
    return src;
}

InputImageLayerDesc::Ptr CreateResizeDesc(InputImageLayerDesc::Resize resize) {
    return std::make_shared<InputImageLayerDesc>(
        resize, InputImageLayerDesc::Crop::NO, InputImageLayerDesc::ColorSpace::NO,
        InputImageLayerDesc::RangeNormalization(), InputImageLayerDesc::DistribNormalization());
}

cv::Mat GradientFrame() {
    cv::Mat frame(frame_size, frame_size, CV_8UC3);
    for (int y = 0; y < frame.rows; y++)
        for (int x = 0; x < frame.cols; x++)
            frame.at<cv::Vec3b>(y, x) = cv::Vec3b(static_cast<uint8_t>(x * 255 / (frame.cols - 1)),
                                                  static_cast<uint8_t>(y * 255 / (frame.rows - 1)), 128);
    return frame;
}

} // namespace

class OpenCVResizeRoundingTest : public testing::TestWithParam<RoiSize> {
  protected:
    std::unique_ptr<ImagePreprocessor> preprocessor{ImagePreprocessor::Create(ImagePreprocessorType::OPENCV, "")};
};

TEST_P(OpenCVResizeRoundingTest, NoAspectRatioMatchesFullResize) {
    const auto &roi = GetParam();
    const cv::Mat frame = GradientFrame();

    PlanarOutput dst(target_size, target_size);
    auto transform = std::make_shared<ImageTransformationParams>();
    ASSERT_NO_THROW(preprocessor->Convert(WrapRoi(frame, roi), dst.image,
                                          CreateResizeDesc(InputImageLayerDesc::Resize::NO_ASPECT_RATIO), transform));

    cv::Mat reference;
    cv::resize(frame(cv::Rect(roi_x, roi_y, roi.width, roi.height)), reference, cv::Size(target_size, target_size));
    cv::Mat diff;
    cv::absdiff(dst.ToMat(), reference, diff);
    double max_diff = 0;
    cv::minMaxLoc(diff.reshape(1), nullptr, &max_diff);
    EXPECT_LE(max_diff, 1);

    EXPECT_DOUBLE_EQ(transform->resize_scale_x, static_cast<double>(target_size) / roi.width);
    EXPECT_DOUBLE_EQ(transform->resize_scale_y, static_cast<double>(target_size) / roi.height);
}

INSTANTIATE_TEST_SUITE_P(ResizeRounding, OpenCVResizeRoundingTest, testing::ValuesIn(roi_sizes),
                         [](const testing::TestParamInfo<RoiSize> &info) { return RoiName(info.param); });

// With a square ROI and a square input, the aspect-ratio resizes must fill the input too: no padding
class OpenCVAspectRatioRoundingTest : public testing::TestWithParam<std::tuple<RoiSize, InputImageLayerDesc::Resize>> {
  protected:
    std::unique_ptr<ImagePreprocessor> preprocessor{ImagePreprocessor::Create(ImagePreprocessorType::OPENCV, "")};
};

TEST_P(OpenCVAspectRatioRoundingTest, SquareRoiFillsSquareInput) {
    const auto &roi = std::get<0>(GetParam());
    const auto resize = std::get<1>(GetParam());

    constexpr uint8_t value = 200;
    const cv::Mat frame(frame_size, frame_size, CV_8UC3, cv::Scalar::all(value));
    PlanarOutput dst(target_size, target_size);
    ASSERT_NO_THROW(preprocessor->Convert(WrapRoi(frame, roi), dst.image, CreateResizeDesc(resize), nullptr));

    double min_value = 0;
    cv::minMaxLoc(dst.ToMat().reshape(1), &min_value, nullptr);
    EXPECT_EQ(min_value, value);
}

INSTANTIATE_TEST_SUITE_P(ResizeRounding, OpenCVAspectRatioRoundingTest,
                         testing::Combine(testing::ValuesIn(square_roi_sizes),
                                          testing::Values(InputImageLayerDesc::Resize::ASPECT_RATIO,
                                                          InputImageLayerDesc::Resize::ASPECT_RATIO_PAD)),
                         [](const testing::TestParamInfo<std::tuple<RoiSize, InputImageLayerDesc::Resize>> &info) {
                             return RoiName(std::get<0>(info.param)) +
                                    (std::get<1>(info.param) == InputImageLayerDesc::Resize::ASPECT_RATIO
                                         ? "_AspectRatio"
                                         : "_AspectRatioPad");
                         });
