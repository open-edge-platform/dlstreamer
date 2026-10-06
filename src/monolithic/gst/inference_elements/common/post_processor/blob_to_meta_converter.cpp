/*******************************************************************************
 * Copyright (C) 2021-2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#include "blob_to_meta_converter.h"

#include "converters/to_roi/blob_to_roi_converter.h"
#include "converters/to_roi/boxes_labels_scores.h"
#include "converters/to_roi/detection_output.h"
#include "converters/to_roi/mask_rcnn.h"
#include "converters/to_roi/yolo_v2.h"
#include "converters/to_roi/yolo_v3.h"
#include "converters/to_roi/yolo_v8.h"
#include "converters/to_tensor/blob_to_tensor_converter.h"
#include "converters/to_tensor/clip_token_converter.h"
#include "converters/to_tensor/keypoints_3d.h"
#include "converters/to_tensor/keypoints_hrnet.h"
#include "converters/to_tensor/keypoints_openpose.h"
#include "converters/to_tensor/label.h"
#include "converters/to_tensor/paddle_ocr.h"
#include "converters/to_tensor/raw_data_copy.h"
#include "converters/to_tensor/text.h"

#include <dlstreamer/gst/metadata/gstanalyticskeypointdescriptor.h>

#include "gva_base_inference.h"

#include <gst/gst.h>

#include <algorithm>
#include <exception>
#include <map>
#include <string>

using namespace post_processing;

namespace {
std::string getConverterType(GstStructure *s) {
    if (s == nullptr || !gst_structure_has_field(s, "converter"))
        throw std::runtime_error("Couldn't determine converter type.");

    std::string converter_type = gst_structure_get_string(s, "converter");
    if (converter_type.empty())
        throw std::runtime_error("model_proc's output_processor has empty converter.");

    return converter_type;
}

std::string converterTypeToTensorName(ConverterType converter_type, std::string layer_name) {
    // GstStructure name string does not support '\'
    std::replace(layer_name.begin(), layer_name.end(), '\\', ':');

    switch (converter_type) {
    case ConverterType::TO_ROI:
        return "detection";
    case ConverterType::TO_TENSOR:
        return "classification_layer_name:" + layer_name;
    case ConverterType::RAW:
        return "inference_layer_name:" + layer_name;
    default:
        throw std::runtime_error("Invalid converter type.");
    }
}
void updateTensorNameIfNeeded(GstStructure *s, const std::string &default_name) {
    if (s == nullptr)
        throw std::runtime_error("GstStructure is null.");

    if (gst_structure_has_field(s, "attribute_name")) {
        const char *result_name = gst_structure_get_string(s, "attribute_name");
        gst_structure_set_name(s, result_name);
        return;
    }

    const std::string current_name = gst_structure_get_name(s);
    if (current_name == default_name)
        return;

    gst_structure_set_name(s, default_name.c_str());
}

std::string resolveConverterName(const std::string &requested) {
    // model_type values from model_info metadata.
    static const std::unordered_map<std::string, std::string> model_type_to_converter = {
        {"ssd", BoxesLabelsScoresConverter::getName()},      // Geti / Model API detection
        {"Classification", LabelConverter::getName()},       // Geti / Model API classification
        {"MaskRCNN", MaskRCNNConverter::getName()},          // Geti / Model API instance segmentation
        {"Segmentation", "semantic_segmentation"},           // Geti / Model API semantic segmentation
        {"rotated_detection", MaskRCNNConverter::getName()}, // Geti oriented detection
        {"YOLOv8", YOLOv8Converter::getName()},              // older DL Streamer download scripts (Ultralytics)
        {"YOLOv8-OBB", YOLOv8ObbConverter::getName()},       // older DL Streamer download scripts (Ultralytics)
        {"YOLOv8-SEG", YOLOv8SegConverter::getName()}};      // older DL Streamer download scripts (Ultralytics)

    // Old converter names typed by users in model-proc files; still accepted, with a deprecation warning.
    static const std::unordered_map<std::string, std::string> legacy_converter_names = {
        {DetectionOutputConverter::getDeprecatedName(), DetectionOutputConverter::getName()},
        {"tensor_to_bbox_atss", BoxesLabelsScoresConverter::getName()},
        {"boxes", BoxesLabelsScoresConverter::getName()},
        {"boxes_labels", BoxesLabelsScoresConverter::getName()},
        {YOLOv2Converter::getDeprecatedName(), YOLOv2Converter::getName()},
        {YOLOv3Converter::getDeprecatedName(), YOLOv3Converter::getName()},
        {LabelConverter::getDeprecatedName(), LabelConverter::getName()},
        {TextConverter::getDeprecatedName(), TextConverter::getName()},
        {KeypointsHRnetConverter::getDeprecatedName(), KeypointsHRnetConverter::getName()},
        {Keypoints3DConverter::getDeprecatedName(), Keypoints3DConverter::getName()},
        {KeypointsOpenPoseConverter::getDeprecatedName(), KeypointsOpenPoseConverter::getName()},
        {"semantic_mask", "semantic_segmentation"}}; // renamed in 2026; still written by download_other_models.sh

    if (const auto it = model_type_to_converter.find(requested); it != model_type_to_converter.cend())
        return it->second;

    if (const auto it = legacy_converter_names.find(requested); it != legacy_converter_names.cend()) {
        GVA_WARNING("The '%s' - is deprecated converter name. Please use '%s' instead.", requested.c_str(),
                    it->second.c_str());
        return it->second;
    }

    return requested;
}
} // namespace

BlobToMetaConverter::BlobToMetaConverter(Initializer initializer)
    : model_name(initializer.model_name), input_image_info(initializer.input_image_info),
      outputs_info(initializer.outputs_info), model_proc_output_info(std::move(initializer.model_proc_output_info)),
      labels(initializer.labels), skip_raw_tensors(initializer.skip_raw_tensors),
      zeroshot_embeddings(std::move(initializer.zeroshot_embeddings)), zeroshot_topk(initializer.zeroshot_topk) {
}

BlobToMetaConverter::Ptr BlobToMetaConverter::create(Initializer initializer, ConverterType converter_type,
                                                     const std::string &displayed_layer_name_in_meta,
                                                     const std::string &custom_postproc_lib) {
    GstStructureUniquePtr &tensor = initializer.model_proc_output_info;

    const std::string converter_name = resolveConverterName(getConverterType(tensor.get()));
    const std::string default_name = converterTypeToTensorName(converter_type, displayed_layer_name_in_meta);

    if (tensor.get() == nullptr) {
        tensor.reset(gst_structure_new_empty(default_name.c_str()));
    }

    updateTensorNameIfNeeded(tensor.get(), default_name);

    gst_structure_set(tensor.get(), "layer_name", G_TYPE_STRING, displayed_layer_name_in_meta.c_str(), "model_name",
                      G_TYPE_STRING, initializer.model_name.c_str(), NULL);

    switch (converter_type) {
    case ConverterType::RAW:
        if (converter_name == RawDataCopyConverter::getName())
            return std::make_unique<RawDataCopyConverter>(std::move(initializer));
        else if (converter_name == CLIPTokenConverter::getName())
            return BlobToMetaConverter::Ptr(new CLIPTokenConverter(std::move(initializer)));
        else
            throw std::runtime_error("Unsupported converter '" + converter_name + "' for type RAW");
        break;
    case ConverterType::TO_ROI:
        return BlobToROIConverter::create(std::move(initializer), converter_name, custom_postproc_lib);
    case ConverterType::TO_TENSOR:
        if (converter_name == KeypointsOpenPoseConverter::getName()) {
            return std::make_unique<KeypointsOpenPoseConverter>(
                std::move(initializer),
                gst_analytics_keypoint_descriptor_lookup(GST_ANALYTICS_KEYPOINT_BODY_POSE_OPENPOSE_18)->point_count);
        } else {
            return BlobToTensorConverter::create(std::move(initializer), converter_name, custom_postproc_lib);
        }
    default:
        throw std::runtime_error("Invalid converter type.");
    }
    return nullptr;
}
