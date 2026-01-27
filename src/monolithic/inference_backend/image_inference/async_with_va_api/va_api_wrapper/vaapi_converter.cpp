/*******************************************************************************
 * Copyright (C) 2018-2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#include "vaapi_converter.h"

#include "inference_backend/pre_proc.h"
#include "safe_arithmetic.hpp"
#include "utils.h"

#include <drm/drm_fourcc.h>

#include <va/va_drmcommon.h>

#include <cstring>
#include <stdexcept>
#include <string>

using namespace InferenceBackend;

namespace {

// Use of this function has a higher priority, but there is a bug in the driver that does not yet allow it to be used
/*VASurfaceID ConvertVASurfaceFromDifferentDriverContext2(VaDpyWrapper display1, VASurfaceID surface1,
                                                        VaDpyWrapper display2, int rt_format, uint64_t &drm_fd_out) {
    VADRMPRIMESurfaceDescriptor drm_descriptor = VADRMPRIMESurfaceDescriptor();
    VA_CALL(display1.drvVtable().vaSyncSurface(display1.drvCtx(), surface1));
    VA_CALL(display1.drvVtable().vaExportSurfaceHandle(display1.drvCtx(), surface1,
                                                       VA_SURFACE_ATTRIB_MEM_TYPE_DRM_PRIME_2,
                                                       VA_EXPORT_SURFACE_READ_ONLY, &drm_descriptor));

    if (drm_descriptor.num_objects != 1)
        throw std::invalid_argument("Unexpected objects number");
    auto object = drm_descriptor.objects[0];
    drm_fd_out = object.fd;
    VASurfaceAttrib attribs[2] = {};
    attribs[0].flags = VA_SURFACE_ATTRIB_SETTABLE;
    attribs[0].type = VASurfaceAttribMemoryType;
    attribs[0].value.type = VAGenericValueTypeInteger;
    attribs[0].value.value.i = VA_SURFACE_ATTRIB_MEM_TYPE_DRM_PRIME_2;

    attribs[1].flags = VA_SURFACE_ATTRIB_SETTABLE;
    attribs[1].type = VASurfaceAttribExternalBufferDescriptor;
    attribs[1].value.type = VAGenericValueTypePointer;
    attribs[1].value.value.p = &drm_descriptor;

    VASurfaceID surface2 = VA_INVALID_SURFACE;
    VA_CALL(display2.drvVtable().vaCreateSurfaces2(display2.drvCtx(), rt_format, drm_descriptor.width,
                                                   drm_descriptor.height, &surface2, 1, attribs, 2));
    return surface2;
}*/

VASurfaceID ConvertVASurfaceFromDifferentDriverContext(VaDpyWrapper src_display, VASurfaceID src_surface,
                                                       VaDpyWrapper dst_display, int rt_format, uint64_t &drm_fd_out) {

    VADRMPRIMESurfaceDescriptor drm_descriptor = VADRMPRIMESurfaceDescriptor();
    VA_CALL(src_display.drvVtable().vaSyncSurface(src_display.drvCtx(), src_surface));
    VA_CALL(src_display.drvVtable().vaExportSurfaceHandle(src_display.drvCtx(), src_surface,
                                                          VA_SURFACE_ATTRIB_MEM_TYPE_DRM_PRIME_2,
                                                          VA_EXPORT_SURFACE_READ_ONLY, &drm_descriptor));

    VASurfaceAttribExternalBuffers external = VASurfaceAttribExternalBuffers();
    external.width = drm_descriptor.width;
    external.height = drm_descriptor.height;
    external.pixel_format = drm_descriptor.fourcc;

    if (drm_descriptor.num_objects != 1)
        throw std::invalid_argument("Unexpected objects number");
    auto object = drm_descriptor.objects[0];
    external.num_buffers = 1;
    uint64_t drm_fd = object.fd;
    drm_fd_out = drm_fd;
    external.buffers = &drm_fd;
    external.data_size = object.size;
    external.flags = object.drm_format_modifier == DRM_FORMAT_MOD_LINEAR ? 0 : VA_SURFACE_EXTBUF_DESC_ENABLE_TILING;
    uint32_t k = 0;
    for (uint32_t i = 0; i < drm_descriptor.num_layers; i++) {
        for (uint32_t j = 0; j < drm_descriptor.layers[i].num_planes; j++) {
            external.pitches[k] = drm_descriptor.layers[i].pitch[j];
            external.offsets[k] = drm_descriptor.layers[i].offset[j];
            ++k;
        }
    }
    external.num_planes = k;

    VASurfaceAttrib attribs[2] = {};
    attribs[0].flags = VA_SURFACE_ATTRIB_SETTABLE;
    attribs[0].type = VASurfaceAttribMemoryType;
    attribs[0].value.type = VAGenericValueTypeInteger;
    attribs[0].value.value.i = VA_SURFACE_ATTRIB_MEM_TYPE_DRM_PRIME;

    attribs[1].flags = VA_SURFACE_ATTRIB_SETTABLE;
    attribs[1].type = VASurfaceAttribExternalBufferDescriptor;
    attribs[1].value.type = VAGenericValueTypePointer;
    attribs[1].value.value.p = &external;

    VASurfaceID dst_surface = VA_INVALID_SURFACE;
    VA_CALL(dst_display.drvVtable().vaCreateSurfaces2(dst_display.drvCtx(), rt_format, drm_descriptor.width,
                                                      drm_descriptor.height, &dst_surface, 1, attribs, 2));
    return dst_surface;
}

VASurfaceID ConvertDMABuf(VaDpyWrapper display, const Image &src, int rt_format) {
    if (src.type != MemoryType::DMA_BUFFER) {
        throw std::runtime_error("MemoryType=DMA_BUFFER expected");
    }

    VASurfaceAttribExternalBuffers external = VASurfaceAttribExternalBuffers();
    external.width = src.width;
    external.height = src.height;
    external.num_planes = Utils::GetPlanesCount(src.format);
    uint64_t dma_fd = src.dma_fd;
    external.buffers = &dma_fd;
    external.num_buffers = 1;
    external.pixel_format = src.format;
    external.data_size = 0;
    for (uint32_t i = 0; i < external.num_planes; i++) {
        external.pitches[i] = src.stride[i];
        external.offsets[i] = src.offsets[i];
    }
    external.data_size = src.size;

    VASurfaceAttrib attribs[2] = {};
    attribs[0].flags = VA_SURFACE_ATTRIB_SETTABLE;
    attribs[0].type = VASurfaceAttribMemoryType;
    attribs[0].value.type = VAGenericValueTypeInteger;
    attribs[0].value.value.i = VA_SURFACE_ATTRIB_MEM_TYPE_DRM_PRIME;

    attribs[1].flags = VA_SURFACE_ATTRIB_SETTABLE;
    attribs[1].type = VASurfaceAttribExternalBufferDescriptor;
    attribs[1].value.type = VAGenericValueTypePointer;
    attribs[1].value.value.p = &external;

    VASurfaceID va_surface_id;
    VA_CALL(display.drvVtable().vaCreateSurfaces2(display.drvCtx(), safe_convert<uint32_t>(rt_format), src.width,
                                                  src.height, &va_surface_id, 1, attribs, 2))

    return va_surface_id;
}

/* static VASurfaceID CreateVASurfaceFromAlignedBuffer(VADisplay dpy, Image &src) {
    if (src.type != InferenceBackend::MemoryType::SYSTEM) {
        throw std::runtime_error("MemoryType=SYSTEM expected");
    }

    VASurfaceAttribExternalBuffers external{};
    external.pixel_format = src.format;
    external.width = src.width;
    external.height = src.height;
    uintptr_t buffers[1] = {(uintptr_t)src.planes[0]};
    external.num_buffers = 1;
    external.buffers = buffers;
    external.flags = VA_SURFACE_ATTRIB_MEM_TYPE_USER_PTR;
    external.num_planes = GetPlanesCount(src.format);
    for (uint32_t i = 0; i < external.num_planes; i++) {
        external.pitches[i] = src.stride[i];
        external.offsets[i] = src.planes[i] - src.planes[0];
        external.data_size += src.stride[i] * src.height;
    }

    VASurfaceAttrib attribs[2]{};
    attribs[0].type = (VASurfaceAttribType)VASurfaceAttribMemoryType;
    attribs[0].flags = VA_SURFACE_ATTRIB_SETTABLE;
    attribs[0].value.type = VAGenericValueTypeInteger;
    attribs[0].value.value.i = VA_SURFACE_ATTRIB_MEM_TYPE_USER_PTR;

    attribs[1].type = (VASurfaceAttribType)VASurfaceAttribExternalBufferDescriptor;
    attribs[1].flags = VA_SURFACE_ATTRIB_SETTABLE;
    attribs[1].value.type = VAGenericValueTypePointer;
    attribs[1].value.value.p = (void *)&external;

    VASurfaceID va_surface_id;
    VA_CALL(vaCreateSurfaces(dpy, FourCc2RTFormat(src.format), src.width, src.height, &va_surface_id, 1, attribs,
2))

    return va_surface_id;
}*/

} // anonymous namespace

std::mutex VaApiConverter::_convert_mutex;

VaApiConverter::VaApiConverter(VaApiContext *context) : _context(context) {
    if (!context)
        throw std::runtime_error("VaApiContext is null. VaConverter requires not nullptr context.");
}

void VaApiConverter::SetupPipelineRegionsWithCustomParams(const InputImageLayerDesc::Ptr &pre_proc_info,
                                                          uint16_t src_width, uint16_t src_height, uint16_t dst_width,
                                                          uint16_t dst_height, VARectangle &src_surface_region,
                                                          VARectangle &dst_surface_region,
                                                          VAProcPipelineParameterBuffer &pipeline_param,
                                                          const ImageTransformationParams::Ptr &image_transform_info) {
    // Padding preparations
    uint16_t padding_x = 0;
    uint16_t padding_y = 0;
    uint32_t background_color = 0xff000000;
    if (pre_proc_info->doNeedPadding() && (!image_transform_info || !image_transform_info->WasPadding())) {
        const auto &padding = pre_proc_info->getPadding();
        padding_x = safe_convert<uint16_t>(padding.stride_x);
        padding_y = safe_convert<uint16_t>(padding.stride_y);
        const auto &fill_value = padding.fill_value;
        background_color |=
            static_cast<uint32_t>(fill_value.at(0) * pow(2, 16) + fill_value.at(1) * pow(2, 8) + fill_value.at(2));
    }

    dst_surface_region.x = safe_convert<int16_t>(padding_x);
    dst_surface_region.y = safe_convert<int16_t>(padding_y);

    if (padding_x * 2 > dst_width || padding_y * 2 > dst_height) {
        throw std::out_of_range("Invalid padding in relation to size");
    }

    uint16_t input_width_except_padding = dst_width - (padding_x * 2);
    uint16_t input_height_except_padding = dst_height - (padding_y * 2);

    // Resize preparations
    double resize_scale_param_x = 1;
    double resize_scale_param_y = 1;
    if (pre_proc_info->doNeedResize() && (src_surface_region.width != input_width_except_padding ||
                                          src_surface_region.height != input_height_except_padding)) {
        double additional_crop_scale_param = 1;
        if (pre_proc_info->doNeedCrop() && pre_proc_info->doNeedResize()) {
            additional_crop_scale_param = 1.125;
        }

        if (src_surface_region.width)
            resize_scale_param_x = safe_convert<double>(input_width_except_padding) / src_surface_region.width;

        if (src_surface_region.height)
            resize_scale_param_y = safe_convert<double>(input_height_except_padding) / src_surface_region.height;

        if (pre_proc_info->getResizeType() == InputImageLayerDesc::Resize::ASPECT_RATIO) {
            resize_scale_param_x = resize_scale_param_y = std::min(resize_scale_param_x, resize_scale_param_y);
        }

        resize_scale_param_x *= additional_crop_scale_param;
        resize_scale_param_y *= additional_crop_scale_param;

        // Resize in future pipeline
        dst_surface_region.width = safe_convert<uint16_t>(src_surface_region.width * resize_scale_param_x + 0.5);
        dst_surface_region.height = safe_convert<uint16_t>(src_surface_region.height * resize_scale_param_y + 0.5);

        if (image_transform_info)
            image_transform_info->ResizeHasDone(resize_scale_param_x, resize_scale_param_y);
    }

    // Crop preparations
    if (pre_proc_info->doNeedCrop() && (dst_surface_region.width != input_width_except_padding ||
                                        dst_surface_region.height != input_height_except_padding)) {
        uint16_t cropped_border_x = 0;
        uint16_t cropped_border_y = 0;

        if (dst_surface_region.width > input_width_except_padding)
            cropped_border_x = dst_surface_region.width - input_width_except_padding;
        if (dst_surface_region.height > input_height_except_padding)
            cropped_border_y = dst_surface_region.height - input_height_except_padding;

        uint16_t cropped_width = dst_surface_region.width - cropped_border_x;
        uint16_t cropped_height = dst_surface_region.height - cropped_border_y;

        if (pre_proc_info->getCropType() == InputImageLayerDesc::Crop::CENTRAL_RESIZE) {
            uint16_t crop_size = std::min(src_width, src_height);
            uint16_t startX = (src_width - crop_size) / 2;
            uint16_t startY = (src_height - crop_size) / 2;

            src_surface_region.x = safe_convert<int16_t>(startX);
            src_surface_region.y = safe_convert<int16_t>(startY);
            src_surface_region.width = crop_size;
            src_surface_region.height = crop_size;

            dst_surface_region.width = input_width_except_padding;
            dst_surface_region.height = input_height_except_padding;

            if (image_transform_info)
                image_transform_info->CropHasDone(startX, startY);
        } else {
            switch (pre_proc_info->getCropType()) {
            case InputImageLayerDesc::Crop::CENTRAL:
                cropped_border_x /= 2;
                cropped_border_y /= 2;
                break;
            case InputImageLayerDesc::Crop::TOP_LEFT:
                cropped_border_x = 0;
                cropped_border_y = 0;
                break;
            case InputImageLayerDesc::Crop::TOP_RIGHT:
                cropped_border_y = 0;
                break;
            case InputImageLayerDesc::Crop::BOTTOM_LEFT:
                cropped_border_x = 0;
                break;
            case InputImageLayerDesc::Crop::BOTTOM_RIGHT:
                break;
            default:
                throw std::runtime_error("Unknown crop format.");
            }

            // Should have this size after src_crop & src_resize
            dst_surface_region.width = cropped_width;
            dst_surface_region.height = cropped_height;

            if (image_transform_info)
                image_transform_info->CropHasDone(cropped_border_x, cropped_border_y);

            cropped_border_x = safe_convert<uint16_t>(safe_convert<double>(cropped_border_x) / resize_scale_param_x);
            cropped_border_y = safe_convert<uint16_t>(safe_convert<double>(cropped_border_y) / resize_scale_param_y);
            cropped_width = safe_convert<uint16_t>(safe_convert<double>(cropped_width) / resize_scale_param_x);
            cropped_height = safe_convert<uint16_t>(safe_convert<double>(cropped_height) / resize_scale_param_y);

            // Actualy we crop src before resize to get 1 preproc pipeline instead 2 (src->dst_resized + dst_resized ->
            // cropped)
            src_surface_region.x += cropped_border_x;
            src_surface_region.y += cropped_border_y;
            src_surface_region.width = cropped_width;
            src_surface_region.height = cropped_height;
        }
    }

    // Add padding for The padding and padding from aspect-ratio resize
    dst_surface_region.x = (dst_width - dst_surface_region.width) / 2;
    dst_surface_region.y = (dst_height - dst_surface_region.height) / 2;

    if (image_transform_info)
        image_transform_info->PaddingHasDone(safe_convert<size_t>(dst_surface_region.x),
                                             safe_convert<size_t>(dst_surface_region.y));

    pipeline_param.output_region = &dst_surface_region;
    pipeline_param.output_background_color = background_color;
}

void VaApiConverter::Convert(const Image &src, VaApiImage &va_api_dst, const InputImageLayerDesc::Ptr &pre_proc_info,
                             const ImageTransformationParams::Ptr &image_transform_info) {

    VASurfaceID src_surface = VA_INVALID_SURFACE;
    Image &dst = va_api_dst.image;

    uint64_t fd = 0;
    bool owns_src_surface = false;
    if (src.type == MemoryType::VAAPI) {
        if (src.va_display != dst.va_display) {
            src_surface =
                ConvertVASurfaceFromDifferentDriverContext(VaDpyWrapper::fromHandle(src.va_display), src.va_surface_id,
                                                           _context->Display(), _context->RTFormat(), fd);
            owns_src_surface = true;
        } else {
            src_surface = src.va_surface_id;
        }
    } else if (src.type == MemoryType::DMA_BUFFER) {
        src_surface = ConvertDMABuf(_context->Display(), src, _context->RTFormat());
        owns_src_surface = true;
    } else {
        throw std::runtime_error("VaApiConverter::Convert: unsupported MemoryType.");
    }

    VAProcPipelineParameterBuffer pipeline_param;
    std::memset(&pipeline_param, 0, sizeof(pipeline_param));

    pipeline_param.surface = src_surface;

    // Scale and csc mode
    pipeline_param.filter_flags = va_api_dst.scaling_flags;

    // Set the pipeline_flags respectively along with filter_flags
    pipeline_param.pipeline_flags = 0;
    if (pipeline_param.filter_flags == VA_FILTER_SCALING_FAST) {
        pipeline_param.pipeline_flags = VA_PROC_PIPELINE_FAST;
    }

    // Crop ROI
    VARectangle src_surface_region = {.x = safe_convert<int16_t>(src.rect.x),
                                      .y = safe_convert<int16_t>(src.rect.y),
                                      .width = safe_convert<uint16_t>(src.rect.width),
                                      .height = safe_convert<uint16_t>(src.rect.height)};
    if (src_surface_region.width > 0 && src_surface_region.height > 0)
        pipeline_param.surface_region = &src_surface_region;

    // Resize to this Rect
    VARectangle dst_surface_region = {.x = static_cast<int16_t>(0),
                                      .y = static_cast<int16_t>(0),
                                      .width = src_surface_region.width,
                                      .height = src_surface_region.height};

    if (pre_proc_info && pre_proc_info->isDefined()) {
        SetupPipelineRegionsWithCustomParams(pre_proc_info, safe_convert<uint16_t>(src.width),
                                             safe_convert<uint16_t>(src.height), safe_convert<uint16_t>(dst.width),
                                             safe_convert<uint16_t>(dst.height), src_surface_region, dst_surface_region,
                                             pipeline_param, image_transform_info);

    } else {
        // Just a resize in that case
        dst_surface_region = {.x = static_cast<int16_t>(0),
                              .y = static_cast<int16_t>(0),
                              .width = safe_convert<uint16_t>(dst.width),
                              .height = safe_convert<uint16_t>(dst.height)};

        pipeline_param.output_region = &dst_surface_region;
    }

    // execute pre-proc pipeline
    auto context = _context->Display().drvCtx();
    VABufferID pipeline_param_buf_id = VA_INVALID_ID;
    VA_CALL(_context->Display().drvVtable().vaCreateBuffer(context, _context->Id(), VAProcPipelineParameterBufferType,
                                                           sizeof(pipeline_param), 1, &pipeline_param,
                                                           &pipeline_param_buf_id));

    {
        // These operations can't be called asynchronously from different threads
        // SEGFAULT observed on media driver 21.4.1, 21.2.3, 21.1.3
        // TODO: investigate further
        std::lock_guard<std::mutex> lock(_convert_mutex);
        VA_CALL(_context->Display().drvVtable().vaBeginPicture(context, _context->Id(), dst.va_surface_id));
        VA_CALL(_context->Display().drvVtable().vaRenderPicture(context, _context->Id(), &pipeline_param_buf_id, 1));
        VA_CALL(_context->Display().drvVtable().vaEndPicture(context, _context->Id()));
    }

    VA_CALL(_context->Display().drvVtable().vaDestroyBuffer(context, pipeline_param_buf_id));

    if (owns_src_surface)
        VA_CALL(_context->Display().drvVtable().vaDestroySurfaces(context, &src_surface, 1));

    if (src.type == MemoryType::VAAPI && owns_src_surface)
        if (close(fd) == -1)
            throw std::runtime_error("VaApiConverter::Convert: close fd failed.");
}
