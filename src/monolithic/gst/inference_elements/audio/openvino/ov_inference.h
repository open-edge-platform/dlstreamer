/*******************************************************************************
 * Copyright (C) 2023-2024 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#pragma once

#include "base/audio_processor_types.h"
#include "dlstreamer/frame_info.h"
#include "inference_backend/image_inference.h"

#include <math.h>
#include <memory>
#include <openvino/openvino.hpp>
#include <string>
#include <vector>

class OpenVINOAudioInference {
  public:
    OpenVINOAudioInference(const std::string &model_path, const std::string &device, AudioInferenceOutput &infOutput);
    virtual ~OpenVINOAudioInference() = default;
    std::vector<uint8_t> convertFloatToU8(std::vector<float> &normalized_samples);
    void setInputBlob(void *buffer_ptr, int dma_fd = 0);
    void infer();
    AudioInferenceOutput *getInferenceOutput();

  private:
    void CreateRemoteContext(const std::string &device);

    ov::Core _core;
    std::shared_ptr<ov::Model> _model;
    ov::CompiledModel _compiled_model;
    ov::InferRequest _infer_request;

    dlstreamer::FrameInfo _model_input_info;

    // InferenceEngine::RemoteContext::Ptr remote_context;
    AudioInferenceOutput infOut;
};
