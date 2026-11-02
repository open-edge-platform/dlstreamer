/*******************************************************************************
 * Copyright (C) 2018 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#include "audio_infer_impl.h"
#include "audio_defs.h"
#include <cmath>
#include <stdexcept>

AudioInferImpl::~AudioInferImpl() {
}

AudioInferImpl::AudioInferImpl(GvaAudioBaseInference *audio_base_inference) {
    if (!audio_base_inference)
        throw std::invalid_argument("GvaAudioBaseInference is null");

    this->audio_base_inference = audio_base_inference;
    setNumOfSamplesToSlide();
}

void AudioInferImpl::addSamples(int16_t *samples, uint32_t num_samples, uint64_t start_time) {
    if (!samples || num_samples == 0)
        throw std::runtime_error("Invalid Input data");

    setStartTime(start_time);
    audioData.insert(audioData.end(), &samples[0], &samples[num_samples]);
}

bool AudioInferImpl::readyToInfer() {
    return (audioData.size() == audio_base_inference->sample_length);
}

void AudioInferImpl::fillAudioFrame(AudioInferenceFrame *frame) {
    if (!frame)
        throw std::invalid_argument("AudioInferenceFrame is null");
    if (inferenceStartTime.empty())
        throw std::runtime_error("Inference start time is not set");

    frame->samples = audioData;
    frame->startTime = inferenceStartTime.front();
    frame->endTime = inferenceStartTime.front() + (audioData.size() * MULTIPLIER);
    if (sliding_samples < audio_base_inference->sample_length) {
        audioData.erase(audioData.begin(), audioData.begin() + sliding_samples);
        inferenceStartTime.erase(inferenceStartTime.begin());
    } else {
        audioData.clear();
        inferenceStartTime.clear();
    }
    startTimeSet = false;
}

void AudioInferImpl::setStartTime(uint64_t start_time) {
    if (sliding_samples < audio_base_inference->sample_length && ((audioData.size() % sliding_samples) == 0)) {
        startTimeSet = false;
    }
    if (!startTimeSet) {
        inferenceStartTime.push_back(start_time);
        startTimeSet = true;
    }
}

void AudioInferImpl::setNumOfSamplesToSlide() {
    sliding_samples = std::round(audio_base_inference->sliding_length * SAMPLE_AUDIO_RATE);
}
