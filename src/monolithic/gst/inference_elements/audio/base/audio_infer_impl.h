/*******************************************************************************
 * Copyright (C) 2018-2021 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#pragma once

#include "gva_audio_base_inference.h"
#include <vector>

class AudioInferImpl {
  public:
    AudioInferImpl(GvaAudioBaseInference *audio_base_inference);
    virtual ~AudioInferImpl();
    void fillAudioFrame(AudioInferenceFrame *frame);
    bool readyToInfer();
    void addSamples(int16_t *samples, uint32_t num_samples, uint64_t start_time);
    void setNumOfSamplesToSlide();

  private:
    void setStartTime(uint64_t start_time);

  private:
    std::vector<float> audioData;
    std::vector<uint64_t> inferenceStartTime;
    bool startTimeSet = false;
    GvaAudioBaseInference *audio_base_inference;
    uint32_t sliding_samples = 0;
};
