/*******************************************************************************
 * Copyright (C) 2021-2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#include "blob_to_meta_converter.h"

#include "converters/to_roi/blob_to_roi_converter.h"
#include "converters/to_roi/boxes_labels.h"
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

size_t getKeypointsNumber(GstStructure *s) {
    if (s == nullptr || !gst_structure_has_field(s, "point_names"))
        throw std::runtime_error("\"point_names\" is not defined in model-proc file.");

    GValueArray *point_names = nullptr;
    gst_structure_get_array(s, "point_names", &point_names);
    if (!point_names)
        throw std::runtime_error("\"point_names\" is not defined in model-proc file.");

    size_t kepoints_number = point_names->n_values;

    g_value_array_free(point_names);

    return kepoints_number;
}

std::string checkOnNameDeprecation(const std::string &converter_name) {
    const std::string GetiDetection = "ssd";
    const std::string GetiClassification = "Classification";
    const std::string GetiInstanceSegmentation = "MaskRCNN";
    const std::string GetiOBB = "rotated_detection";
    const std::unordered_map<std::string, std::string> deprecatedNameToName = {
        {DetectionOutputConverter::getDeprecatedName(), DetectionOutputConverter::getName()},
        {BoxesLabelsConverter::getDeprecatedName(), BoxesLabelsConverter::getName()},
        {YOLOv2Converter::getDeprecatedName(), YOLOv2Converter::getName()},
        {YOLOv3Converter::getDeprecatedName(), YOLOv3Converter::getName()},
        {LabelConverter::getDeprecatedName(), LabelConverter::getName()},
        {TextConverter::getDeprecatedName(), TextConverter::getName()},
        {KeypointsHRnetConverter::getDeprecatedName(), KeypointsHRnetConverter::getName()},
        {Keypoints3DConverter::getDeprecatedName(), Keypoints3DConverter::getName()},
        {KeypointsOpenPoseConverter::getDeprecatedName(), KeypointsOpenPoseConverter::getName()},
        {GetiDetection, BoxesLabelsConverter::getName()},
        {GetiClassification, LabelConverter::getName()},
        {GetiInstanceSegmentation, MaskRCNNConverter::getName()},
        {GetiOBB, MaskRCNNConverter::getName()},
        {"YOLOv8", YOLOv8Converter::getName()},
        {"YOLOv8-OBB", YOLOv8ObbConverter::getName()},
        {"YOLOv8-SEG", YOLOv8SegConverter::getName()}};

    const auto it = deprecatedNameToName.find(converter_name);
    if (it != deprecatedNameToName.cend()) {
        GVA_WARNING("The '%s' - is deprecated converter name. Please use '%s' instead.", converter_name.c_str(),
                    it->second.c_str());
        return it->second;
    }

    return converter_name;
}
} // namespace

BlobToMetaConverter::BlobToMetaConverter(Initializer initializer)
    : model_name(initializer.model_name), input_image_info(initializer.input_image_info),
      outputs_info(initializer.outputs_info), model_proc_output_info(std::move(initializer.model_proc_output_info)),
      labels(initializer.labels) {
}

BlobToMetaConverter::Ptr BlobToMetaConverter::create(Initializer initializer, ConverterType converter_type,
                                                     const std::string &displayed_layer_name_in_meta,
                                                     const std::string &custom_postproc_lib) {
    GstStructureUniquePtr &tensor = initializer.model_proc_output_info;

    const std::string converter_name = checkOnNameDeprecation(getConverterType(tensor.get()));
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
            auto keypoints_number = getKeypointsNumber(tensor.get());
            return std::make_unique<KeypointsOpenPoseConverter>(std::move(initializer), keypoints_number);
        } else {
            return BlobToTensorConverter::create(std::move(initializer), converter_name, custom_postproc_lib);
        }
    default:
        throw std::runtime_error("Invalid converter type.");
    }
    return nullptr;
}
