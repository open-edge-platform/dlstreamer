/*******************************************************************************
 * Copyright (C) 2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

// Tests for the D3D11/D3D12 pre-processing converters: source region (ROI) selection,
// resize/crop/padding geometry and the pixels they produce on the GPU.

#include "d3d11_converter.h"
#include "d3d12_converter.h"

#include "utils.h"

#include <gtest/gtest.h>

#include <memory>
#include <stdexcept>
#include <vector>

using namespace InferenceBackend;
using namespace d3d_test;

namespace {

using Roi = InferenceBackend::Rectangle<uint32_t>;

struct D3D11Backend {
    using Converter = D3D11Converter;
    using DevicePtr = D3D11DevicePtr;
    using Device = GstD3D11Device;
    static constexpr const char *name = "D3D11";

    static DevicePtr CreateDevice() {
        return CreateD3D11Device();
    }
    static BufferPtr AllocSource(Device *device, uint32_t width, uint32_t height,
                                 DXGI_FORMAT format = DXGI_FORMAT_B8G8R8A8_UNORM) {
        return AllocD3D11Texture(device, width, height, format, D3D11_BIND_SHADER_RESOURCE);
    }
    static BufferPtr AllocTarget(Device *device, uint32_t width, uint32_t height) {
        return AllocD3D11Texture(device, width, height, DXGI_FORMAT_B8G8R8A8_UNORM,
                                 D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE);
    }
    static void Fill(GstBuffer *buffer, const PixelFn &pixel) {
        FillD3D11(buffer, pixel);
    }
    static void FillNv12(GstBuffer *buffer, uint8_t y, uint8_t u, uint8_t v) {
        FillD3D11Nv12(buffer, y, u, v);
    }
    static BgraImage Read(GstBuffer *buffer) {
        return ReadD3D11(buffer);
    }
    static void Convert(Converter &converter, GstBuffer *src, GstBuffer *dst,
                        const InputImageLayerDesc::Ptr &pre_proc_info = nullptr,
                        const ImageTransformationParams::Ptr &transform = nullptr, const Roi &roi = {}) {
        converter.Convert(src, dst, pre_proc_info, transform, roi);
    }
};

struct D3D12Backend {
    using Converter = D3D12Converter;
    using DevicePtr = D3D12DevicePtr;
    using Device = GstD3D12Device;
    static constexpr const char *name = "D3D12";

    static DevicePtr CreateDevice() {
        return CreateD3D12Device();
    }
    static BufferPtr AllocSource(Device *device, uint32_t width, uint32_t height,
                                 DXGI_FORMAT format = DXGI_FORMAT_B8G8R8A8_UNORM) {
        return AllocD3D12Texture(device, width, height, format, D3D12_RESOURCE_FLAG_NONE);
    }
    static BufferPtr AllocTarget(Device *device, uint32_t width, uint32_t height) {
        return AllocD3D12Texture(device, width, height, DXGI_FORMAT_B8G8R8A8_UNORM,
                                 D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET);
    }
    static void Fill(GstBuffer *buffer, const PixelFn &pixel) {
        FillD3D12(buffer, pixel);
    }
    static void FillNv12(GstBuffer *buffer, uint8_t y, uint8_t u, uint8_t v) {
        FillD3D12Nv12(buffer, y, u, v);
    }
    static BgraImage Read(GstBuffer *buffer) {
        return ReadD3D12(buffer);
    }
    static void Convert(Converter &converter, GstBuffer *src, GstBuffer *dst,
                        const InputImageLayerDesc::Ptr &pre_proc_info = nullptr,
                        const ImageTransformationParams::Ptr &transform = nullptr, const Roi &roi = {}) {
        WaitFence(converter.fence(), converter.Convert(src, dst, pre_proc_info, transform, roi));
    }
};

constexpr DXGI_FORMAT BGRA = DXGI_FORMAT_B8G8R8A8_UNORM;
// Same channel tolerance as the shader output of solid areas needs
constexpr int tolerance = 2;

InputImageLayerDesc::Ptr MakeDesc(InputImageLayerDesc::Resize resize, InputImageLayerDesc::Crop crop,
                                  const InputImageLayerDesc::Padding &padding = {}) {
    return std::make_shared<InputImageLayerDesc>(resize, crop, InputImageLayerDesc::ColorSpace::NO,
                                                 InputImageLayerDesc::RangeNormalization(),
                                                 InputImageLayerDesc::DistribNormalization(), padding);
}

struct CentralResizeCase {
    const char *name;
    uint32_t roi_width;
    uint32_t roi_height;
    uint32_t target_width;
    uint32_t target_height;
    InputImageLayerDesc::CropRect expected_crop;
};

// Same expectations as the OpenCV and VA-API pre-processor tests, so all backends share one geometry
const std::vector<CentralResizeCase> central_resize_cases = {
    {"PortraitNarrowerThanTarget", 174, 424, 224, 224, {0, 125, 174, 174}},
    {"LandscapeLowerThanTarget", 424, 174, 224, 224, {125, 0, 174, 174}},
    {"SmallerThanTarget", 100, 150, 224, 224, {0, 25, 100, 100}},
    {"LargerThanTarget", 300, 500, 224, 224, {0, 100, 300, 300}},
    {"LandscapeTarget", 300, 300, 224, 112, {0, 75, 300, 150}},
    {"PortraitTargetSmallLandscapeRoi", 120, 80, 112, 224, {40, 0, 40, 80}},
};

} // namespace

template <typename Backend>
class D3DConverterTest : public testing::Test {
  protected:
    typename Backend::DevicePtr device;

    void SetUp() override {
        device = Backend::CreateDevice();
        if (!device)
            GTEST_SKIP() << "No " << Backend::name << " device";
    }

    std::unique_ptr<typename Backend::Converter> MakeConverter(uint32_t src_width, uint32_t src_height,
                                                               uint32_t dst_width, uint32_t dst_height,
                                                               DXGI_FORMAT src_format = BGRA) {
        return std::make_unique<typename Backend::Converter>(device.get(), src_width, src_height, src_format, dst_width,
                                                             dst_height, BGRA);
    }

    BufferPtr MakeSource(uint32_t width, uint32_t height, const PixelFn &pixel) {
        auto buffer = Backend::AllocSource(device.get(), width, height);
        Backend::Fill(buffer.get(), pixel);
        return buffer;
    }
};

using Backends = testing::Types<D3D11Backend, D3D12Backend>;

class BackendNames {
  public:
    template <typename T>
    static std::string GetName(int) {
        return T::name;
    }
};

TYPED_TEST_SUITE(D3DConverterTest, Backends, BackendNames);

TYPED_TEST(D3DConverterTest, NullDeviceThrows) {
    using Converter = typename TypeParam::Converter;
    EXPECT_THROW(Converter(nullptr, 64, 64, BGRA, 32, 32, BGRA), std::invalid_argument);
}

TYPED_TEST(D3DConverterTest, UnsupportedFormatThrows) {
    using Converter = typename TypeParam::Converter;
    EXPECT_THROW(Converter(this->device.get(), 64, 64, DXGI_FORMAT_R16_UNORM, 32, 32, BGRA), std::invalid_argument);
    EXPECT_THROW(Converter(this->device.get(), 64, 64, BGRA, 32, 32, DXGI_FORMAT_R16_UNORM), std::invalid_argument);
}

TYPED_TEST(D3DConverterTest, SupportedFormatsConstruct) {
    using Converter = typename TypeParam::Converter;
    for (DXGI_FORMAT src : {DXGI_FORMAT_NV12, BGRA, DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_B8G8R8X8_UNORM}) {
        for (DXGI_FORMAT dst : {BGRA, DXGI_FORMAT_R8G8B8A8_UNORM}) {
            SCOPED_TRACE(testing::Message() << "src format " << src << ", dst format " << dst);
            EXPECT_NO_THROW(Converter(this->device.get(), 64, 64, src, 32, 32, dst));
        }
    }
}

TYPED_TEST(D3DConverterTest, NullBufferThrows) {
    auto converter = this->MakeConverter(64, 64, 32, 32);
    auto src = this->MakeSource(64, 64, QuadrantPattern(0, 0, 64, 64));
    auto dst = TypeParam::AllocTarget(this->device.get(), 32, 32);

    EXPECT_THROW(converter->Convert(nullptr, dst.get()), std::invalid_argument);
    EXPECT_THROW(converter->Convert(src.get(), nullptr), std::invalid_argument);
}

TYPED_TEST(D3DConverterTest, FullFrameResize) {
    auto converter = this->MakeConverter(128, 96, 64, 48);
    auto src = this->MakeSource(128, 96, QuadrantPattern(0, 0, 128, 96));
    auto dst = TypeParam::AllocTarget(this->device.get(), 64, 48);

    TypeParam::Convert(*converter, src.get(), dst.get());

    ExpectQuadrants(TypeParam::Read(dst.get()), 0, 0, 64, 48, 1);
}

TYPED_TEST(D3DConverterTest, RoiSelectsSourceRegion) {
    const Roi roi(40, 24, 120, 80);
    auto converter = this->MakeConverter(256, 192, 60, 40);
    auto src = this->MakeSource(256, 192, QuadrantPattern(roi.x, roi.y, roi.width, roi.height));
    auto dst = TypeParam::AllocTarget(this->device.get(), 60, 40);

    TypeParam::Convert(*converter, src.get(), dst.get(), nullptr, nullptr, roi);

    ExpectQuadrants(TypeParam::Read(dst.get()), 0, 0, 60, 40, 1);
}

TYPED_TEST(D3DConverterTest, RoiIsClampedToFrame) {
    // Detection boxes may extend past the frame: only the part inside it is used
    auto converter = this->MakeConverter(128, 128, 32, 32);
    auto src = this->MakeSource(128, 128, QuadrantPattern(64, 64, 64, 64));
    auto dst = TypeParam::AllocTarget(this->device.get(), 32, 32);

    TypeParam::Convert(*converter, src.get(), dst.get(), nullptr, nullptr, Roi(64, 64, 100, 100));

    ExpectQuadrants(TypeParam::Read(dst.get()), 0, 0, 32, 32, 1);
}

TYPED_TEST(D3DConverterTest, RoiOutsideFrameThrows) {
    auto converter = this->MakeConverter(128, 128, 32, 32);
    auto src = this->MakeSource(128, 128, QuadrantPattern(0, 0, 128, 128));
    auto dst = TypeParam::AllocTarget(this->device.get(), 32, 32);

    EXPECT_THROW(converter->Convert(src.get(), dst.get(), nullptr, nullptr, Roi(128, 0, 10, 10)), std::runtime_error);
    EXPECT_THROW(converter->Convert(src.get(), dst.get(), nullptr, nullptr, Roi(0, 200, 10, 10)), std::runtime_error);

    // A failed conversion leaves the converter usable
    TypeParam::Convert(*converter, src.get(), dst.get());
    ExpectQuadrants(TypeParam::Read(dst.get()), 0, 0, 32, 32, 1);
}

TYPED_TEST(D3DConverterTest, AspectRatioResizeLetterboxesInBlack) {
    auto converter = this->MakeConverter(200, 100, 128, 128);
    auto src = this->MakeSource(200, 100, QuadrantPattern(0, 0, 200, 100));
    auto dst = TypeParam::AllocTarget(this->device.get(), 128, 128);
    auto transform = std::make_shared<ImageTransformationParams>();

    TypeParam::Convert(*converter, src.get(), dst.get(),
                       MakeDesc(InputImageLayerDesc::Resize::ASPECT_RATIO, InputImageLayerDesc::Crop::NO), transform);

    EXPECT_DOUBLE_EQ(transform->resize_scale_x, 0.64);
    EXPECT_DOUBLE_EQ(transform->resize_scale_y, 0.64);
    EXPECT_EQ(transform->padding_size_x, 0u);
    EXPECT_EQ(transform->padding_size_y, 32u);

    const auto out = TypeParam::Read(dst.get());
    EXPECT_TRUE(RegionIs(out, 0, 0, 128, 32, colors::black, tolerance)) << "top border";
    EXPECT_TRUE(RegionIs(out, 0, 96, 128, 128, colors::black, tolerance)) << "bottom border";
    ExpectQuadrants(out, 0, 32, 128, 96, 2);
}

TYPED_TEST(D3DConverterTest, PaddingUsesFillValue) {
    // 64x64 target with an 8 pixel border: the image is resized to the inner 48x48
    const Bgra fill = {200, 200, 200, 255};
    auto converter = this->MakeConverter(128, 128, 64, 64);
    auto src = this->MakeSource(128, 128, QuadrantPattern(0, 0, 128, 128));
    auto dst = TypeParam::AllocTarget(this->device.get(), 64, 64);
    auto transform = std::make_shared<ImageTransformationParams>();

    TypeParam::Convert(*converter, src.get(), dst.get(),
                       MakeDesc(InputImageLayerDesc::Resize::NO_ASPECT_RATIO, InputImageLayerDesc::Crop::NO,
                                InputImageLayerDesc::Padding(8, {200.0, 200.0, 200.0})),
                       transform);

    EXPECT_DOUBLE_EQ(transform->resize_scale_x, 0.375);
    EXPECT_DOUBLE_EQ(transform->resize_scale_y, 0.375);
    EXPECT_EQ(transform->padding_size_x, 8u);
    EXPECT_EQ(transform->padding_size_y, 8u);

    const auto out = TypeParam::Read(dst.get());
    EXPECT_TRUE(RegionIs(out, 0, 0, 64, 8, fill, tolerance)) << "top border";
    EXPECT_TRUE(RegionIs(out, 0, 56, 64, 64, fill, tolerance)) << "bottom border";
    EXPECT_TRUE(RegionIs(out, 0, 8, 8, 56, fill, tolerance)) << "left border";
    EXPECT_TRUE(RegionIs(out, 56, 8, 64, 56, fill, tolerance)) << "right border";
    ExpectQuadrants(out, 8, 8, 56, 56, 2);
}

TYPED_TEST(D3DConverterTest, SimpleResizeAfterPreprocessingFillsTarget) {
    // The converter is cached per source size and reused: a conversion without
    // pre-processing info must not keep the destination rectangle of the previous one
    auto converter = this->MakeConverter(200, 100, 128, 128);
    auto src = this->MakeSource(200, 100, QuadrantPattern(0, 0, 200, 100));
    auto dst = TypeParam::AllocTarget(this->device.get(), 128, 128);

    TypeParam::Convert(*converter, src.get(), dst.get(),
                       MakeDesc(InputImageLayerDesc::Resize::ASPECT_RATIO, InputImageLayerDesc::Crop::NO),
                       std::make_shared<ImageTransformationParams>());
    TypeParam::Convert(*converter, src.get(), dst.get());

    ExpectQuadrants(TypeParam::Read(dst.get()), 0, 0, 128, 128, 1);
}

TYPED_TEST(D3DConverterTest, AspectRatioMultipleOfResizeThrows) {
    auto converter = this->MakeConverter(200, 100, 128, 128);
    auto src = this->MakeSource(200, 100, QuadrantPattern(0, 0, 200, 100));
    auto dst = TypeParam::AllocTarget(this->device.get(), 128, 128);

    EXPECT_THROW(converter->Convert(
                     src.get(), dst.get(),
                     MakeDesc(InputImageLayerDesc::Resize::ASPECT_RATIO_MULTIPLE_OF, InputImageLayerDesc::Crop::NO),
                     std::make_shared<ImageTransformationParams>()),
                 std::runtime_error);
}

TYPED_TEST(D3DConverterTest, CentralResizeCropsRoiCenter) {
    constexpr uint32_t frame_size = 640;
    constexpr uint32_t roi_x = 37;
    constexpr uint32_t roi_y = 53;

    for (const auto &c : central_resize_cases) {
        const auto &crop = c.expected_crop;
        // Only the expected crop shows the quadrants: anything else sampled shows up as white
        auto src = this->MakeSource(
            frame_size, frame_size,
            QuadrantPattern(roi_x + static_cast<uint32_t>(crop.x), roi_y + static_cast<uint32_t>(crop.y),
                            static_cast<uint32_t>(crop.width), static_cast<uint32_t>(crop.height)));
        auto converter = this->MakeConverter(frame_size, frame_size, c.target_width, c.target_height);
        auto dst = TypeParam::AllocTarget(this->device.get(), c.target_width, c.target_height);

        for (auto resize : {InputImageLayerDesc::Resize::NO, InputImageLayerDesc::Resize::NO_ASPECT_RATIO,
                            InputImageLayerDesc::Resize::ASPECT_RATIO, InputImageLayerDesc::Resize::ASPECT_RATIO_PAD}) {
            SCOPED_TRACE(testing::Message() << c.name << ", resize " << static_cast<int>(resize));
            auto transform = std::make_shared<ImageTransformationParams>();

            TypeParam::Convert(*converter, src.get(), dst.get(),
                               MakeDesc(resize, InputImageLayerDesc::Crop::CENTRAL_RESIZE), transform,
                               Roi(roi_x, roi_y, c.roi_width, c.roi_height));

            const double scale_x = static_cast<double>(c.target_width) / crop.width;
            const double scale_y = static_cast<double>(c.target_height) / crop.height;
            EXPECT_DOUBLE_EQ(transform->resize_scale_x, scale_x);
            EXPECT_DOUBLE_EQ(transform->resize_scale_y, scale_y);
            EXPECT_EQ(transform->padding_size_x, 0u);
            EXPECT_EQ(transform->padding_size_y, 0u);
            EXPECT_EQ(transform->croped_border_size_x, static_cast<size_t>(crop.x * scale_x + 0.5));
            EXPECT_EQ(transform->croped_border_size_y, static_cast<size_t>(crop.y * scale_y + 0.5));

            ExpectQuadrants(TypeParam::Read(dst.get()), 0, 0, c.target_width, c.target_height, 3);
        }
    }
}

TYPED_TEST(D3DConverterTest, Nv12SourceIsConvertedToBgra) {
    auto converter = this->MakeConverter(64, 64, 32, 32, DXGI_FORMAT_NV12);
    auto src = TypeParam::AllocSource(this->device.get(), 64, 64, DXGI_FORMAT_NV12);
    // Mid-gray: limited-range Y=128 is 130 in full range whatever the color matrix
    TypeParam::FillNv12(src.get(), 128, 128, 128);
    auto dst = TypeParam::AllocTarget(this->device.get(), 32, 32);

    TypeParam::Convert(*converter, src.get(), dst.get());

    EXPECT_TRUE(RegionIs(TypeParam::Read(dst.get()), 0, 0, 32, 32, Bgra{130, 130, 130, 255}, 3));
}

// -- D3D11 only -----------------------------------------------------------------

class D3D11ConverterTest : public D3DConverterTest<D3D11Backend> {};

TEST_F(D3D11ConverterTest, SourceWithoutShaderResourceViewIsCopied) {
    // Decoder DPB textures often have no SRV, and are padded to the macroblock size
    // (128 -> 144 rows here). Both sizes must convert correctly through the SRV copy.
    auto converter = MakeConverter(128, 128, 64, 64);
    auto dst = D3D11Backend::AllocTarget(device.get(), 64, 64);

    for (uint32_t texture_height : {128u, 144u, 128u}) {
        SCOPED_TRACE(testing::Message() << "texture height " << texture_height);
        auto src = AllocD3D11Texture(device.get(), 128, texture_height, BGRA, D3D11_BIND_RENDER_TARGET);
        FillD3D11(src.get(), QuadrantPattern(0, 0, 128, 128));

        D3D11Backend::Convert(*converter, src.get(), dst.get());

        ExpectQuadrants(ReadD3D11(dst.get()), 0, 0, 64, 64, 1);
    }
}

// -- D3D12 only -----------------------------------------------------------------

class D3D12ConverterTest : public D3DConverterTest<D3D12Backend> {};

TEST_F(D3D12ConverterTest, ConvertTagsTargetWithFence) {
    auto converter = MakeConverter(64, 64, 32, 32);
    auto src = MakeSource(64, 64, QuadrantPattern(0, 0, 64, 64));
    auto dst = D3D12Backend::AllocTarget(device.get(), 32, 32);

    const uint64_t first = converter->Convert(src.get(), dst.get());
    const uint64_t second = converter->Convert(src.get(), dst.get());
    EXPECT_GT(first, 0u);
    EXPECT_GT(second, first);

    ID3D12Fence *fence = nullptr;
    guint64 fence_value = 0;
    ASSERT_TRUE(
        gst_d3d12_memory_get_fence(GST_D3D12_MEMORY_CAST(gst_buffer_peek_memory(dst.get(), 0)), &fence, &fence_value));
    EXPECT_EQ(fence, converter->fence());
    EXPECT_EQ(fence_value, second);
    if (fence)
        fence->Release();

    WaitFence(converter->fence(), second);
    ExpectQuadrants(ReadD3D12(dst.get()), 0, 0, 32, 32, 1);
}

TEST_F(D3D12ConverterTest, ManyConversionsInFlight) {
    // Submissions without waiting in between recycle command allocators through the
    // queue's fence notifications
    constexpr int count = 32;
    auto converter = MakeConverter(64, 64, 32, 32);
    auto src = MakeSource(64, 64, QuadrantPattern(0, 0, 64, 64));
    std::vector<BufferPtr> targets;
    uint64_t last = 0;
    for (int i = 0; i < count; i++) {
        targets.push_back(D3D12Backend::AllocTarget(device.get(), 32, 32));
        const uint64_t value = converter->Convert(src.get(), targets.back().get());
        EXPECT_GT(value, last);
        last = value;
    }

    WaitFence(converter->fence(), last);
    for (int i = 0; i < count; i++) {
        SCOPED_TRACE(testing::Message() << "target " << i);
        ExpectQuadrants(ReadD3D12(targets[i].get()), 0, 0, 32, 32, 1);
    }
}
