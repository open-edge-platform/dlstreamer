/*******************************************************************************
 * Copyright (C) 2020-2025 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#include "vaapi_images.h"

#include "utils.h"

#include "inference_backend/image.h"
#include "vaapi_context.h"

#include <gtest/gtest.h>

using namespace InferenceBackend;

struct VAAPIImageMapTest : public testing::Test {};

TEST_F(VAAPIImageMapTest, VAAPIImageMapTestInitialization) {
    ASSERT_NO_THROW(std::unique_ptr<VaApiImageMap_VASurface>(new VaApiImageMap_VASurface()));
}

TEST_F(VAAPIImageMapTest, VAAPIImageMapTestRightInitialization) {
    auto va_image_map = std::unique_ptr<VaApiImageMap_VASurface>(new VaApiImageMap_VASurface());
    ASSERT_NE(va_image_map, nullptr);
}

TEST_F(VAAPIImageMapTest, VAAPIImageMapTestCreation) {
    ASSERT_NO_THROW(std::unique_ptr<ImageMap>(ImageMap::Create(MemoryType::VAAPI)));
    ASSERT_NO_THROW(std::unique_ptr<ImageMap>(ImageMap::Create(MemoryType::SYSTEM)));
    ASSERT_NO_THROW(std::unique_ptr<ImageMap>(ImageMap::Create(MemoryType::DMA_BUFFER)));
    ASSERT_ANY_THROW(std::unique_ptr<ImageMap>(ImageMap::Create(MemoryType::ANY)));
}

TEST_F(VAAPIImageMapTest, VAAPIImageMapTestRightCreation) {
    ASSERT_ANY_THROW(ImageMap::Create(MemoryType::ANY));
    ASSERT_NO_THROW(ImageMap::Create(MemoryType::DMA_BUFFER));
    ASSERT_NO_THROW(ImageMap::Create(MemoryType::VAAPI));
    ASSERT_NO_THROW(ImageMap::Create(MemoryType::SYSTEM));
}

TEST_F(VAAPIImageMapTest, VAAPIImageMapTestMapSystemMem) {
    auto image_map = std::unique_ptr<ImageMap>(ImageMap::Create(MemoryType::SYSTEM));
    VaApiImageMap_SystemMemory *va_sys_image_map = dynamic_cast<VaApiImageMap_SystemMemory *>(image_map.get());

    ASSERT_ANY_THROW(va_sys_image_map->Map(Image()));

    int fd;
    auto image = createSurfaceImage(fd);
    ASSERT_NO_THROW(va_sys_image_map->Map(image));
    ASSERT_NO_THROW(va_sys_image_map->Unmap());
}

TEST_F(VAAPIImageMapTest, VAAPIImageMapTestRightMapSystemMem) {
    auto image_map = std::unique_ptr<ImageMap>(ImageMap::Create(MemoryType::SYSTEM));
    VaApiImageMap_SystemMemory *va_sys_image_map = dynamic_cast<VaApiImageMap_SystemMemory *>(image_map.get());

    ASSERT_ANY_THROW(va_sys_image_map->Map(Image()));

    int fd;
    auto image = createSurfaceImage(fd);
    auto mapped_image = va_sys_image_map->Map(image);

    ASSERT_EQ(MemoryType::SYSTEM, mapped_image.type);
    ASSERT_EQ(image.height, mapped_image.height);
    ASSERT_EQ(image.width, mapped_image.width);
    ASSERT_EQ(image.format, mapped_image.format);

    va_sys_image_map->Unmap();
}

//=============================================================================================================

struct VAAPIImageTest : public testing::Test {};

TEST_F(VAAPIImageTest, VAAPIImageTestDefInitialization) {
    ASSERT_NO_THROW(std::unique_ptr<VaApiImage>(new VaApiImage()));
}

TEST_F(VAAPIImageTest, VAAPIImageTestRightDefInitialization) {
    VaApiImage va_image;
    ASSERT_EQ(va_image.context, nullptr);
    ASSERT_EQ(va_image.image_map, nullptr);
    ASSERT_EQ(va_image.image.dma_fd, -1);
    ASSERT_EQ(va_image.image.va_display, nullptr);
    ASSERT_EQ(va_image.image.va_surface_id, VA_INVALID_SURFACE);
}

TEST_F(VAAPIImageTest, VAAPIImageTestInitialization) {
    VaApiDisplayPtr va_dpy = vaApiCreateVaDisplay(0);
    auto va_context = std::unique_ptr<VaApiContext>(new VaApiContext(va_dpy));
    ASSERT_NO_THROW(std::unique_ptr<VaApiImage>(
        new VaApiImage(va_context.get(), 480, 640, FourCC::FOURCC_I420, MemoryType::DMA_BUFFER)));
    ASSERT_NO_THROW(std::unique_ptr<VaApiImage>(
        new VaApiImage(va_context.get(), 480, 640, FourCC::FOURCC_NV12, MemoryType::VAAPI)));
    ASSERT_NO_THROW(std::unique_ptr<VaApiImage>(
        new VaApiImage(va_context.get(), 480, 640, FourCC::FOURCC_NV12, MemoryType::SYSTEM)));
    ASSERT_ANY_THROW(
        std::unique_ptr<VaApiImage>(new VaApiImage(va_context.get(), 480, 640, FourCC::FOURCC_NV12, MemoryType::ANY)));
}

TEST_F(VAAPIImageTest, VAAPIImageTestRightInitialization) {
    VaApiDisplayPtr va_dpy = vaApiCreateVaDisplay(0);
    auto va_context = std::unique_ptr<VaApiContext>(new VaApiContext(va_dpy));
    const constexpr size_t width = 640;
    const constexpr size_t height = 480;
    const constexpr MemoryType mem_type = MemoryType::VAAPI;
    const constexpr FourCC format = FourCC::FOURCC_NV12;

    VaApiImage va_image(va_context.get(), width, height, format, mem_type);

    ASSERT_EQ(va_image.context, va_context.get());
    ASSERT_NE(va_image.image_map, nullptr);
    ASSERT_EQ(va_image.image.type, MemoryType::VAAPI);
    ASSERT_EQ(va_image.image.format, format);
    ASSERT_EQ(va_image.image.width, width);
    ASSERT_EQ(va_image.image.height, height);
    ASSERT_NE(va_image.image.va_display, nullptr);
    ASSERT_NE(va_image.image.va_surface_id, VA_INVALID_SURFACE);
}

TEST_F(VAAPIImageTest, VAAPIImageTestMap) {
    VaApiDisplayPtr va_dpy = vaApiCreateVaDisplay(0);
    auto va_context = std::unique_ptr<VaApiContext>(new VaApiContext(va_dpy));
    const constexpr size_t width = 640;
    const constexpr size_t height = 480;
    const constexpr MemoryType mem_type = MemoryType::VAAPI;
    const constexpr FourCC format = FourCC::FOURCC_NV12;

    VaApiImage va_image(va_context.get(), width, height, format, mem_type);

    ASSERT_NO_THROW(va_image.Map());
    ASSERT_NO_THROW(va_image.Unmap());

    ASSERT_NO_THROW(
        std::unique_ptr<VaApiImage>(new VaApiImage(va_context.get(), 480, 640, FourCC::FOURCC_NV12, MemoryType::VAAPI))
            ->Map());
    ASSERT_NO_THROW(
        std::unique_ptr<VaApiImage>(new VaApiImage(va_context.get(), 480, 640, FourCC::FOURCC_NV12, MemoryType::VAAPI))
            ->Unmap());

    ASSERT_NO_THROW(
        std::unique_ptr<VaApiImage>(new VaApiImage(va_context.get(), 480, 640, FourCC::FOURCC_NV12, MemoryType::SYSTEM))
            ->Map());
}

TEST_F(VAAPIImageTest, VAAPIImageTestRightMap) {
    VaApiDisplayPtr va_dpy = vaApiCreateVaDisplay(0);
    auto va_context = std::unique_ptr<VaApiContext>(new VaApiContext(va_dpy));
    const constexpr size_t width = 640;
    const constexpr size_t height = 480;
    const constexpr MemoryType mem_type = MemoryType::VAAPI;
    const constexpr FourCC format = FourCC::FOURCC_NV12;

    VaApiImage va_image(va_context.get(), width, height, format, mem_type);

    auto mapped_image = va_image.Map();

    ASSERT_EQ(va_image.image.height, mapped_image.height);
    ASSERT_EQ(va_image.image.width, mapped_image.width);
    ASSERT_EQ(va_image.image.format, mapped_image.format);
    ASSERT_EQ(va_image.image.type, mapped_image.type);
    ASSERT_EQ(va_image.image.va_display, mapped_image.va_display);
    ASSERT_EQ(va_image.image.va_surface_id, mapped_image.va_surface_id);
    ASSERT_EQ(va_image.image.dma_fd, mapped_image.dma_fd);
    ASSERT_EQ(va_image.image.size, mapped_image.size);
    ASSERT_EQ(va_image.image.rect.x, mapped_image.rect.x);
    ASSERT_EQ(va_image.image.rect.y, mapped_image.rect.y);
    ASSERT_EQ(va_image.image.rect.width, mapped_image.rect.width);
    ASSERT_EQ(va_image.image.rect.height, mapped_image.rect.height);

    ASSERT_NO_THROW(va_image.Unmap());
}

//=============================================================================================================

struct VAAPIImagePoolTest : public testing::Test {
    std::unique_ptr<VaApiContext> va_context = nullptr;
    VaApiImagePool::SizeParams pool_size;
    VaApiImagePool::ImageInfo image_info;

    void SetUp() {
        VaApiDisplayPtr va_dpy = vaApiCreateVaDisplay(0);
        va_context = std::unique_ptr<VaApiContext>(new VaApiContext(va_dpy));
        pool_size.num_default = 5;
        image_info = {1920, 1080, 1, FourCC::FOURCC_NV12, MemoryType::VAAPI};
    }

    void TearDown() {
    }
};

TEST_F(VAAPIImagePoolTest, VAAPIImagePoolTestInitialization) {
    ASSERT_ANY_THROW(std::unique_ptr<VaApiImagePool>(
        new VaApiImagePool(nullptr, VaApiImagePool::SizeParams(1), VaApiImagePool::ImageInfo())));
    ASSERT_ANY_THROW(std::unique_ptr<VaApiImagePool>(
        new VaApiImagePool(va_context.get(), VaApiImagePool::SizeParams(), VaApiImagePool::ImageInfo())));
    ASSERT_NO_THROW(std::unique_ptr<VaApiImagePool>(new VaApiImagePool(va_context.get(), pool_size, image_info)));
}

TEST_F(VAAPIImagePoolTest, VAAPIImagePoolTestRightInitialization) {
    auto pool = std::unique_ptr<VaApiImagePool>(new VaApiImagePool(va_context.get(), pool_size, image_info));
    ASSERT_NE(pool, nullptr);
}

TEST_F(VAAPIImagePoolTest, VAAPIImagePoolTestManipulations) {
    auto pool = std::unique_ptr<VaApiImagePool>(new VaApiImagePool(va_context.get(), pool_size, image_info));

    VaApiImage *va_image = nullptr;
    ASSERT_NO_THROW(va_image = pool->AcquireBuffer());
    ASSERT_FALSE(va_image->completed);
    ASSERT_NO_THROW(pool->ReleaseBuffer(va_image));
    ASSERT_TRUE(va_image->completed);
    ASSERT_NO_THROW(pool->Flush());

    ASSERT_ANY_THROW(pool->ReleaseBuffer(nullptr));
}
