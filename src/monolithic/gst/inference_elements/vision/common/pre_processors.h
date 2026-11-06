/*******************************************************************************
 * Copyright (C) 2018-2020 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#pragma once

#include <processor_types.h>

#ifdef __cplusplus

#include <inference_backend/image_inference.h>

std::map<std::string, InferenceBackend::InputLayerDesc::Ptr>
GetInputPreprocessors(const std::shared_ptr<InferenceBackend::ImageInference> &inference,
                      const std::vector<ModelInputProcessorInfo::Ptr> &model_input_processor_info,
                      GstVideoRegionOfInterestMeta *roi);

extern "C" {

#endif // __cplusplus

extern InputPreprocessorsFactory GET_INPUT_PREPROCESSORS;

#ifdef __cplusplus
}
#endif // __cplusplus
