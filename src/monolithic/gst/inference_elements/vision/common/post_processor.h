/*******************************************************************************
 * Copyright (C) 2021-2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#pragma once

#include <gst/gst.h>

#include "post_processor/post_processor_impl.h"
#include <atomic>
#include <vector>

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

    // `active` (one entry per frame) limits the processing to some of the frames, the others are left untouched. Empty
    // means all frames.
    PostProcessorImpl::ExitStatus process(const OutputBlobs &, InferenceFrames &, std::vector<bool> active = {}) const;

    // Safe to call from any thread, takes effect from the next inference result that is converted
    void set_confidence_threshold(double threshold) {
        threshold_override = threshold;
        post_proc_impl.setConfidenceThreshold(threshold);
    }

    // True if both post-processors give the same results for the same inference output. The only setting that can
    // differ between the elements that share a model is the confidence threshold the user set on them.
    bool has_same_settings_as(const PostProcessor &other) const {
        return threshold_override == other.threshold_override;
    }

    // TODO: temporary for test purposes
    PostProcessorImpl::Initializer get_initializer() const {
        return initializer;
    }

  private:
    PostProcessorImpl post_proc_impl;
    PostProcessorImpl::Initializer initializer;
    // The threshold set with set_confidence_threshold(), -1 while the one of the model is in use
    std::atomic<double> threshold_override{-1.0};
};

} /* namespace post_processing */
