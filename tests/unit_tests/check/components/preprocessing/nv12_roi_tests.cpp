/*******************************************************************************
 * Copyright (C) 2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

// OpenCV preprocessing of NV12 ROIs, including ROIs that are not chroma-aligned.

#include "inference_backend/pre_proc.h"

#include <gtest/gtest.h>
#include <opencv2/opencv.hpp>

#include <memory>
#include <ostream>
#include <sstream>
#include <vector>

using namespace InferenceBackend;

namespace {

constexpr int FRAME_WIDTH = 64;
constexpr int FRAME_HEIGHT = 48;
constexpr int FRAME_STRIDE = 80; // wider than the frame, as with VA surfaces

struct Nv12Roi {
    int x;
    int y;
    int width;
    int height;
};

std::ostream &operator<<(std::ostream &os, const Nv12Roi &r) {
    return os << r.x << "_" << r.y << "_" << r.width << "x" << r.height;
}

struct Nv12Frame {
    std::vector<uint8_t> data = std::vector<uint8_t>(FRAME_STRIDE * FRAME_HEIGHT * 3 / 2);

    Nv12Frame() {
        cv::RNG rng(1234);
        rng.fill(data, cv::RNG::UNIFORM, 0, 256);
    }

    uint8_t *YPlane() {
        return data.data();
    }
    uint8_t *UvPlane() {
        return data.data() + FRAME_STRIDE * FRAME_HEIGHT;
    }

    Image Wrap(const Nv12Roi &roi) {
        Image img;
        img.type = MemoryType::SYSTEM;
        img.format = FOURCC_NV12;
        img.width = FRAME_WIDTH;
        img.height = FRAME_HEIGHT;
        img.planes[0] = YPlane();
        img.planes[1] = UvPlane();
        img.stride[0] = img.stride[1] = FRAME_STRIDE;
        img.rect = InferenceBackend::Rectangle<uint32_t>(roi.x, roi.y, roi.width, roi.height);
        return img;
    }

    // Reference: convert the complete frame to BGR first, crop the ROI afterwards
    cv::Mat ReferenceRoi(const Nv12Roi &roi) {
        cv::Mat packed(FRAME_HEIGHT * 3 / 2, FRAME_WIDTH, CV_8UC1);
        cv::Mat(FRAME_HEIGHT, FRAME_WIDTH, CV_8UC1, YPlane(), FRAME_STRIDE).copyTo(packed.rowRange(0, FRAME_HEIGHT));
        cv::Mat(FRAME_HEIGHT / 2, FRAME_WIDTH, CV_8UC1, UvPlane(), FRAME_STRIDE)
            .copyTo(packed.rowRange(FRAME_HEIGHT, FRAME_HEIGHT * 3 / 2));
        cv::Mat bgr;
        cv::cvtColor(packed, bgr, cv::COLOR_YUV2BGR_NV12);
        return bgr(cv::Rect(roi.x, roi.y, roi.width, roi.height)).clone();
    }
};

// Runs the OpenCV preprocessor on the ROI and returns the planar output merged back into BGR
cv::Mat ConvertRoi(Nv12Frame &frame, const Nv12Roi &roi) {
    std::unique_ptr<ImagePreprocessor> preprocessor{ImagePreprocessor::Create(ImagePreprocessorType::OPENCV, "")};

    std::vector<uint8_t> data(3 * roi.width * roi.height);
    Image dst;
    dst.type = MemoryType::SYSTEM;
    dst.format = FOURCC_RGBP;
    dst.width = roi.width;
    dst.height = roi.height;
    for (int i = 0; i < 3; i++) {
        dst.planes[i] = data.data() + i * roi.width * roi.height;
        dst.stride[i] = roi.width;
    }

    preprocessor->Convert(frame.Wrap(roi), dst);

    std::vector<cv::Mat> channels;
    for (int i = 0; i < 3; i++)
        channels.emplace_back(roi.height, roi.width, CV_8UC1, dst.planes[i]);
    cv::Mat merged;
    cv::merge(channels, merged);
    return merged;
}

} // namespace

class Nv12RoiTest : public testing::TestWithParam<Nv12Roi> {};

TEST_P(Nv12RoiTest, MatchesFullFrameConversionThenCrop) {
    const auto roi = GetParam();
    Nv12Frame frame;

    cv::Mat result;
    ASSERT_NO_THROW(result = ConvertRoi(frame, roi));

    const cv::Mat reference = frame.ReferenceRoi(roi);
    ASSERT_EQ(result.size(), reference.size());
    cv::Mat diff;
    cv::absdiff(result, reference, diff);
    double max_diff = 0;
    cv::minMaxLoc(diff.reshape(1), nullptr, &max_diff);
    EXPECT_LE(max_diff, 1);
}

INSTANTIATE_TEST_SUITE_P(Nv12Roi, Nv12RoiTest,
                         testing::Values(
                             // even origin, even size (chroma-aligned)
                             Nv12Roi{0, 0, 16, 16}, Nv12Roi{4, 6, 16, 12},
                             // odd origin, even size
                             Nv12Roi{1, 0, 16, 16}, Nv12Roi{0, 1, 16, 16}, Nv12Roi{3, 5, 16, 12},
                             // even origin, odd size
                             Nv12Roi{2, 4, 15, 17}, Nv12Roi{0, 0, 9, 8}, Nv12Roi{4, 6, 8, 9},
                             // odd origin, odd size
                             Nv12Roi{1, 1, 15, 15}, Nv12Roi{7, 9, 9, 7},
                             // touching the frame's right and bottom edges
                             Nv12Roi{47, 31, 17, 17}, Nv12Roi{46, 30, 18, 18}, Nv12Roi{63, 47, 1, 1}),
                         [](const testing::TestParamInfo<Nv12Roi> &info) {
                             std::ostringstream os;
                             os << info.param;
                             return os.str();
                         });
