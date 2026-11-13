/*******************************************************************************
 * Copyright (C) 2018-2025 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#pragma once
#include <gst/gst.h>
#include <map>
#include <string>
#include <vector>

/**
 * Structure to hold transcription results with confidence score.
 */
struct TranscriptionResult {
    std::string text; // The transcribed text
    float confidence; // Confidence score (0.0 to 1.0)

    // Constructor for convenience
    TranscriptionResult(const std::string &txt = "", float conf = 1.0f) : text(txt), confidence(conf) {
    }
};

/**
 * Base class for audio transcription handlers.
 *
 * This interface allows users to implement custom model inference handlers
 * for different types of speech recognition models. Currently, Whisper is
 * the primary supported model type, but users can extend this interface
 * to support other models based on their interest and requirements.
 *
 * To implement a custom handler:
 * 1. Inherit from this class
 * 2. Implement all pure virtual methods
 * 3. Register your handler in the main transcription element
 * 4. Set model_type parameter to your custom type name
 */
class GvaAudioTranscribeHandler {
  public:
    virtual ~GvaAudioTranscribeHandler() = default;

    /**
     * Initialize the handler with model and configuration parameters.
     * @param model_path Path to the model (directory for Whisper, file/path for custom models)
     * @param device Device to use for inference (CPU, GPU, etc.)
     * @param language Language code for transcription
     * @param task Task type (transcribe, translate, etc.)
     * @param return_timestamps Whether to return timestamps with transcription
     * @return true if initialization succeeded, false otherwise
     */
    virtual bool initialize(const std::string &model_path, const std::string &device, const std::string &language,
                            const std::string &task, bool return_timestamps) = 0;

    /**
     * Perform transcription on audio data.
     * @param audio_data Vector of normalized float audio samples (16kHz, mono)
     * @param buf GStreamer buffer containing the audio data (for metadata)
     * @return TranscriptionResult containing transcribed text and confidence score
     */
    virtual TranscriptionResult transcribe(const std::vector<float> &audio_data, GstBuffer *buf) = 0;

    /**
     * Clean up resources and shut down the handler.
     */
    virtual void cleanup() = 0;

    /**
     * Get handler-specific information (optional override).
     * @return Map of key-value pairs with handler information
     */
    virtual std::map<std::string, std::string> get_info() const {
        return {{"handler_type", "unknown"}, {"status", "active"}};
    }
};
