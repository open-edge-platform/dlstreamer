/*******************************************************************************
 * Copyright (C) 2021-2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#pragma once

#include "converter_facade.h"

#include "inference_backend/image_inference.h"

#include <map>
#include <memory>
#include <vector>

class InferenceImpl;
typedef struct _GvaBaseInference GvaBaseInference;

namespace post_processing {

class PostProcessorImpl {
  protected:
    std::vector<ConverterFacade> converters;
    const std::string any_layer_name = "ANY";

  public:
    enum class ExitStatus { SUCCESS, FAIL };

    struct Initializer {
        /* image info */
        ModelImageInputInfo image_info;
        /* model info */
        std::string model_name;
        ModelOutputsInfo model_outputs;
        /* model proc info */
        std::map<std::string, GstStructure *> output_processors;
        std::map<std::string, std::vector<std::string>> labels;
        /* other */
        ConverterType converter_type;
        AttachType attach_type;
        bool use_default = true;
        double threshold = 0.5;
        bool threshold_explicitly_set = false;

        std::string custom_postproc_lib;
    };

    PostProcessorImpl(Initializer initializer);

    ExitStatus process(const OutputBlobs &, FramesWrapper &) const;

    PostProcessorImpl() = default;
    ~PostProcessorImpl() = default;

  private:
    void setDefaultConverter(GstStructure *model_proc_output, const ModelOutputsInfo &model_outputs_info,
                             ConverterType converter_type);
};
} // namespace post_processing
