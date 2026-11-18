/*******************************************************************************
 * Copyright (C) 2018-2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#pragma once

#include "inference_backend/input_image_layer_descriptor.h"

#include "vaapi_context.h"
#include "vaapi_images.h"
#include "vaapi_utils.h"

#include <memory>
#include <mutex>

namespace InferenceBackend {

class VaApiConverter {
    VaApiContext *_context;
    // Serializes the picture sequence submitted on _context. A VA context must not be used by two threads at once;
    // each converter has its own context, so different converters do not wait for each other.
    std::mutex _convert_mutex;

  public:
    // Source and destination regions, and background color, of a VPP crop/resize under the model-proc pre-processing
    // parameters; records the transformations in image_transform_info. Also used by VaRoiGpuPreproc, which
    // pre-processes on the GPU with the same regions.
    static void SetupPipelineRegionsWithCustomParams(const InputImageLayerDesc::Ptr &pre_proc_info, uint16_t src_width,
                                                     uint16_t src_height, uint16_t dst_width, uint16_t dst_height,
                                                     VARectangle &src_surface_region, VARectangle &dst_surface_region,
                                                     VAProcPipelineParameterBuffer &pipeline_param,
                                                     const ImageTransformationParams::Ptr &image_transform_info);

    explicit VaApiConverter(VaApiContext *context);
    ~VaApiConverter() = default;

    void Convert(const Image &src, InferenceBackend::VaApiImage &va_api_dst,
                 const InputImageLayerDesc::Ptr &pre_proc_info = nullptr,
                 const ImageTransformationParams::Ptr &image_transform_info = nullptr);
};

} // namespace InferenceBackend
