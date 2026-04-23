/*******************************************************************************
 * Copyright (C) 2021-2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#include "converter_facade.h"

#include "blob_to_meta_converter.h"
#include "meta_attacher.h"

#include "gva_base_inference.h"

#include <gst/gst.h>

#include <exception>
#include <map>
#include <memory>
#include <string>
#include <unordered_set>
#include <vector>

using namespace post_processing;

namespace {
std::string getDisplayedLayerNameInMeta(std::vector<std::string> &&layer_names) {
    if (layer_names.empty())
        throw std::invalid_argument("Layer names is empty.");

    std::string displayed_layer_name = layer_names.back();
    layer_names.pop_back();
    for (const auto &layer_name : layer_names)
        displayed_layer_name = layer_name + "\\" + displayed_layer_name;

    return displayed_layer_name;
}
} // namespace

void ConverterFacade::setLayerNames(const GstStructure *s) {
    if (not gst_structure_has_field(s, "layer_name") and not gst_structure_has_field(s, "layer_names"))
        throw std::runtime_error("model proc does not have \"layer_name\" information.");
    if (gst_structure_has_field(s, "layer_name") and gst_structure_has_field(s, "layer_names"))
        throw std::runtime_error("model proc has \"layer_name\" and \"layer_names\" information.");

    if (gst_structure_has_field(s, "layer_name") and not gst_structure_has_field(s, "layer_names")) {
        layer_names_to_process.emplace(gst_structure_get_string(s, "layer_name"));
    }

    if (gst_structure_has_field(s, "layer_names") and not gst_structure_has_field(s, "layer_name")) {
        GValueArray *arr = nullptr;
        gst_structure_get_array(const_cast<GstStructure *>(s), "layer_names", &arr);
        if (arr and arr->n_values) {
            layer_names_to_process.reserve(arr->n_values);
            for (guint i = 0; i < arr->n_values; ++i)
                layer_names_to_process.emplace(g_value_get_string(g_value_array_get_nth(arr, i)));
            g_value_array_free(arr);
        } else {
            throw std::runtime_error("\"layer_names\" array is null.");
        }
    }
}

CoordinatesRestorer::Ptr ConverterFacade::createCoordinatesRestorer(ConverterType converter_type,
                                                                    AttachType attach_type,
                                                                    const ModelImageInputInfo &input_image_info,
                                                                    GstStructure *model_proc_output_info) {
    if (converter_type == ConverterType::TO_ROI)
        return CoordinatesRestorer::Ptr(new ROICoordinatesRestorer(input_image_info, attach_type));

    if (model_proc_output_info != nullptr)
        if (gst_structure_has_field(model_proc_output_info, "point_names"))
            return CoordinatesRestorer::Ptr(new KeypointsCoordinatesRestorer(input_image_info, attach_type));

    return nullptr;
}

ConverterFacade::ConverterFacade(std::unordered_set<std::string> all_layer_names, GstStructure *model_proc_output_info,
                                 ConverterType converter_type, AttachType attach_type,
                                 const ModelImageInputInfo &input_image_info, const ModelOutputsInfo &outputs_info,
                                 const std::string &model_name, const std::vector<std::string> &labels,
                                 const std::string &custom_postproc_lib, bool skip_raw_tensors)
    : layer_names_to_process(std::move(all_layer_names)), process_all_outputs(true) {

    GstStructureUniquePtr smart_model_proc_output_info(gst_structure_copy(model_proc_output_info), gst_structure_free);
    // TODO: Don't include labels in meta
    gst_structure_remove_field(smart_model_proc_output_info.get(), "labels");
    BlobToMetaConverter::Initializer initializer = {
        model_name, input_image_info, outputs_info, std::move(smart_model_proc_output_info), labels, skip_raw_tensors};

    const auto displayed_layer_name_to_process = getDisplayedLayerNameInMeta(
        std::vector<std::string>(layer_names_to_process.cbegin(), layer_names_to_process.cend()));

    blob_to_meta = BlobToMetaConverter::create(std::move(initializer), converter_type, displayed_layer_name_to_process,
                                               custom_postproc_lib);
    coordinates_restorer =
        createCoordinatesRestorer(converter_type, attach_type, input_image_info, model_proc_output_info);
    meta_attacher = MetaAttacher::create(converter_type, attach_type);
}

ConverterFacade::ConverterFacade(GstStructure *model_proc_output_info, ConverterType converter_type,
                                 AttachType attach_type, const ModelImageInputInfo &input_image_info,
                                 const ModelOutputsInfo &outputs_info, const std::string &model_name,
                                 const std::vector<std::string> &labels, const std::string &custom_postproc_lib,
                                 bool skip_raw_tensors)
    : process_all_outputs(false) {
    if (model_proc_output_info == nullptr) {
        throw std::runtime_error("Can not get model_proc output information.");
    }

    setLayerNames(model_proc_output_info);
    if (layer_names_to_process.size() == outputs_info.size())
        process_all_outputs = true;

    const auto outputs_info_to_process = extractProcessedModelOutputsInfo(outputs_info);
    GstStructureUniquePtr smart_model_proc_output_info(gst_structure_copy(model_proc_output_info), gst_structure_free);
    // TODO: Don't include labels in meta
    gst_structure_remove_field(smart_model_proc_output_info.get(), "labels");

    BlobToMetaConverter::Initializer initializer = {
        model_name, input_image_info, outputs_info_to_process, std::move(smart_model_proc_output_info),
        labels,     skip_raw_tensors};

    const auto displayed_layer_name_to_process = getDisplayedLayerNameInMeta(
        std::vector<std::string>(layer_names_to_process.cbegin(), layer_names_to_process.cend()));

    blob_to_meta = BlobToMetaConverter::create(std::move(initializer), converter_type, displayed_layer_name_to_process,
                                               custom_postproc_lib);
    coordinates_restorer =
        createCoordinatesRestorer(converter_type, attach_type, input_image_info, model_proc_output_info);
    meta_attacher = MetaAttacher::create(converter_type, attach_type);
}

ModelOutputsInfo ConverterFacade::extractProcessedModelOutputsInfo(const ModelOutputsInfo &all_output_blobs) const {
    if (all_output_blobs.empty())
        throw std::invalid_argument("Output blobs are empty.");

    // FIXME: c++17 has extract method for it
    ModelOutputsInfo processed_output_blobs;
    for (const auto &blob : all_output_blobs) {
        if (layer_names_to_process.find(blob.first) != layer_names_to_process.cend())
            processed_output_blobs.insert(blob);
    }

    return processed_output_blobs;
}

OutputBlobs ConverterFacade::extractProcessedOutputBlobs(const OutputBlobs &all_output_blobs) const {
    if (all_output_blobs.empty())
        throw std::invalid_argument("Output blobs are empty.");

    // FIXME: c++17 has extract method for it
    OutputBlobs processed_output_blobs;
    for (const auto &blob : all_output_blobs) {
        if (layer_names_to_process.find(blob.first) != layer_names_to_process.cend())
            processed_output_blobs.insert(blob);
    }

    return processed_output_blobs;
}

void ConverterFacade::convert(const OutputBlobs &all_output_blobs, FramesWrapper &frames) const {
    TensorsTable tensors_batch;
    if (process_all_outputs)
        tensors_batch = blob_to_meta->convert(all_output_blobs);
    else {
        const auto processed_output_blobs = extractProcessedOutputBlobs(all_output_blobs);
        tensors_batch = blob_to_meta->convert(processed_output_blobs);
    }

    if (frames.need_coordinate_restore() && coordinates_restorer != nullptr)
        coordinates_restorer->restore(tensors_batch, frames);

    meta_attacher->attach(tensors_batch, frames, *blob_to_meta);
}
