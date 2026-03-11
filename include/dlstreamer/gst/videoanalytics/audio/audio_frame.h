/*******************************************************************************
 * Copyright (C) 2018-2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

/**
 * @file audio_frame.h
 * @brief This file contains GVA::AudioFrame class to control particular inferenced segment and attached
 * GVA::AudioEvent and GVA::Tensor instances.
 */

#pragma once

#include "audio_event.h"

#include "metadata/gva_json_meta.h"
#include "metadata/gva_tensor_meta.h"

#include <gst/audio/audio.h>
#include <gst/audio/gstaudiometa.h>
#include <gst/gstbuffer.h>

#include <algorithm>
#include <assert.h>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace GVA {

/**
 * @brief This class represents audio frame - object for working with AudioEvent and Tensor objects which
 * belong to this audio frame . AudioEvent describes detected object (segments) and its Tensor
 * objects (inference results on AudioEvent level). Tensor describes inference results on AudioFrame level.
 * AudioFrame also provides access to underlying GstBuffer and GstAudioInfo describing frame's audio information (such
 * as format, channels, etc.).
 */

class AudioFrame {
  protected:
    /**
     * @brief GstBuffer with inference results metadata attached (Gstreamer pipeline's GstBuffer, which is output of GVA
     * inference elements, such as gvaaudiodetect)
     */
    GstBuffer *buffer;

    /**
     * @brief GstAudioInfo containing actual audio information for this AudioFrame
     */
    std::unique_ptr<GstAudioInfo, std::function<void(GstAudioInfo *)>> info;

  public:
    /**
     * @brief Construct AudioFrame instance from GstBuffer and GstAudioInfo. This is preferred way of creating
     * AudioFrame
     * @param buffer GstBuffer* to which metadata is attached and retrieved
     * @param info GstAudioInfo* containing audio information
     */
    AudioFrame(GstBuffer *buffer, GstAudioInfo *info)
        : buffer(buffer), info(gst_audio_info_copy(info), gst_audio_info_free) {
        if (not buffer or not info) {
            throw std::invalid_argument("GVA::AudioFrame: buffer or info nullptr");
        }
    }

    /**
     * @brief Construct AudioFrame instance from GstBuffer and GstCaps
     * @param buffer GstBuffer* to which metadata is attached and retrieved
     * @param caps GstCaps* from which audio information is obtained
     */
    AudioFrame(GstBuffer *buffer, const GstCaps *caps) : buffer(buffer) {
        if (not buffer or not caps) {
            throw std::invalid_argument("GVA::AudioFrame: buffer or caps nullptr");
        }
        info = std::unique_ptr<GstAudioInfo, std::function<void(GstAudioInfo *)>>(gst_audio_info_new(),
                                                                                  gst_audio_info_free);
        if (!gst_audio_info_from_caps(info.get(), caps)) {
            throw std::runtime_error("GVA::AudioFrame: gst_audio_info_from_caps failed");
        }
    }

    /**
     * @brief Construct AudioFrame instance from GstBuffer. Audio information will be obtained from buffer. This is
     * not recommended way of creating AudioFrame, because it relies on GstAudioMeta which can be absent for the
     * buffer
     * @param buffer GstBuffer* to which metadata is attached and retrieved
     */
    AudioFrame(GstBuffer *buffer) : buffer(buffer) {
        if (not buffer)
            throw std::invalid_argument("GVA::AudioFrame: buffer is nullptr");

        GstAudioMeta *meta = audio_meta();
        if (not meta)
            throw std::logic_error("GVA::AudioFrame: audio_meta() is nullptr");

        info = std::unique_ptr<GstAudioInfo, std::function<void(GstAudioInfo *)>>(gst_audio_info_new(),
                                                                                  gst_audio_info_free);
        if (not info.get())
            throw std::logic_error("GVA::AudioFrame: gst_audio_info_new() failed");

        memcpy(info.get(), &meta->info, sizeof(meta->info));
    }

    /**
     * @brief Get audio metadata of buffer
     * @return GstAudioMeta  of buffer, nullptr if no GstAudioMeta available
     */
    GstAudioMeta *audio_meta() {
        return gst_buffer_get_audio_meta(buffer);
    }

    /**
     * @brief Get GstAudioInfo of this AudioFrame. This is preferrable way of getting audio information
     * @return GstAudioInfo of this AudioFrame
     */
    GstAudioInfo *audio_info() {
        return info.get();
    }

    /**
     * @brief Get AudioEvent objects attached to AudioFrame
     * @return vector of AudioEvent objects attached to AudioFrame
     */
    std::vector<AudioEvent> events() {
        return get_events();
    }

    /**
     * @brief Get AudioEvent objects attached to AudioFrame
     * @return vector of AudioEvent objects attached to AudioFrame
     */
    const std::vector<AudioEvent> events() const {
        return get_events();
    }

    /**
     * @brief Get Tensor objects attached to AudioFrame
     * @return vector of Tensor objects attached to AudioFrame
     */
    std::vector<Tensor> tensors() {
        return get_tensors();
    }

    /**
     * @brief Get Tensor objects attached to AudioFrame
     * @return vector of Tensor objects attached to AudioFrame
     */
    const std::vector<Tensor> tensors() const {
        return get_tensors();
    }

    /**
     * @brief Get messages attached to this AudioFrame
     * @return messages attached to this AudioFrame
     */
    std::vector<std::string> messages() {
        std::vector<std::string> json_messages;
        GstGVAJSONMeta *meta = NULL;
        gpointer state = NULL;
        GType meta_api_type = g_type_from_name(GVA_JSON_META_API_NAME);
        while ((meta = (GstGVAJSONMeta *)gst_buffer_iterate_meta_filtered(buffer, &state, meta_api_type))) {
            json_messages.emplace_back(meta->message);
        }
        return json_messages;
    }

    /**
     * @brief Attach AudioEvent to this AudioFrame. This function takes ownership of event_tensor, if passed
     * @start_time: start time stamp of the segment
     * @end_time: end time stamp of the segment
     * @param label object label
     * @param confidence detection confidence
     * @return new AudioEvent instance
     */
    AudioEvent add_event(guint64 start_time, guint64 end_time, std::string label = std::string(),
                         double confidence = 0.0) {

        GstGVAAudioEventMeta *meta = gst_gva_buffer_add_audio_event_meta(buffer, label.c_str(), start_time, end_time);

        // Add detection tensor
        GstStructure *detection = gst_structure_new("detection", "start_timestamp", G_TYPE_UINT64, start_time,
                                                    "end_timestamp", G_TYPE_UINT64, end_time, NULL);
        if (confidence) {
            gst_structure_set(detection, "confidence", G_TYPE_DOUBLE, confidence, NULL);
        }
        gst_gva_audio_event_meta_add_param(meta, detection);

        return AudioEvent(meta);
    }

    /**
     * @brief Attach empty Tensor to this AudioFrame
     * @return new Tensor instance
     */
    Tensor add_tensor() {
        const GstMetaInfo *meta_info = gst_meta_get_info(GVA_TENSOR_META_IMPL_NAME);

        if (!gst_buffer_is_writable(buffer))
            throw std::runtime_error("Buffer is not writable.");

        GstGVATensorMeta *tensor_meta = (GstGVATensorMeta *)gst_buffer_add_meta(buffer, meta_info, NULL);

        return Tensor(tensor_meta->data);
    }

    /**
     * @brief Attach message to this AudioFrame
     * @param message message to attach to this AudioFrame
     */
    void add_message(const std::string &message) {
        const GstMetaInfo *meta_info = gst_meta_get_info(GVA_JSON_META_IMPL_NAME);

        if (!gst_buffer_is_writable(buffer))
            throw std::runtime_error("Buffer is not writable.");

        GstGVAJSONMeta *json_meta = (GstGVAJSONMeta *)gst_buffer_add_meta(buffer, meta_info, NULL);
        json_meta->message = g_strdup(message.c_str());
    }

    /**
     * @brief Remove AudioEvent
     * @param event the AudioEvent to remove
     */
    void remove_event(const AudioEvent &event) {
        if (!gst_buffer_is_writable(buffer))
            throw std::runtime_error("Buffer is not writable.");

        if (!gst_buffer_remove_meta(buffer, (GstMeta *)event._meta())) {
            throw std::out_of_range("GVA::AudioFrame: AudioEvent doesn't belong to this frame");
        }
    }

    /**
     * @brief Remove Tensor
     * @param tensor the Tensor to remove
     */
    void remove_tensor(const Tensor &tensor) {
        GstGVATensorMeta *meta = NULL;
        gpointer state = NULL;
        GType meta_api_type = g_type_from_name("GstGVATensorMetaAPI");
        while ((meta = (GstGVATensorMeta *)gst_buffer_iterate_meta_filtered(buffer, &state, meta_api_type))) {
            if (meta->data == tensor._structure) {
                if (!gst_buffer_is_writable(buffer))
                    throw std::runtime_error("Buffer is not writable.");

                if (gst_buffer_remove_meta(buffer, (GstMeta *)meta))
                    return;
            }
        }
        throw std::out_of_range("GVA::AudioFrame: Tensor doesn't belong to this frame");
    }

  private:
    std::vector<AudioEvent> get_events() const {
        std::vector<AudioEvent> events;
        GstMeta *meta = NULL;
        gpointer state = NULL;

        while ((meta = gst_buffer_iterate_meta_filtered(buffer, &state, GST_GVA_AUDIO_EVENT_META_API_TYPE)))
            events.emplace_back((GstGVAAudioEventMeta *)meta);
        return events;
    }

    std::vector<Tensor> get_tensors() const {
        std::vector<Tensor> tensors;
        GstGVATensorMeta *meta = NULL;
        gpointer state = NULL;
        GType meta_api_type = g_type_from_name("GstGVATensorMetaAPI");
        while ((meta = (GstGVATensorMeta *)gst_buffer_iterate_meta_filtered(buffer, &state, meta_api_type)))
            tensors.emplace_back(meta->data);
        return tensors;
    }
};

} // namespace GVA
