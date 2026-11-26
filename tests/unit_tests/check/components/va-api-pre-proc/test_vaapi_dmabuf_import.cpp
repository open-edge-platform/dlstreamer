/*******************************************************************************
 * Copyright (C) 2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

// DMA-BUF input of the VA pre-processing. A VA surface exported as a DMA-BUF is usually tiled, and the chroma plane
// of an NV12 surface starts after a padded height. Both must reach the import: the DMA-BUF has to convert to the
// same pixels as the surface itself.

#include "inference_backend/buffer_mapper.h"
#include "vaapi_context.h"
#include "vaapi_converter.h"
#include "vaapi_images.h"

#include <gst/allocators/allocators.h>
#include <gst/gst.h>
#include <gst/video/video.h>
#include <gtest/gtest.h>
#include <va/va_drmcommon.h>

#include <drm/drm_fourcc.h>

#include <unistd.h>

#include <cstring>
#include <memory>
#include <vector>

using namespace InferenceBackend;

namespace {

// A height that is not a multiple of the surface alignment, so the chroma plane does not follow the luma rows
constexpr uint32_t width = 320;
constexpr uint32_t height = 180;

class VaApiDmaBufImportTest : public testing::Test {
  protected:
    VaApiDisplayPtr va_display;
    std::unique_ptr<VaApiContext> va_context;
    VASurfaceID surface = VA_INVALID_SURFACE;
    VADRMPRIMESurfaceDescriptor prime = {};

    void SetUp() override {
        va_display = vaApiCreateVaDisplay(0);
        va_context = std::make_unique<VaApiContext>(va_display);
        VaDpyWrapper display = va_context->Display();

        VASurfaceAttrib attrib = {};
        attrib.type = VASurfaceAttribPixelFormat;
        attrib.flags = VA_SURFACE_ATTRIB_SETTABLE;
        attrib.value.type = VAGenericValueTypeInteger;
        attrib.value.value.i = VA_FOURCC_NV12;
        VA_CALL(display.drvVtable().vaCreateSurfaces2(display.drvCtx(), VA_RT_FORMAT_YUV420, width, height, &surface, 1,
                                                      &attrib, 1));
        UploadPattern(display);

        VA_CALL(display.drvVtable().vaExportSurfaceHandle(
            display.drvCtx(), surface, VA_SURFACE_ATTRIB_MEM_TYPE_DRM_PRIME_2,
            VA_EXPORT_SURFACE_READ_ONLY | VA_EXPORT_SURFACE_COMPOSED_LAYERS, &prime));
        ASSERT_EQ(prime.num_objects, 1u);
        ASSERT_EQ(prime.num_layers, 1u);
        ASSERT_EQ(prime.layers[0].num_planes, 2u);
    }

    void TearDown() override {
        for (uint32_t i = 0; i < prime.num_objects; i++)
            close(prime.objects[i].fd);
        if (surface != VA_INVALID_SURFACE) {
            VaDpyWrapper display = va_context->Display();
            display.drvVtable().vaDestroySurfaces(display.drvCtx(), &surface, 1);
        }
    }

    // Gradients, so that a wrong tiling, stride or plane offset changes the pixels
    void UploadPattern(VaDpyWrapper display) {
        VAImageFormat format = {};
        format.fourcc = VA_FOURCC_NV12;
        format.byte_order = VA_LSB_FIRST;
        format.bits_per_pixel = 12;
        VAImage image = {};
        VA_CALL(display.drvVtable().vaCreateImage(display.drvCtx(), &format, width, height, &image));
        void *data = nullptr;
        VA_CALL(display.drvVtable().vaMapBuffer(display.drvCtx(), image.buf, &data));
        auto *bytes = static_cast<uint8_t *>(data);
        for (uint32_t y = 0; y < height; y++)
            for (uint32_t x = 0; x < width; x++)
                bytes[image.offsets[0] + y * image.pitches[0] + x] = static_cast<uint8_t>(16 + (x * 7 + y * 3) % 220);
        for (uint32_t y = 0; y < height / 2; y++)
            for (uint32_t x = 0; x < width / 2; x++) {
                bytes[image.offsets[1] + y * image.pitches[1] + 2 * x] = static_cast<uint8_t>(16 + (x * 5) % 224);
                bytes[image.offsets[1] + y * image.pitches[1] + 2 * x + 1] = static_cast<uint8_t>(16 + (y * 9) % 224);
            }
        VA_CALL(display.drvVtable().vaUnmapBuffer(display.drvCtx(), image.buf));
        VA_CALL(display.drvVtable().vaPutImage(display.drvCtx(), surface, image.image_id, 0, 0, width, height, 0, 0,
                                               width, height));
        VA_CALL(display.drvVtable().vaDestroyImage(display.drvCtx(), image.image_id));
        VA_CALL(display.drvVtable().vaSyncSurface(display.drvCtx(), surface));
    }

    Image SurfaceImage() const {
        Image image = {};
        image.type = MemoryType::VAAPI;
        image.format = FOURCC_NV12;
        image.width = width;
        image.height = height;
        image.va_display = va_context->DisplayRaw();
        image.va_surface_id = surface;
        image.rect = {0, 0, width, height};
        return image;
    }

    Image DmaBufImage() const {
        Image image = {};
        image.type = MemoryType::DMA_BUFFER;
        image.format = FOURCC_NV12;
        image.width = width;
        image.height = height;
        image.dma_fd = prime.objects[0].fd;
        image.drm_format_modifier = prime.objects[0].drm_format_modifier;
        image.size = prime.objects[0].size;
        for (uint32_t i = 0; i < 2; i++) {
            image.stride[i] = prime.layers[0].pitch[i];
            image.offsets[i] = prime.layers[0].offset[i];
        }
        image.rect = {0, 0, width, height};
        return image;
    }

    // Converts the image (its rect) to BGRX of the given size and reads it back
    std::vector<uint8_t> ToBgrx(const Image &src, uint32_t dst_width = width, uint32_t dst_height = height) {
        VaApiConverter converter(va_context.get());
        VaApiImage dst(va_context.get(), dst_width, dst_height, FOURCC_BGRX, MemoryType::SYSTEM);
        converter.Convert(src, dst);
        VaDpyWrapper display = va_context->Display();
        VA_CALL(display.drvVtable().vaSyncSurface(display.drvCtx(), dst.image.va_surface_id));
        Image mapped = dst.Map();
        std::vector<uint8_t> pixels(dst_width * dst_height * 4);
        for (uint32_t y = 0; y < dst_height; y++)
            std::memcpy(pixels.data() + y * dst_width * 4, mapped.planes[0] + y * mapped.stride[0], dst_width * 4);
        dst.Unmap();
        return pixels;
    }
};

TEST_F(VaApiDmaBufImportTest, DmaBufConvertsLikeTheSurface) {
    EXPECT_EQ(ToBgrx(DmaBufImage()), ToBgrx(SurfaceImage()));
}

TEST_F(VaApiDmaBufImportTest, DmaBufScalesLikeTheSurface) {
    EXPECT_EQ(ToBgrx(DmaBufImage(), 64, 48), ToBgrx(SurfaceImage(), 64, 48));
}

TEST_F(VaApiDmaBufImportTest, DmaBufRoiConvertsLikeTheSurface) {
    // An odd-sized ROI at odd coordinates, scaled, as a model input from a detection
    Image dma_buf = DmaBufImage();
    Image va_surface = SurfaceImage();
    dma_buf.rect = va_surface.rect = {37, 21, 101, 77};
    EXPECT_EQ(ToBgrx(dma_buf, 64, 48), ToBgrx(va_surface, 64, 48));
}

// The path of the inference elements: a DMABuf GstBuffer with a GstVideoMeta, and the modifier from the caps
TEST_F(VaApiDmaBufImportTest, GstDmaBufMapperKeepsModifierAndPlaneLayout) {
    gst_init(nullptr, nullptr);
    const uint64_t modifier = prime.objects[0].drm_format_modifier;

    GstAllocator *allocator = gst_dmabuf_allocator_new();
    GstMemory *memory = gst_dmabuf_allocator_alloc(allocator, dup(prime.objects[0].fd), prime.objects[0].size);
    gst_object_unref(allocator);
    ASSERT_NE(memory, nullptr);
    GstBuffer *buffer = gst_buffer_new();
    gst_buffer_append_memory(buffer, memory);
    gsize offsets[GST_VIDEO_MAX_PLANES] = {prime.layers[0].offset[0], prime.layers[0].offset[1]};
    gint strides[GST_VIDEO_MAX_PLANES] = {static_cast<gint>(prime.layers[0].pitch[0]),
                                          static_cast<gint>(prime.layers[0].pitch[1])};
    gst_buffer_add_video_meta_full(buffer, GST_VIDEO_FRAME_FLAG_NONE, GST_VIDEO_FORMAT_NV12, width, height, 2, offsets,
                                   strides);

    // What the inference elements derive from 'video/x-raw(memory:DMABuf), format=DMA_DRM, drm-format=NV12:<modifier>'
    gchar *drm_format = gst_video_dma_drm_fourcc_to_string(DRM_FORMAT_NV12, modifier);
    GstCaps *caps = gst_caps_new_simple("video/x-raw", "format", G_TYPE_STRING, "DMA_DRM", "width", G_TYPE_INT,
                                        static_cast<int>(width), "height", G_TYPE_INT, static_cast<int>(height),
                                        "drm-format", G_TYPE_STRING, drm_format, nullptr);
    g_free(drm_format);
    gst_caps_set_features(caps, 0, gst_caps_features_new(GST_CAPS_FEATURE_MEMORY_DMABUF, nullptr));
    GstVideoInfoDmaDrm drm_info;
    ASSERT_TRUE(gst_video_info_dma_drm_from_caps(&drm_info, caps));
    gst_caps_unref(caps);
    GstVideoInfo video_info;
    ASSERT_TRUE(gst_video_info_dma_drm_to_video_info(&drm_info, &video_info));
    ASSERT_EQ(drm_info.drm_modifier, modifier);

    auto mapper = BufferMapperFactory::createMapper(MemoryType::DMA_BUFFER, &video_info, nullptr, modifier);
    ImagePtr image = mapper->map(buffer, GST_MAP_READ);
    EXPECT_EQ(image->drm_format_modifier, modifier);
    for (uint32_t i = 0; i < 2; i++) {
        EXPECT_EQ(image->stride[i], prime.layers[0].pitch[i]) << "plane " << i;
        EXPECT_EQ(image->offsets[i], prime.layers[0].offset[i]) << "plane " << i;
    }
    image->rect = {0, 0, width, height};
    EXPECT_EQ(ToBgrx(*image), ToBgrx(SurfaceImage()));

    image.reset();
    mapper.reset();
    gst_buffer_unref(buffer);
}

} // namespace
