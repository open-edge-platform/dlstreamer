/*******************************************************************************
 * Copyright (C) 2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

// The OpenCL pre-processing backend (pre-process-backend=opencl): the regions it computes from model-proc parameters,
// and, on a machine with an Intel GPU, the model input its kernels write for system memory frames.

#include "opencl_context.h"
#include "opencl_preproc_geometry.h"
#include "opencl_preprocessor.h"

#include <gtest/gtest.h>
#include <opencv2/opencv.hpp>

#include <algorithm>
#include <cmath>
#include <memory>
#include <string>
#include <vector>

using namespace InferenceBackend;

namespace {

using Resize = InputImageLayerDesc::Resize;
using Crop = InputImageLayerDesc::Crop;

InputImageLayerDesc::Ptr Desc(Resize resize, Crop crop = Crop::NO,
                              const InputImageLayerDesc::Padding &padding = InputImageLayerDesc::Padding()) {
    return std::make_shared<InputImageLayerDesc>(resize, crop, InputImageLayerDesc::ColorSpace::NO,
                                                 InputImageLayerDesc::RangeNormalization(),
                                                 InputImageLayerDesc::DistribNormalization(), padding);
}

TEST(OpenclRoiGeometry, StretchesTheRoiWithoutParameters) {
    const auto g = ComputeRoiGeometry(1920, 1080, {100, 50, 200, 400}, 64, 48, nullptr, nullptr);
    EXPECT_DOUBLE_EQ(g.src_x, 100);
    EXPECT_DOUBLE_EQ(g.src_y, 50);
    EXPECT_DOUBLE_EQ(g.src_w, 200);
    EXPECT_DOUBLE_EQ(g.src_h, 400);
    EXPECT_EQ(g.dst_x, 0);
    EXPECT_EQ(g.dst_y, 0);
    EXPECT_EQ(g.dst_w, 64);
    EXPECT_EQ(g.dst_h, 48);
}

TEST(OpenclRoiGeometry, EmptyRectIsTheWholeFrameAndRoisAreClamped) {
    auto g = ComputeRoiGeometry(1920, 1080, {}, 64, 48, nullptr, nullptr);
    EXPECT_DOUBLE_EQ(g.src_w, 1920);
    EXPECT_DOUBLE_EQ(g.src_h, 1080);

    g = ComputeRoiGeometry(1920, 1080, {1900, 1000, 100, 100}, 64, 48, nullptr, nullptr);
    EXPECT_DOUBLE_EQ(g.src_x, 1900);
    EXPECT_DOUBLE_EQ(g.src_w, 20);
    EXPECT_DOUBLE_EQ(g.src_h, 80);
}

TEST(OpenclRoiGeometry, AspectRatioResizeIsCenteredAndReported) {
    auto transform = std::make_shared<ImageTransformationParams>();
    const auto g = ComputeRoiGeometry(1920, 1080, {}, 640, 640, Desc(Resize::ASPECT_RATIO), transform);
    EXPECT_EQ(g.dst_w, 640);
    EXPECT_EQ(g.dst_h, 360);
    EXPECT_EQ(g.dst_x, 0);
    EXPECT_EQ(g.dst_y, 140);
    EXPECT_TRUE(transform->WasResize());
    EXPECT_DOUBLE_EQ(transform->resize_scale_x, 1.0 / 3);
    EXPECT_DOUBLE_EQ(transform->resize_scale_y, 1.0 / 3);
    EXPECT_TRUE(transform->WasPadding());
    EXPECT_EQ(transform->padding_size_x, 0u);
    EXPECT_EQ(transform->padding_size_y, 140u);
}

TEST(OpenclRoiGeometry, AspectRatioPadIsTopLeft) {
    const auto g = ComputeRoiGeometry(1920, 1080, {}, 640, 640, Desc(Resize::ASPECT_RATIO_PAD), nullptr);
    EXPECT_EQ(g.dst_w, 640);
    EXPECT_EQ(g.dst_h, 360);
    EXPECT_EQ(g.dst_x, 0);
    EXPECT_EQ(g.dst_y, 0);
}

TEST(OpenclRoiGeometry, CropStaysInsideTheRoi) {
    // The resized region is rounded (40.5 rows -> 41): mapped back, the crop must not leave the frame.
    for (auto crop : {Crop::CENTRAL, Crop::TOP_LEFT, Crop::TOP_RIGHT, Crop::BOTTOM_LEFT, Crop::BOTTOM_RIGHT}) {
        const auto g = ComputeRoiGeometry(1920, 1080, {}, 64, 48, Desc(Resize::ASPECT_RATIO, crop), nullptr);
        EXPECT_GE(g.src_x, 0);
        EXPECT_GE(g.src_y, 0);
        EXPECT_LE(g.src_x + g.src_w, 1920 + 1e-6);
        EXPECT_LE(g.src_y + g.src_h, 1080 + 1e-6);
        EXPECT_EQ(g.dst_w, 64);
    }
    const auto g = ComputeRoiGeometry(1920, 1080, {}, 64, 48, Desc(Resize::ASPECT_RATIO, Crop::CENTRAL), nullptr);
    EXPECT_NEAR(g.src_x + g.src_w / 2, 960, 1e-6);
}

TEST(OpenclRoiGeometry, CentralResizeCropsToTheInputAspectRatio) {
    auto transform = std::make_shared<ImageTransformationParams>();
    const auto g = ComputeRoiGeometry(1920, 1080, {37, 53, 300, 500}, 224, 224,
                                      Desc(Resize::ASPECT_RATIO, Crop::CENTRAL_RESIZE), transform);
    EXPECT_DOUBLE_EQ(g.src_x, 37);
    EXPECT_DOUBLE_EQ(g.src_y, 53 + 100);
    EXPECT_DOUBLE_EQ(g.src_w, 300);
    EXPECT_DOUBLE_EQ(g.src_h, 300);
    EXPECT_EQ(g.dst_w, 224);
    EXPECT_EQ(g.dst_h, 224);
    EXPECT_TRUE(transform->WasCrop());
}

TEST(OpenclRoiGeometry, PaddingStrideAndFillColor) {
    const InputImageLayerDesc::Padding padding(4, std::vector<double>{10, 120, 230});
    const auto g = ComputeRoiGeometry(1920, 1080, {}, 64, 48, Desc(Resize::ASPECT_RATIO, Crop::NO, padding), nullptr);
    EXPECT_EQ(g.dst_w, 56);
    EXPECT_EQ(g.dst_h, 32);
    EXPECT_EQ(g.dst_x, 4);
    EXPECT_EQ(g.dst_y, 8);
    EXPECT_EQ(g.fill_rgb[0], 10);
    EXPECT_EQ(g.fill_rgb[1], 120);
    EXPECT_EQ(g.fill_rgb[2], 230);
}

TEST(OpenclRoiGeometry, AspectRatioMultipleOfIsRejected) {
    EXPECT_ANY_THROW(ComputeRoiGeometry(1920, 1080, {}, 64, 48, Desc(Resize::ASPECT_RATIO_MULTIPLE_OF), nullptr));
}

// -- Kernels, on system memory frames -------------------------------------------------------------------------------

// The kernels' sampling: bilinear at pixel centers, clamped to the frame, on each plane.
double Bilinear(const cv::Mat &plane, int channel, double px, double py) {
    const double u = std::clamp(px, 0.5, plane.cols - 0.5) - 0.5, v = std::clamp(py, 0.5, plane.rows - 0.5) - 0.5;
    const int x0 = static_cast<int>(std::floor(u)), y0 = static_cast<int>(std::floor(v));
    const int x1 = std::min(x0 + 1, plane.cols - 1), y1 = std::min(y0 + 1, plane.rows - 1);
    const double a = u - x0, b = v - y0;
    const int c = plane.channels();
    auto at = [&](int x, int y) { return static_cast<double>(plane.ptr<uint8_t>(y)[x * c + channel]); };
    return at(x0, y0) * (1 - a) * (1 - b) + at(x1, y0) * a * (1 - b) + at(x0, y1) * (1 - a) * b + at(x1, y1) * a * b;
}

uint8_t Sat(double v) {
    return static_cast<uint8_t>(std::clamp(std::lround(v), 0l, 255l));
}

// The published YUV -> RGB coefficients of some color matrices, independent of how the backend derives them from Kr
// and Kb: R = y_scale (Y - y_offset) + r_v (V - 128), G = ... + g_u (U - 128) + g_v (V - 128), B = ... + b_u (U - 128).
struct YuvCoefficients {
    double y_scale, y_offset, r_v, g_u, g_v, b_u;
};
const YuvCoefficients kBt601Limited = {1.164383, 16, 1.596027, -0.391762, -0.812968, 2.017232};
const YuvCoefficients kBt709Limited = {1.164383, 16, 1.792741, -0.213249, -0.532909, 2.112402};
const YuvCoefficients kBt601Full = {1, 0, 1.402, -0.344136, -0.714136, 1.772};

// Planar BGR model input (3 x h x w) of `rect` of a BGR or NV12 frame, stretched.
std::vector<uint8_t> Reference(const cv::Mat &bgr, const cv::Mat &y_plane, const cv::Mat &uv_plane, cv::Rect rect,
                               int w, int h, const YuvCoefficients &yuv = kBt601Limited) {
    std::vector<uint8_t> out(3 * w * h);
    for (int j = 0; j < h; j++) {
        for (int i = 0; i < w; i++) {
            const double px = rect.x + (i + 0.5) * rect.width / w, py = rect.y + (j + 0.5) * rect.height / h;
            double b, g, r;
            if (!bgr.empty()) {
                b = Bilinear(bgr, 0, px, py);
                g = Bilinear(bgr, 1, px, py);
                r = Bilinear(bgr, 2, px, py);
            } else {
                const double luma = yuv.y_scale * (Bilinear(y_plane, 0, px, py) - yuv.y_offset);
                const double u = Bilinear(uv_plane, 0, px / 2, py / 2) - 128;
                const double v = Bilinear(uv_plane, 1, px / 2, py / 2) - 128;
                r = luma + yuv.r_v * v;
                g = luma + yuv.g_u * u + yuv.g_v * v;
                b = luma + yuv.b_u * u;
            }
            out[j * w + i] = Sat(b);
            out[w * h + j * w + i] = Sat(g);
            out[2 * w * h + j * w + i] = Sat(r);
        }
    }
    return out;
}

// A smooth, colorful test frame.
cv::Mat TestFrame(int width, int height, int seed) {
    cv::Mat small(9, 16, CV_8UC3);
    cv::RNG rng(seed);
    rng.fill(small, cv::RNG::UNIFORM, 0, 256);
    cv::Mat frame;
    cv::resize(small, frame, cv::Size(width, height), 0, 0, cv::INTER_CUBIC);
    return frame;
}

// A system memory NV12 frame of `bgr` (height a multiple of 4), in `y_plane` and `uv_plane` (interleaved, half
// resolution), which must outlive it. No colorimetry: the default for the frame size applies.
ImagePtr Nv12Image(const cv::Mat &bgr, cv::Mat &y_plane, cv::Mat &uv_plane) {
    cv::Mat i420;
    cv::cvtColor(bgr, i420, cv::COLOR_BGR2YUV_I420);
    const int h = bgr.rows, chroma_h = bgr.rows / 2;
    y_plane = i420.rowRange(0, h).clone();
    const cv::Mat u = i420.rowRange(h, h + chroma_h / 2).reshape(1, chroma_h);
    const cv::Mat v = i420.rowRange(h + chroma_h / 2, h + chroma_h).reshape(1, chroma_h);
    cv::merge(std::vector<cv::Mat>{u, v}, uv_plane);

    auto image = std::make_shared<Image>();
    image->type = MemoryType::SYSTEM;
    image->format = FOURCC_NV12;
    image->width = bgr.cols;
    image->height = bgr.rows;
    image->planes[0] = y_plane.data;
    image->planes[1] = uv_plane.data;
    image->stride[0] = static_cast<uint32_t>(y_plane.step);
    image->stride[1] = static_cast<uint32_t>(uv_plane.step);
    return image;
}

bool HaveIntelGpu() {
    try {
        clReleaseContext(CreateOpenclPreprocContext(nullptr));
        return true;
    } catch (const std::exception &) {
        return false;
    }
}

class OpenclPreprocessorTest : public ::testing::Test {
  protected:
    void SetUp() override {
        if (!HaveIntelGpu())
            GTEST_SKIP() << "no Intel GPU with OpenCL";
    }
};

struct Roi {
    ImagePtr frame;
    cv::Rect rect;
    std::vector<uint8_t> expected;
};

void ExpectClose(const uint8_t *got, const std::vector<uint8_t> &expected, const std::string &what) {
    int max_diff = 0;
    double sum = 0;
    for (size_t i = 0; i < expected.size(); i++) {
        const int d = std::abs(static_cast<int>(got[i]) - static_cast<int>(expected[i]));
        max_diff = std::max(max_diff, d);
        sum += d;
    }
    // The GPU sampler interpolates with fixed-point weights.
    EXPECT_LE(max_diff, 2) << what;
    EXPECT_LT(sum / expected.size(), 0.1) << what;
}

TEST_F(OpenclPreprocessorTest, BgrAndNv12FramesInOneBatch) {
    constexpr int W = 64, H = 48;
    const cv::Mat bgr = TestFrame(640, 360, 1);
    const cv::Mat bgr2 = TestFrame(640, 360, 2);
    cv::Mat y_plane, uv_plane;
    const ImagePtr nv12_image = Nv12Image(bgr2, y_plane, uv_plane);

    auto bgr_image = std::make_shared<Image>();
    bgr_image->type = MemoryType::SYSTEM;
    bgr_image->format = FOURCC_BGR;
    bgr_image->width = bgr.cols;
    bgr_image->height = bgr.rows;
    bgr_image->planes[0] = const_cast<uint8_t *>(bgr.data);
    bgr_image->stride[0] = static_cast<uint32_t>(bgr.step);

    // Two ROIs of the BGR frame (one launch), then two of the NV12 frame, one of them half out of the frame.
    std::vector<Roi> rois = {{bgr_image, {10, 20, 100, 200}, {}},
                             {bgr_image, {300, 100, 333, 211}, {}},
                             {nv12_image, {0, 0, 640, 360}, {}},
                             {nv12_image, {620, 330, 40, 60}, {}}};
    for (auto &roi : rois) {
        const cv::Rect inside = roi.rect & cv::Rect(0, 0, bgr.cols, bgr.rows); // as the backend clamps it
        roi.expected = roi.frame == bgr_image ? Reference(bgr, {}, {}, inside, W, H)
                                              : Reference({}, y_plane, uv_plane, inside, W, H);
    }

    OpenclPreprocessor::Settings settings;
    settings.width = W;
    settings.height = H;
    settings.rgb = false;
    settings.batch_size = rois.size();
    // No device context: the preprocessor's own context, the model input in host memory.
    OpenclPreprocessor preproc(nullptr, nullptr, settings);
    ASSERT_FALSE(preproc.OutputsOnDevice());
    OpenclBatch &batch = preproc.CreateBatch();
    ASSERT_NE(batch.host_memory, nullptr);

    for (int pass = 0; pass < 2; pass++) { // the batch is reused, as an infer request is
        for (size_t i = 0; i < rois.size(); i++) {
            // Qualified: on Windows, wingdi.h declares a Rectangle() function too.
            rois[i].frame->rect = InferenceBackend::Rectangle<uint32_t>(rois[i].rect.x, rois[i].rect.y,
                                                                        rois[i].rect.width, rois[i].rect.height);
            preproc.Add(batch, i, rois[i].frame, nullptr, nullptr);
        }
        preproc.Finish(batch);
        const auto *out = static_cast<const uint8_t *>(batch.host_memory);
        for (size_t i = 0; i < rois.size(); i++)
            ExpectClose(out + i * 3 * W * H, rois[i].expected, "ROI " + std::to_string(i));
    }
}

TEST_F(OpenclPreprocessorTest, Nv12FramesAreConvertedWithTheirColorimetry) {
    constexpr int W = 64, H = 48;
    const cv::Mat sd = TestFrame(640, 360, 4);
    const cv::Mat hd = TestFrame(1280, 720, 5);
    cv::Mat y_planes[3], uv_planes[3];
    // In one batch: BT.709 and BT.601 full range, as the caps can give them, and no colorimetry on a frame of 720
    // lines, which makes it BT.709.
    const std::vector<ImagePtr> frames = {Nv12Image(sd, y_planes[0], uv_planes[0]),
                                          Nv12Image(sd, y_planes[1], uv_planes[1]),
                                          Nv12Image(hd, y_planes[2], uv_planes[2])};
    frames[0]->colorimetry = {YuvColorimetry::Matrix::BT709, false};
    frames[1]->colorimetry = {YuvColorimetry::Matrix::BT601, true};
    const YuvCoefficients *expected[3] = {&kBt709Limited, &kBt601Full, &kBt709Limited};

    OpenclPreprocessor::Settings settings;
    settings.width = W;
    settings.height = H;
    settings.batch_size = frames.size();
    OpenclPreprocessor preproc(nullptr, nullptr, settings);
    OpenclBatch &batch = preproc.CreateBatch();
    for (size_t i = 0; i < frames.size(); i++)
        preproc.Add(batch, i, frames[i], nullptr, nullptr);
    preproc.Finish(batch);

    const auto *out = static_cast<const uint8_t *>(batch.host_memory);
    for (size_t i = 0; i < frames.size(); i++) {
        const cv::Rect whole(0, 0, static_cast<int>(frames[i]->width), static_cast<int>(frames[i]->height));
        ExpectClose(out + i * 3 * W * H, Reference({}, y_planes[i], uv_planes[i], whole, W, H, *expected[i]),
                    "frame " + std::to_string(i));
    }
}

TEST_F(OpenclPreprocessorTest, LetterboxIsFilled) {
    constexpr int W = 32, H = 32;
    const cv::Mat bgr = TestFrame(64, 32, 3);
    auto image = std::make_shared<Image>();
    image->type = MemoryType::SYSTEM;
    image->format = FOURCC_BGR;
    image->width = bgr.cols;
    image->height = bgr.rows;
    image->planes[0] = const_cast<uint8_t *>(bgr.data);
    image->stride[0] = static_cast<uint32_t>(bgr.step);

    OpenclPreprocessor::Settings settings;
    settings.width = W;
    settings.height = H;
    settings.rgb = true;
    OpenclPreprocessor preproc(nullptr, nullptr, settings);
    OpenclBatch &batch = preproc.CreateBatch();
    const InputImageLayerDesc::Padding padding(0, std::vector<double>{10, 120, 230});
    preproc.Add(batch, 0, image, Desc(Resize::ASPECT_RATIO, Crop::NO, padding), nullptr);
    preproc.Finish(batch);

    // 64x32 into 32x32: rows 8 to 23 are the image, the rest is the fill color, in RGB order.
    const auto *out = static_cast<const uint8_t *>(batch.host_memory);
    const uint8_t fill[3] = {10, 120, 230};
    for (int c = 0; c < 3; c++) {
        for (int y : {0, 7, 24, 31})
            for (int x = 0; x < W; x++)
                ASSERT_EQ(out[c * W * H + y * W + x], fill[c]) << "channel " << c << " row " << y;
    }
}

} // namespace
