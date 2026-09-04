/*******************************************************************************
 * Copyright (C) 2021-2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#include "post_processor.h"

#include "converters/to_roi/boxes.h"
#include "converters/to_roi/boxes_labels.h"
#include "converters/to_roi/boxes_scores.h"
#include "converters/to_roi/detection_output.h"
#include "converters/to_tensor/clip_zeroshot.h"
#include "converters/to_tensor/raw_data_copy.h"

#include "inference_backend/logger.h"

#include <exception>
#include <map>
#include <string>
#include <unordered_set>
#include <vector>

using namespace post_processing;

namespace {
inline void set_convert_name(GstStructure *s, const std::string &name) {
    gst_structure_set(s, "converter", G_TYPE_STRING, name.c_str(), NULL);
}

std::string get_convert_name(const GstStructure *s) {
    if (s == nullptr || !gst_structure_has_field(s, "converter"))
        return std::string();
    const gchar *converter = gst_structure_get_string(s, "converter");
    return converter ? std::string(converter) : std::string();
}

// The clip_zeroshot converter is selected by the model itself (model_info model_type=clip_zeroshot);
// the zeroshot-embeddings-file property only supplies the class bank. Reject the two mismatches
// early and explicitly - in particular pairing a text-embeddings bank with an unprojected clip_token
// model, which would otherwise silently compute cosine similarities across incompatible vector
// spaces and only surface as a dimension mismatch (or not at all, when the dimensions happen to
// agree).
void validateZeroShotConfiguration(const std::vector<GstStructure *> &model_proc_outputs,
                                   const PostProcessorImpl::Initializer &initializer) {
    const bool embeddings_supplied = !initializer.zeroshot_embeddings_file.empty();

    std::string zeroshot_layer;
    std::string other_converters;
    for (const auto *model_proc_output : model_proc_outputs) {
        const std::string converter_name = get_convert_name(model_proc_output);
        if (converter_name == ClipZeroShotConverter::getName()) {
            zeroshot_layer = converter_name;
        } else if (!converter_name.empty()) {
            if (!other_converters.empty())
                other_converters += ", ";
            other_converters += "'" + converter_name + "'";
        }
    }

    if (!zeroshot_layer.empty() && !embeddings_supplied) {
        throw std::runtime_error("The model declares model_type=clip_zeroshot, which selects the '" +
                                 ClipZeroShotConverter::getName() +
                                 "' converter, but no class embeddings were supplied. Set gvaclassify "
                                 "zeroshot-embeddings-file=<labels>.safetensors (generate it with "
                                 "scripts/download_models/clip_text_embeddings.py using the same CLIP model).");
    }

    if (embeddings_supplied && zeroshot_layer.empty()) {
        throw std::runtime_error(
            "gvaclassify zeroshot-embeddings-file='" + initializer.zeroshot_embeddings_file +
            "' was supplied, but the model does not declare model_type=clip_zeroshot" +
            (other_converters.empty() ? std::string() : " (resolved converter: " + other_converters + ")") +
            ". Zero-shot classification compares the model's projected image embedding against projected text "
            "embeddings; an unprojected clip_token model lives in a different vector space. Re-export the image "
            "encoder for zero-shot: python3 download_hf_models.py --model <clip-model> --extra_args --zeroshot");
    }
}
} // namespace

void PostProcessorImpl::setDefaultConverter(GstStructure *model_proc_output, const ModelOutputsInfo &model_outputs,
                                            ConverterType converter_type) {
    if (model_proc_output == nullptr)
        throw std::runtime_error("Can not get model_proc output information.");

    // gvainference (RAW) must always emit raw output tensors, ignoring any converter declared in the
    // model's metadata/config (e.g. timm "label" classification converter). For other types, keep the
    // converter already declared by the model/model-proc.
    if (converter_type != ConverterType::RAW && gst_structure_has_field(model_proc_output, "converter"))
        return;

    switch (converter_type) {
    case ConverterType::TO_ROI: {
        if (BoxesLabelsConverter::isValidModelOutputs(model_outputs)) {
            set_convert_name(model_proc_output, BoxesLabelsConverter::getName());
        } else if (BoxesConverter::isValidModelOutputs(model_outputs)) {
            set_convert_name(model_proc_output, BoxesConverter::getName());
        } else if (BoxesScoresConverter::isValidModelOutputs(model_outputs)) {
            set_convert_name(model_proc_output, BoxesScoresConverter::getName());
        } else if (DetectionOutputConverter::isValidModelOutputs(model_outputs)) {
            set_convert_name(model_proc_output, DetectionOutputConverter::getName());
        } else {
            throw std::runtime_error("Failed to determine the default detection converter. "
                                     "Please specify it yourself in the 'model-proc' file.");
        }
    } break;
    case ConverterType::RAW:
    case ConverterType::TO_TENSOR: {
        set_convert_name(model_proc_output, RawDataCopyConverter::getName());
    } break;
    default:
        throw std::runtime_error("Unknown inference type.");
    }
}

PostProcessorImpl::PostProcessorImpl(Initializer initializer) {
    try {
        if (initializer.use_default) {
            std::unordered_set<std::string> layer_names;
            layer_names.reserve(initializer.model_outputs.size());
            for (const auto &output_info : initializer.model_outputs) {
                layer_names.insert(output_info.first);
            }

            std::map<std::string, GstStructure *> model_proc_outputs;
            GstStructureUniquePtr model_proc_output_info(nullptr, gst_structure_free);

            if (initializer.output_processors.empty()) {
                model_proc_output_info.reset(gst_structure_new_empty(any_layer_name.c_str()));
                model_proc_outputs.insert(std::make_pair(any_layer_name, model_proc_output_info.get()));
            } else {
                model_proc_outputs = initializer.output_processors;
            }
            setDefaultConverter(model_proc_outputs.cbegin()->second, initializer.model_outputs,
                                initializer.converter_type);

            validateZeroShotConfiguration({model_proc_outputs.cbegin()->second}, initializer);

            if (initializer.converter_type == ConverterType::TO_ROI) {
                // Only set threshold if user explicitly provided one OR model has no threshold
                if (initializer.threshold_explicitly_set ||
                    !gst_structure_has_field(model_proc_outputs.cbegin()->second, "confidence_threshold")) {
                    gst_structure_set(model_proc_outputs.cbegin()->second, "confidence_threshold", G_TYPE_DOUBLE,
                                      initializer.threshold, NULL);
                }
                // Otherwise keep model's existing threshold
            }

            const std::vector<std::string> labels =
                (initializer.labels.find(model_proc_outputs.cbegin()->first) != initializer.labels.cend())
                    ? initializer.labels.at(model_proc_outputs.cbegin()->first)
                    : std::vector<std::string>{};

            converters.emplace_back(layer_names, model_proc_outputs.cbegin()->second, initializer.converter_type,
                                    initializer.attach_type, initializer.image_info, initializer.model_outputs,
                                    initializer.model_name, labels, initializer.custom_postproc_lib,
                                    initializer.skip_raw_tensors, initializer.zeroshot_embeddings,
                                    initializer.zeroshot_topk);
        } else {
            std::vector<GstStructure *> declared_outputs;
            declared_outputs.reserve(initializer.output_processors.size());
            for (const auto &model_proc_output : initializer.output_processors)
                declared_outputs.push_back(model_proc_output.second);
            validateZeroShotConfiguration(declared_outputs, initializer);

            for (const auto &model_proc_output : initializer.output_processors) {
                if (model_proc_output.second == nullptr) {
                    throw std::runtime_error("Can not get model_proc output information.");
                }

                if (initializer.converter_type == ConverterType::TO_ROI) {
                    // Only set threshold if user explicitly provided one OR model has no threshold
                    if (initializer.threshold_explicitly_set ||
                        !gst_structure_has_field(model_proc_output.second, "confidence_threshold")) {
                        gst_structure_set(model_proc_output.second, "confidence_threshold", G_TYPE_DOUBLE,
                                          initializer.threshold, NULL);
                    }
                    // Otherwise keep model's existing threshold
                }

                const std::vector<std::string> labels =
                    (initializer.labels.find(model_proc_output.first) != initializer.labels.cend())
                        ? initializer.labels.at(model_proc_output.first)
                        : std::vector<std::string>{};

                converters.emplace_back(model_proc_output.second, initializer.converter_type, initializer.attach_type,
                                        initializer.image_info, initializer.model_outputs, initializer.model_name,
                                        labels, initializer.custom_postproc_lib, initializer.skip_raw_tensors,
                                        initializer.zeroshot_embeddings, initializer.zeroshot_topk);
            }
        }
    } catch (const std::exception &e) {
        GVA_ERROR("Post-processing error: %s", e.what());
        std::throw_with_nested(std::runtime_error("Failed to create PostProcessorImpl"));
    }
}

PostProcessorImpl::ExitStatus PostProcessorImpl::process(const OutputBlobs &output_blobs, FramesWrapper &frames) const {
    try {
        for (const auto &converter : converters) {
            converter.convert(output_blobs, frames);
        }
    } catch (const std::exception &e) {
        GVA_ERROR("Post-processing error: %s", Utils::createNestedErrorMsg(e).c_str());
        return ExitStatus::FAIL;
    }

    return ExitStatus::SUCCESS;
}
