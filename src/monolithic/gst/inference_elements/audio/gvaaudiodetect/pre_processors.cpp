/*******************************************************************************
 * Copyright (C) 2018-2025 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#include "pre_processors.h"
#include "gstgvaaudiodetect.h"

#include <safe_arithmetic.hpp>

#include <functional>
#include <limits.h>
#include <math.h>
#include <numeric>
#include <vector>

std::vector<float> GetNormalizedSamples(AudioInferenceFrame *frame) {
    if (!frame || frame->samples.empty())
        throw std::runtime_error("Invalid AudioInferenceFrame object");

    const auto samples_size = frame->samples.size();
    float mean = accumulate(frame->samples.begin(), frame->samples.end(), 0) / safe_convert<float>(samples_size);
    float sq_sum = std::inner_product(frame->samples.begin(), frame->samples.end(), frame->samples.begin(), 0.0);
    float std_dev = std::sqrt((sq_sum / safe_convert<float>(samples_size)) - (mean * mean));
    std::vector<float> normalized_samples(samples_size);
    transform(frame->samples.begin(), frame->samples.end(), normalized_samples.begin(),
              [mean, std_dev](float v) { return ((v - mean) / (std_dev + 1e-15)); });
    return normalized_samples;
}

int GetNumberOfSamplesRequired(GvaAudioBaseInference *audio_base_inference) {
    GvaAudioDetect *gvaaudiodetect = GVA_AUDIO_DETECT(audio_base_inference);
    if (!gvaaudiodetect)
        throw std::invalid_argument("GvaAudioDetect is null");

    return gvaaudiodetect->req_num_samples;
}

AudioPreProcFunction GET_NORMALIZED_SAMPLES = GetNormalizedSamples;
AudioNumOfSamplesRequired GET_NUM_OF_SAMPLES_REQUIRED = GetNumberOfSamplesRequired;
