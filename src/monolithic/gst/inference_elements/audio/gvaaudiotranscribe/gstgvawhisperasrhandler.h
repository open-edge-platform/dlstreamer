/*******************************************************************************
 * Copyright (C) 2018-2025 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#pragma once
#include "gstgvaaudiotranscribehandler.h"
#include <openvino/genai/whisper_pipeline.hpp>

/**
 * OpenVINO GenAI Whisper implementation of the audio transcription handler.
 *
 * This is the default and primary supported handler for Whisper-based
 * speech recognition using OpenVINO GenAI backend.
 */
class WhisperHandler : public GvaAudioTranscribeHandler {
  public:
    bool initialize(const std::string &model_path, const std::string &device, const std::string &language,
                    const std::string &task, bool return_timestamps) override;

    TranscriptionResult transcribe(const std::vector<float> &audio_data, GstBuffer *buf) override;

    void cleanup() override;

    std::map<std::string, std::string> get_info() const override;

  private:
    ov::genai::WhisperPipeline *pipeline = nullptr;
    ov::genai::WhisperGenerationConfig *config = nullptr;
};
