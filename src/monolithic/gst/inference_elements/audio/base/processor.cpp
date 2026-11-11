/*******************************************************************************
 * Copyright (C) 2018-2025 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#include "processor.h"

#include "audio_infer_impl.h"
#include "model_proc_provider.h"
#include "ov_inference.h"
#include <utils.h>

#include <assert.h>
#include <gst/allocators/allocators.h>
#include <sstream>

using namespace InferenceBackend;

using GstMemoryUniquePtr = std::unique_ptr<GstMemory, decltype(&gst_memory_unref)>;

namespace {

std::map<uint32_t, std::pair<std::string, float>> create_labels_map(GValueArray *arr,
                                                                    GvaAudioBaseInference *audio_base_inference) {
    assert(arr && audio_base_inference && "Expected non-null GValueArray and GvaAudioBaseInference");

    std::map<uint32_t, std::pair<std::string, float>> labelsNThresholds;
    for (guint i = 0; i < arr->n_values; i++) {
        const GValue *value = g_value_array_get_nth(arr, i);
        if (G_TYPE_CHECK_VALUE_TYPE(value, G_TYPE_STRING)) {
            const gchar *label = g_value_get_string(value);
            labelsNThresholds.insert({i, make_pair(std::string(label), audio_base_inference->threshold)});
        } else {
            const GstStructure *s = gst_value_get_structure(value);
            if (s) {
                gint index = 0;
                gdouble threshold = 0;
                gchar *label;
                if (gst_structure_get(s, "index", G_TYPE_INT, &index, "label", G_TYPE_STRING, &label, "threshold",
                                      G_TYPE_DOUBLE, &threshold, NULL)) {
                    labelsNThresholds.insert({index, make_pair(std::string(label), (float)threshold)});
                } else {
                    throw std::runtime_error("Invalid model-proc, labels must be strings or objects with "
                                             "index, label and threshold");
                }
            } else {
                throw std::runtime_error("Invalid model-proc, labels must be strings or objects with "
                                         "index, label and threshold");
            }
        }
    }
    return labelsNThresholds;
}

void load_model_proc(AudioInferenceOutput *infOutPut, GvaAudioBaseInference *audio_base_inference) {
    assert(infOutPut && audio_base_inference && "Expected non-null AudioInferenceOutput and GvaAudioBaseInference");

    if (!audio_base_inference->model_proc)
        throw std::runtime_error("Model-proc file is not set");

    ModelProcProvider model_proc_provider;
    model_proc_provider.readJsonFile(std::string(audio_base_inference->model_proc));
    std::map<std::string, GstStructure *> model_proc_structure = model_proc_provider.parseOutputPostproc();

    for (auto proc : model_proc_structure) {
        const gchar *convertor = gst_structure_get_string(proc.second, "converter");
        if (convertor && strcmp(convertor, "audio_labels") == 0) {
            const gchar *layer_name = gst_structure_get_string(proc.second, "layer_name");
            GValueArray *arr = NULL;
            if (layer_name && gst_structure_get_array(proc.second, "labels", &arr)) {
                auto labelsNThresholds = create_labels_map(arr, audio_base_inference);
                if (!labelsNThresholds.empty())
                    infOutPut->model_proc.insert({proc.first, labelsNThresholds});
                g_value_array_free(arr);
            } else {
                GST_ELEMENT_WARNING(audio_base_inference, RESOURCE, SETTINGS, ("Labels does not exist in model-proc"),
                                    ("Labels doesn't exist in model-proc, missing valid layer name"));
                return;
            }
        } else {
            GST_ELEMENT_WARNING(audio_base_inference, RESOURCE, SETTINGS, ("Invalid Convertor"),
                                ("Invalid Convertor set in model-proc"));
            return;
        }
    }
}

void check_and_adjust_properties(uint32_t num_samples, GvaAudioBaseInference *audio_base_inference) {
    assert(audio_base_inference && "Expected valid GvaAudioBaseInference instance");

    if (audio_base_inference->values_checked)
        return;

    if (num_samples == 0)
        throw std::runtime_error("Samples number is zero");

    uint32_t sample_length = audio_base_inference->sample_length;
    if (sample_length < num_samples || (sample_length % num_samples) != 0)
        throw std::runtime_error(
            "Input size must be less than or equal to inference-length and multiple to inference-length ");

    uint32_t sliding_samples = round(audio_base_inference->sliding_length * SAMPLE_AUDIO_RATE);
    if ((sliding_samples < sample_length) && ((sliding_samples % num_samples) != 0)) {
        sliding_samples = sliding_samples - (sliding_samples % num_samples);
        audio_base_inference->sliding_length = static_cast<double>(sliding_samples) / SAMPLE_AUDIO_RATE;
        GST_ELEMENT_WARNING(audio_base_inference, RESOURCE, SETTINGS, ("sliding-length adjusted"),
                            ("New sliding-length value %f Sec", audio_base_inference->sliding_length));
        audio_base_inference->impl_handle->setNumOfSamplesToSlide();
    }

    audio_base_inference->values_checked = true;
}

} // namespace

GstFlowReturn infer_audio(GvaAudioBaseInference *audio_base_inference, GstBuffer *buf, GstClockTime start_time) {
    if (!audio_base_inference) {
        GST_ERROR("Failed to infer audio: AudioBaseInference is null");
        return GST_FLOW_ERROR;
    }

    if (!buf) {
        GST_ELEMENT_ERROR(audio_base_inference, CORE, FAILED, ("Error: "), ("%s", "Audio buffer is null"));
        return GST_FLOW_ERROR;
    }

    try {
        AudioInferImpl *impl_handle = audio_base_inference->impl_handle;
        GstMapInfo map;
        if (!gst_buffer_map(buf, &map, GST_MAP_READ))
            throw std::runtime_error("Invalid Audio buffer");
        auto map_context = std::unique_ptr<GstMapInfo, std::function<void(GstMapInfo *)>>(
            &map, [buf](GstMapInfo *map) { gst_buffer_unmap(buf, map); });
        auto samples = reinterpret_cast<int16_t *>(map.data);
        uint32_t num_samples = map.size / sizeof(int16_t);
        check_and_adjust_properties(num_samples, audio_base_inference);
        impl_handle->addSamples(samples, num_samples, static_cast<uint64_t>(start_time));
        if (impl_handle->readyToInfer()) {
            OpenVINOAudioInference *inf_handle = audio_base_inference->inf_handle;
            AudioInferenceFrame frame;
            frame.buffer = buf;
            impl_handle->fillAudioFrame(&frame);
            std::vector<float> normalized_samples = audio_base_inference->pre_proc(&frame);
            auto normalized_samples_u8 = inf_handle->convertFloatToU8(normalized_samples);
            if (normalized_samples_u8.empty())
                inf_handle->setInputBlob(normalized_samples.data());
            else
                inf_handle->setInputBlob(normalized_samples_u8.data());
            inf_handle->infer();
            audio_base_inference->post_proc(&frame, inf_handle->getInferenceOutput());
        }
    } catch (const std::exception &e) {
        GST_ELEMENT_ERROR(audio_base_inference, CORE, FAILED, ("Error: "),
                          ("%s", Utils::createNestedErrorMsg(e).c_str()));
        return GST_FLOW_ERROR;
    }
    return GST_FLOW_OK;
}

gboolean create_handles(GvaAudioBaseInference *audio_base_inference) {
    if (!audio_base_inference) {
        GST_ERROR("Failed to create handles: AudioBaseInference is null");
        return false;
    }

    try {
        AudioInferenceOutput infOutput;
        load_model_proc(&infOutput, audio_base_inference);
        // smart pointers cannot be used because of mixed c and c++ code
        audio_base_inference->impl_handle = new AudioInferImpl(audio_base_inference);

        if (!audio_base_inference->impl_handle) {
            GST_ELEMENT_ERROR(audio_base_inference, CORE, FAILED, ("Could not initialize"),
                              ("%s", "Failed to allocate memory"));
            return false;
        }
        // smart pointers cannot be used because of mixed c and c++ code
        audio_base_inference->inf_handle =
            new OpenVINOAudioInference(audio_base_inference->model, audio_base_inference->device, infOutput);
        if (!audio_base_inference->inf_handle) {
            GST_ELEMENT_ERROR(audio_base_inference, CORE, FAILED, ("Could not initialize"),
                              ("%s", "Failed to allocate memory for OpenVINOAudioInference object"));
            return false;
        }
        audio_base_inference->sample_length = audio_base_inference->req_sample_size(audio_base_inference);
    } catch (const std::exception &e) {
        GST_ELEMENT_ERROR(audio_base_inference, CORE, FAILED, ("Could not initialize"),
                          ("%s", Utils::createNestedErrorMsg(e).c_str()));
        return false;
    }
    return true;
}

void delete_handles(GvaAudioBaseInference *audio_base_inference) {
    if (!audio_base_inference) {
        GST_ERROR("Failed to delete handles: AudioBaseInference is null");
        return;
    }

    try {
        // smart pointers cannot be used because of mixed c and c++ code
        if (audio_base_inference->inf_handle) {
            delete audio_base_inference->inf_handle;
            audio_base_inference->inf_handle = nullptr;
        }

        if (audio_base_inference->impl_handle) {
            delete audio_base_inference->impl_handle;
            audio_base_inference->impl_handle = nullptr;
        }
    } catch (const std::exception &e) {
        GST_ELEMENT_ERROR(audio_base_inference, CORE, FAILED, ("freeing up handles failed"),
                          ("%s", Utils::createNestedErrorMsg(e).c_str()));
    }
}
