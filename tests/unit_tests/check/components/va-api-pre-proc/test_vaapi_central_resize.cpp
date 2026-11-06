/*******************************************************************************
 * Copyright (C) 2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#include "vaapi_context.h"

#include "utils.h"
#include "vaapi_converter.h"

#include <gtest/gtest.h>

#include <ostream>
#include <tuple>
#include <vector>

using namespace InferenceBackend;

namespace {

constexpr uint16_t frame_width = 1920;
constexpr uint16_t frame_height = 1080;
constexpr int16_t roi_x = 37;
constexpr int16_t roi_y = 53;

struct CentralResizeCase {
    const char *name;
    uint16_t roi_width;
    uint16_t roi_height;
    uint16_t target_width;
    uint16_t target_height;
    InputImageLayerDesc::CropRect expected_crop;
};

std::ostream &operator<<(std::ostream &os, const CentralResizeCase &c) {
    return os << c.name;
}

// Same expectations as the OpenCV pre-processor tests, so both backends share one geometry
const std::vector<CentralResizeCase> central_resize_cases = {
    {"PortraitNarrowerThanTarget", 174, 424, 224, 224, {0, 125, 174, 174}},
    {"LandscapeLowerThanTarget", 424, 174, 224, 224, {125, 0, 174, 174}},
    {"SmallerThanTarget", 100, 150, 224, 224, {0, 25, 100, 100}},
    {"LargerThanTarget", 300, 500, 224, 224, {0, 100, 300, 300}},
    {"LandscapeTarget", 300, 300, 224, 112, {0, 75, 300, 150}},
    {"PortraitTargetSmallLandscapeRoi", 120, 80, 112, 224, {40, 0, 40, 80}},
};

class TestableVaApiConverter : public VaApiConverter {
  public:
    using VaApiConverter::SetupPipelineRegionsWithCustomParams;
    using VaApiConverter::VaApiConverter;
};

std::string CaseName(const testing::TestParamInfo<std::tuple<CentralResizeCase, InputImageLayerDesc::Resize>> &info) {
    static const char *resize_names[] = {"No", "NoAspectRatio", "AspectRatio", "AspectRatioPad",
                                         "AspectRatioMultipleOf"};
    return std::string(std::get<0>(info.param).name) + "_Resize" +
           resize_names[static_cast<int>(std::get<1>(info.param))];
}

} // namespace

class VAAPIConverterCentralResizeTest
    : public testing::TestWithParam<std::tuple<CentralResizeCase, InputImageLayerDesc::Resize>> {
  protected:
    std::unique_ptr<VaApiContext> va_context;
    int drm_fd = -1;
    VASurfaceID surface_id = VA_INVALID_SURFACE;

    void SetUp() override {
        va_context.reset(new VaApiContext(createVASurface(surface_id, drm_fd)));
    }
};

TEST_P(VAAPIConverterCentralResizeTest, FillsTargetWithCenteredCrop) {
    const auto &c = std::get<0>(GetParam());
    const auto pre_proc_info = std::make_shared<InputImageLayerDesc>(
        std::get<1>(GetParam()), InputImageLayerDesc::Crop::CENTRAL_RESIZE, InputImageLayerDesc::ColorSpace::NO,
        InputImageLayerDesc::RangeNormalization(), InputImageLayerDesc::DistribNormalization());
    auto transform = std::make_shared<ImageTransformationParams>();

    TestableVaApiConverter converter(va_context.get());
    VARectangle src_region = {roi_x, roi_y, c.roi_width, c.roi_height};
    VARectangle dst_region = {0, 0, c.roi_width, c.roi_height};
    VAProcPipelineParameterBuffer pipeline_param = {};

    converter.SetupPipelineRegionsWithCustomParams(pre_proc_info, frame_width, frame_height, c.target_width,
                                                   c.target_height, src_region, dst_region, pipeline_param, transform);

    EXPECT_EQ(src_region.x, roi_x + static_cast<int16_t>(c.expected_crop.x));
    EXPECT_EQ(src_region.y, roi_y + static_cast<int16_t>(c.expected_crop.y));
    EXPECT_EQ(src_region.width, c.expected_crop.width);
    EXPECT_EQ(src_region.height, c.expected_crop.height);

    EXPECT_EQ(dst_region.x, 0);
    EXPECT_EQ(dst_region.y, 0);
    EXPECT_EQ(dst_region.width, c.target_width);
    EXPECT_EQ(dst_region.height, c.target_height);

    const double scale_x = static_cast<double>(c.target_width) / c.expected_crop.width;
    const double scale_y = static_cast<double>(c.target_height) / c.expected_crop.height;
    EXPECT_DOUBLE_EQ(transform->resize_scale_x, scale_x);
    EXPECT_DOUBLE_EQ(transform->resize_scale_y, scale_y);
    EXPECT_EQ(transform->padding_size_x, 0u);
    EXPECT_EQ(transform->padding_size_y, 0u);
    EXPECT_EQ(transform->croped_border_size_x, static_cast<size_t>(c.expected_crop.x * scale_x + 0.5));
    EXPECT_EQ(transform->croped_border_size_y, static_cast<size_t>(c.expected_crop.y * scale_y + 0.5));
}

INSTANTIATE_TEST_SUITE_P(CentralResize, VAAPIConverterCentralResizeTest,
                         testing::Combine(testing::ValuesIn(central_resize_cases),
                                          testing::Values(InputImageLayerDesc::Resize::NO,
                                                          InputImageLayerDesc::Resize::NO_ASPECT_RATIO,
                                                          InputImageLayerDesc::Resize::ASPECT_RATIO,
                                                          InputImageLayerDesc::Resize::ASPECT_RATIO_PAD)),
                         CaseName);
