/*******************************************************************************
 * Copyright (C) 2021-2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#pragma once

#include <gst/gst.h>

#include "post_processor/post_processor_impl.h"

class InferenceImpl;
typedef struct _GvaBaseInference GvaBaseInference;

namespace post_processing {

class PostProcessor {
  public:
    enum class ModelProcOutputsValidationResult { OK, USE_DEFAULT, FAIL };

    PostProcessor(InferenceImpl *, GvaBaseInference *);
    PostProcessor(size_t image_width, size_t image_height, size_t batch_size, const std::string &model_proc_path,
                  const std::string &model_name, const ModelOutputsInfo &, ConverterType, double threshold,
                  const std::string &labels);

    PostProcessorImpl::ExitStatus process(const OutputBlobs &, InferenceFrames &) const;

    // Safe to call from any thread, takes effect from the next inference result that is converted
    void set_confidence_threshold(double threshold) const {
        post_proc_impl.setConfidenceThreshold(threshold);
    }

    // TODO: temporary for test purposes
    PostProcessorImpl::Initializer get_initializer() const {
        return initializer;
    }

  private:
    PostProcessorImpl post_proc_impl;
    PostProcessorImpl::Initializer initializer;
};

} /* namespace post_processing */
