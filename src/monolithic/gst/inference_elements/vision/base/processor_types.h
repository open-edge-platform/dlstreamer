/*******************************************************************************
 * Copyright (C) 2019-2025 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#pragma once

#include "gst/gstbuffer.h"
#ifdef __cplusplus

#include "common/post_processor.h"
#include "inference_backend/image_inference.h"
#include "inference_backend/input_image_layer_descriptor.h"
#include "input_model_preproc.h"

#include <gst/gst.h>
#include <gst/video/video.h>

#include <functional>

struct _GvaBaseInference;
typedef struct _GvaBaseInference GvaBaseInference;

struct InferenceFrame {
    GstBuffer *buffer;
    GstVideoRegionOfInterestMeta roi;
    std::vector<GstStructure *> roi_classifications; // length equals to output layers count
    GvaBaseInference *gva_base_inference;
    GstVideoInfo *info;

    InferenceBackend::ImageTransformationParams::Ptr image_transform_info = nullptr;

    InferenceFrame() = default;
    InferenceFrame(const InferenceFrame &) = delete;
    InferenceFrame &operator=(const InferenceFrame &rhs) = delete;
    ~InferenceFrame() {
        if (info) {
            gst_video_info_free(info);
            info = nullptr;
        }
    }
};

using InputPreprocessingFunction = std::function<void(const InferenceBackend::InputBlob::Ptr &)>;

typedef InputPreprocessingFunction (*InputPreprocessingFunctionGetter)(
    const std::shared_ptr<InferenceBackend::ImageInference> &inference, GstStructure *preproc,
    GstVideoRegionOfInterestMeta *roi_meta);
typedef std::map<std::string, InferenceBackend::InputLayerDesc::Ptr> (*InputPreprocessorsFactory)(
    const std::shared_ptr<InferenceBackend::ImageInference> &inference,
    const std::vector<ModelInputProcessorInfo::Ptr> &model_input_processor_info, GstVideoRegionOfInterestMeta *roi);
typedef void (*PreProcFunction)(GstStructure *preproc, InferenceBackend::Image &image);
typedef bool (*FilterROIFunction)(GvaBaseInference *gva_base_inference, guint64 current_num_frame, GstBuffer *buffer,
                                  GstVideoRegionOfInterestMeta *roi);

using PostProcessorExitStatus = post_processing::PostProcessorImpl::ExitStatus;
using PostProcessor = post_processing::PostProcessor;

#else // __cplusplus

typedef void *PreProcFunction;
typedef void *InputPreprocessorsFactory;
typedef void *InputPreprocessingFunctionGetter;
typedef struct PostProcessor PostProcessor;
typedef struct PostProcessorExitStatus PostProcessorExitStatus;
typedef void *FilterROIFunction;

#endif // __cplusplus
