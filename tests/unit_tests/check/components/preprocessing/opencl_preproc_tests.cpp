/*******************************************************************************
 * Copyright (C) 2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

// The OpenCL pre-processing backend (pre-process-backend=opencl): the regions it computes from model-proc parameters,
// and, on a machine with an Intel GPU, the model input its kernels write for system memory frames (BGR, BGRx, RGBx,
// NV12, I420).

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

// What the reference reads: B, G, R pixels, or a Y plane with U and V at half resolution (one interleaved plane for
// NV12, two planes for I420).
struct Source {
    cv::Mat bgr;
    cv::Mat y, u, v;
    int u_channel = 0, v_channel = 0;
};

Source Packed(const cv::Mat &bgr) {
    Source s;
    s.bgr = bgr;
    return s;
}

Source Nv12(const cv::Mat &y, const cv::Mat &uv) {
    Source s;
    s.y = y;
    s.u = uv;
    s.v = uv;
    s.v_channel = 1;
    return s;
}

Source I420(const cv::Mat &y, const cv::Mat &u, const cv::Mat &v) {
    Source s;
    s.y = y;
    s.u = u;
    s.v = v;
    return s;
}

// Planar model input (3 x h x w: B, G, R planes, or R, G, B if `rgb`) of `rect` of the frame, stretched.
std::vector<uint8_t> Reference(const Source &src, cv::Rect rect, int w, int h, bool rgb) {
    std::vector<uint8_t> out(3 * w * h);
    for (int j = 0; j < h; j++) {
        for (int i = 0; i < w; i++) {
            const double px = rect.x + (i + 0.5) * rect.width / w, py = rect.y + (j + 0.5) * rect.height / h;
            double b, g, r;
            if (!src.bgr.empty()) {
                b = Bilinear(src.bgr, 0, px, py);
                g = Bilinear(src.bgr, 1, px, py);
                r = Bilinear(src.bgr, 2, px, py);
            } else {
                const double c = Bilinear(src.y, 0, px, py) - 16;
                const double d = Bilinear(src.u, src.u_channel, px / 2, py / 2) - 128;
                const double e = Bilinear(src.v, src.v_channel, px / 2, py / 2) - 128;
                r = 1.164 * c + 1.596 * e;
                g = 1.164 * c - 0.391 * d - 0.813 * e;
                b = 1.164 * c + 2.018 * d;
            }
            const double planes[3] = {rgb ? r : b, g, rgb ? b : r};
            for (int p = 0; p < 3; p++)
                out[p * w * h + j * w + i] = Sat(planes[p]);
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

// Y, U and V planes of a BGR frame.
void ToI420(const cv::Mat &bgr, cv::Mat &y, cv::Mat &u, cv::Mat &v) {
    cv::Mat i420;
    cv::cvtColor(bgr, i420, cv::COLOR_BGR2YUV_I420);
    const int cw = bgr.cols / 2, ch = bgr.rows / 2;
    y = i420.rowRange(0, bgr.rows).clone();
    u = cv::Mat(ch, cw, CV_8UC1, i420.ptr(bgr.rows)).clone();
    v = cv::Mat(ch, cw, CV_8UC1, i420.ptr(bgr.rows) + cw * ch).clone();
}

// 4-byte pixels of a BGR frame, in the order of `code` (cv::COLOR_BGR2BGRA or cv::COLOR_BGR2RGBA), with noise in the
// 4th byte, which the kernels must ignore.
cv::Mat WithX(const cv::Mat &bgr, int code, int seed) {
    cv::Mat out;
    cv::cvtColor(bgr, out, code);
    cv::Mat noise(bgr.size(), CV_8UC1);
    cv::RNG rng(seed);
    rng.fill(noise, cv::RNG::UNIFORM, 0, 256);
    const int from_to[] = {0, 3};
    cv::mixChannels(&noise, 1, &out, 1, from_to, 1);
    return out;
}

// A system memory frame on `planes`, which must outlive it.
ImagePtr SystemFrame(int format, const std::vector<const cv::Mat *> &planes) {
    auto image = std::make_shared<Image>();
    image->type = MemoryType::SYSTEM;
    image->format = format;
    image->width = planes[0]->cols;
    image->height = planes[0]->rows;
    for (size_t i = 0; i < planes.size(); i++) {
        image->planes[i] = const_cast<uint8_t *>(planes[i]->data);
        image->stride[i] = static_cast<uint32_t>(planes[i]->step);
    }
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
    Source source;
    cv::Rect rect;
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

// The ROIs in one batch of a w x h model input, compared to the reference. Twice: the batch is reused, as an infer
// request is.
void RunBatch(const std::vector<Roi> &rois, int w, int h, bool rgb) {
    OpenclPreprocessor::Settings settings;
    settings.width = w;
    settings.height = h;
    settings.rgb = rgb;
    settings.batch_size = rois.size();
    // No device context: the preprocessor's own context, the model input in host memory.
    OpenclPreprocessor preproc(nullptr, nullptr, settings);
    ASSERT_FALSE(preproc.OutputsOnDevice());
    OpenclBatch &batch = preproc.CreateBatch();
    ASSERT_NE(batch.host_memory, nullptr);

    std::vector<std::vector<uint8_t>> expected;
    for (const auto &roi : rois) {
        // As the backend clamps it.
        const cv::Rect inside = roi.rect & cv::Rect(0, 0, roi.frame->width, roi.frame->height);
        expected.push_back(Reference(roi.source, inside, w, h, rgb));
    }
    for (int pass = 0; pass < 2; pass++) {
        for (size_t i = 0; i < rois.size(); i++) {
            const cv::Rect &r = rois[i].rect;
            rois[i].frame->rect = Rectangle<uint32_t>(r.x, r.y, r.width, r.height);
            preproc.Add(batch, i, rois[i].frame, nullptr, nullptr);
        }
        preproc.Finish(batch);
        const auto *out = static_cast<const uint8_t *>(batch.host_memory);
        for (size_t i = 0; i < rois.size(); i++)
            ExpectClose(out + i * 3 * w * h, expected[i],
                        "pass " + std::to_string(pass) + ", ROI " + std::to_string(i));
    }
}

TEST_F(OpenclPreprocessorTest, BgrAndNv12FramesInOneBatch) {
    const cv::Mat bgr = TestFrame(640, 360, 1);
    cv::Mat y, u, v, uv;
    ToI420(TestFrame(640, 360, 2), y, u, v);
    cv::merge(std::vector<cv::Mat>{u, v}, uv); // NV12: U and V interleaved
    const auto bgr_frame = SystemFrame(FOURCC_BGR, {&bgr});
    const auto nv12_frame = SystemFrame(FOURCC_NV12, {&y, &uv});

    // Two ROIs of the BGR frame (one launch), then two of the NV12 frame, one of them half out of the frame.
    RunBatch({{bgr_frame, Packed(bgr), {10, 20, 100, 200}},
              {bgr_frame, Packed(bgr), {300, 100, 333, 211}},
              {nv12_frame, Nv12(y, uv), {0, 0, 640, 360}},
              {nv12_frame, Nv12(y, uv), {620, 330, 40, 60}}},
             64, 48, false);
}

TEST_F(OpenclPreprocessorTest, I420FrameIntoAnUnalignedInput) {
    cv::Mat y, u, v;
    ToI420(TestFrame(640, 360, 3), y, u, v);
    const auto frame = SystemFrame(FOURCC_I420, {&y, &u, &v});

    // The whole frame, an odd origin (chroma between samples) and a ROI half out of the frame. 50 x 30 is not a
    // multiple of the kernels' 32 x 8 tiles: the work items past the input must not write into the next rows.
    RunBatch({{frame, I420(y, u, v), {0, 0, 640, 360}},
              {frame, I420(y, u, v), {37, 53, 101, 77}},
              {frame, I420(y, u, v), {600, 300, 80, 90}}},
             50, 30, false);
}

TEST_F(OpenclPreprocessorTest, BgrxAndRgbxFramesInAnRgbModel) {
    const cv::Mat bgr = TestFrame(640, 360, 4);
    const cv::Mat bgr2 = TestFrame(640, 360, 5);
    const cv::Mat bgrx = WithX(bgr, cv::COLOR_BGR2BGRA, 6);
    const cv::Mat rgbx = WithX(bgr2, cv::COLOR_BGR2RGBA, 7);
    const auto bgrx_frame = SystemFrame(FOURCC_BGRX, {&bgrx});
    const auto rgbx_frame = SystemFrame(FOURCC_RGBX, {&rgbx});

    // Both byte orders into a model that takes R, G, B planes.
    RunBatch({{bgrx_frame, Packed(bgr), {10, 20, 100, 200}},
              {bgrx_frame, Packed(bgr), {600, 300, 80, 90}},
              {rgbx_frame, Packed(bgr2), {0, 0, 640, 360}},
              {rgbx_frame, Packed(bgr2), {37, 53, 101, 77}}},
             64, 48, true);
}

TEST_F(OpenclPreprocessorTest, LetterboxIsFilled) {
    constexpr int W = 32, H = 32;
    const cv::Mat bgr = TestFrame(64, 32, 8);
    const auto image = SystemFrame(FOURCC_BGR, {&bgr});

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
