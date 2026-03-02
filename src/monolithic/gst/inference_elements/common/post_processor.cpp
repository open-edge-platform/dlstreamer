/*******************************************************************************
 * Copyright (C) 2021-2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#include "post_processor.h"

#include "gstgvadetect.h"
#include "gva_base_inference.h"
#include "inference_backend/logger.h"
#include "inference_impl.h"
#include "model_proc_provider.h"

#include <map>
#include <regex>
#include <string>
#include <vector>

using namespace post_processing;

namespace {
constexpr auto ANY_LAYER_NAME = "ANY";

std::set<std::string> getDeclaredLayers(const std::map<std::string, GstStructure *> &model_proc_outputs) {
    std::set<std::string> layers;

    for (const auto &model_proc_output : model_proc_outputs) {
        GstStructure *s = model_proc_output.second;
        if (s == nullptr)
            throw std::runtime_error("Can not get model_proc output information.");

        if (not gst_structure_has_field(s, "layer_name") and not gst_structure_has_field(s, "layer_names"))
            return layers;
        if (gst_structure_has_field(s, "layer_name") and gst_structure_has_field(s, "layer_names"))
            return layers;

        if (gst_structure_has_field(s, "layer_name") and not gst_structure_has_field(s, "layer_names")) {
            layers.emplace(gst_structure_get_string(s, "layer_name"));
        }

        if (gst_structure_has_field(s, "layer_names") and not gst_structure_has_field(s, "layer_name")) {
            GValueArray *arr = nullptr;
            gst_structure_get_array(const_cast<GstStructure *>(s), "layer_names", &arr);
            if (arr and arr->n_values) {
                for (guint i = 0; i < arr->n_values; ++i)
                    layers.emplace(g_value_get_string(g_value_array_get_nth(arr, i)));
                g_value_array_free(arr);
            } else {
                throw std::runtime_error("\"layer_names\" array is null.");
            }
        }
    }

    return layers;
}

std::set<std::string> getDeclaredLayers(const ModelOutputsInfo &model_outputs_info) {
    std::set<std::string> layers;

    for (const auto &output : model_outputs_info) {
        layers.insert(output.first);
    }

    return layers;
}

PostProcessor::ModelProcOutputsValidationResult
validateModelProcOutputs(const std::map<std::string, GstStructure *> &model_proc_outputs,
                         const ModelOutputsInfo &model_outputs_info) {
    const size_t procs_num = model_proc_outputs.size();
    if (procs_num == 0) {
        return PostProcessor::ModelProcOutputsValidationResult::USE_DEFAULT;
    }

    const auto proc_layers = getDeclaredLayers(model_proc_outputs);
    if (proc_layers.empty() and procs_num == 1) {
        return PostProcessor::ModelProcOutputsValidationResult::USE_DEFAULT;
    }

    if (proc_layers.empty() and procs_num > 1) {
        GVA_ERROR("Number of declared output_postprocs more then 1, but layers are not defined.");
        return PostProcessor::ModelProcOutputsValidationResult::FAIL;
    }

    const auto model_layers = getDeclaredLayers(model_outputs_info);

    for (const auto &proc_layer : proc_layers) {
        if (model_layers.find(proc_layer) == model_layers.cend()) {
            GVA_ERROR("The '%s' is not contained among model's output layers.", proc_layer.c_str());
            return PostProcessor::ModelProcOutputsValidationResult::FAIL;
        }
    }

    return PostProcessor::ModelProcOutputsValidationResult::OK;
}

void loadLabelsFromFile(const std::string &layer_name, const std::string &labels_file,
                        std::map<std::string, std::vector<std::string>> &labels) {
    if (!Utils::fileExists(labels_file))
        throw std::invalid_argument("Labels file '" + labels_file + "' does not exist");

    if (Utils::symLink(labels_file))
        throw std::invalid_argument("Labels file '" + labels_file + "' is a symbolic link");

    std::ifstream file_stream(labels_file);
    labels[layer_name] = std::vector<std::string>();
    for (std::string line; std::getline(file_stream, line);)
        labels[layer_name].emplace_back(std::move(line));
}

void fillModelLabels(post_processing::PostProcessorImpl::Initializer &initializer, const std::string &labels_str) {
    if (!labels_str.empty()) {
        std::regex key_value_regex{"([^=]+)=([^,]+),?"};
        auto key_value_begin = std::sregex_iterator(labels_str.begin(), labels_str.end(), key_value_regex);
        auto key_value_end = std::sregex_iterator();

        if (key_value_begin != key_value_end) {
            // If there are any KEY=VALUE pairs, load labels directly
            for (auto it = key_value_begin; it != key_value_end; ++it) {
                if (it->size() != 3)
                    continue;
                loadLabelsFromFile(it->str(1), it->str(2), initializer.labels);
            }
        } else {
            // If there labels is just a path and there is only one spec for output layer in model-proc,
            // find out its name to override labels
            std::string layer_name_to_overwrite = "ANY";
            if (initializer.output_processors.size() == 1)
                layer_name_to_overwrite = initializer.output_processors.begin()->first;
            loadLabelsFromFile(layer_name_to_overwrite, labels_str, initializer.labels);
        }
    } else {
        // Load labels from model-proc file, since labels property was not provided
        for (auto &proc : initializer.output_processors) {
            GType labels_field_type = gst_structure_get_field_type(proc.second, "labels");
            if (labels_field_type == GST_TYPE_ARRAY) {
                GValueArray *labels_raw = nullptr;
                gst_structure_get_array(proc.second, "labels", &labels_raw);

                std::vector<std::string> labels;
                if (labels_raw and labels_raw->n_values) {
                    labels.reserve(labels_raw->n_values);
                    for (uint32_t i = 0; i < labels_raw->n_values; ++i) {
                        const gchar *label = g_value_get_string(labels_raw->values + i);
                        labels.emplace_back(label);
                    }
                    g_value_array_free(labels_raw);
                }
                initializer.labels[proc.first] = labels;
            } else if (labels_field_type == G_TYPE_STRING) {
                const std::string labels_file(gst_structure_get_string(proc.second, "labels"));
                loadLabelsFromFile(proc.first, labels_file, initializer.labels);
            }
        }
    }

    if (initializer.labels.empty())
        initializer.labels.insert(std::make_pair(ANY_LAYER_NAME, std::vector<std::string>{}));
}

} /* anonymous namespace */

PostProcessor::PostProcessor(InferenceImpl *inference_impl, GvaBaseInference *base_inference) {
    auto inference_type = base_inference->type;
    auto inference_region = base_inference->inference_region;

    /* set model & its name */
    const auto &model = inference_impl->GetModel();
    initializer.model_name = model.name;
    /* set input image info */
    model.inference->GetModelImageInputInfo(initializer.image_info.width, initializer.image_info.height,
                                            initializer.image_info.batch_size, initializer.image_info.format,
                                            initializer.image_info.memory_type);
    /* set outputs info */
    initializer.model_outputs = model.inference->GetModelOutputsInfo();
    /* set output processors from model proc */
    initializer.output_processors = model.output_processor_info;
    /* if model proc is empty check if post-processing info can be derived from model metadata */
    if (initializer.output_processors.empty()) {
        initializer.output_processors = model.inference->GetModelInfoPostproc();
    }
    /* set output labels */
    // NOTE: must be called after setting output_processors
    fillModelLabels(initializer, model.labels);
    /* validate outputs */
    auto validation_result = validateModelProcOutputs(initializer.output_processors, initializer.model_outputs);
    if (validation_result == ModelProcOutputsValidationResult::FAIL) {
        throw std::runtime_error("Cannot create post-processor with current model-proc information for model: " +
                                 initializer.model_name);
    }
    initializer.use_default = validation_result == ModelProcOutputsValidationResult::USE_DEFAULT;
    /* set attach type */
    if (inference_region == InferenceRegionType::FULL_FRAME) {
        initializer.attach_type = AttachType::TO_FRAME;
    } else if (inference_region == InferenceRegionType::ROI_LIST) {
        initializer.attach_type = AttachType::TO_ROI;
    }
    /* set converter type & threshold for detection */
    if (inference_type == InferenceType::GST_GVA_DETECT_TYPE) {
        initializer.converter_type = ConverterType::TO_ROI;
        GstGvaDetect *gva_detect = reinterpret_cast<GstGvaDetect *>(base_inference);
        initializer.threshold = gva_detect->threshold;
        initializer.threshold_explicitly_set = gva_detect->threshold_explicitly_set;
    } else if (inference_type == InferenceType::GST_GVA_CLASSIFY_TYPE) {
        initializer.converter_type = ConverterType::TO_TENSOR;
    } else if (inference_type == InferenceType::GST_GVA_INFERENCE_TYPE) {
        initializer.converter_type = ConverterType::RAW;
    }

    if (base_inference->custom_postproc_lib) {
        initializer.custom_postproc_lib = base_inference->custom_postproc_lib;
    }

    // This won't be converted to shared ptr because of memory placement
    new (&post_proc_impl) PostProcessorImpl(initializer);
}

PostProcessor::PostProcessor(size_t image_width, size_t image_height, size_t batch_size, const std::string &model_proc,
                             const std::string &model_name, const ModelOutputsInfo &tensor_descs,
                             ConverterType converter_type, double threshold, const std::string &labels) {

    /* image info */
    initializer.image_info.width = image_width;
    initializer.image_info.height = image_height;
    initializer.image_info.batch_size = batch_size;
    /* model proc info: labels & output processors */
    if (!model_proc.empty()) {
        ModelProcProvider model_proc_provider;
        model_proc_provider.readJsonFile(model_proc);
        initializer.output_processors = model_proc_provider.parseOutputPostproc();
    }
    // NOTE: must be called after setting output_processors
    fillModelLabels(initializer, labels);
    /* model info: name & outputs */
    initializer.model_name = model_name;
    initializer.model_outputs = tensor_descs;
    /* validate outputs */
    auto validation_result = validateModelProcOutputs(initializer.output_processors, initializer.model_outputs);
    if (validation_result == ModelProcOutputsValidationResult::FAIL) {
        throw std::runtime_error("Cannot create post-processor with current model-proc information for model: " +
                                 initializer.model_name);
    }
    initializer.use_default = validation_result == ModelProcOutputsValidationResult::USE_DEFAULT;
    /* other */
    initializer.threshold = threshold;
    initializer.attach_type = AttachType::FOR_MICRO;
    initializer.converter_type = converter_type;
    // This won't be converted to shared ptr because of memory placement
    new (&post_proc_impl) PostProcessorImpl(initializer);
}

PostProcessorImpl::ExitStatus PostProcessor::process(const OutputBlobs &blobs, InferenceFrames &frames) const {
    auto frame_wrappers = FramesWrapper(frames);
    return post_proc_impl.process(blobs, frame_wrappers);
}
