/*******************************************************************************
 * Copyright (C) 2021-2025 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#include "blob_to_tensor_converter.h"
#include "clip_token_converter.h"
#include "custom_to_tensor.h"
#include "detection_anomaly.h"
#include "docTR_ocr.h"
#include "keypoints_3d.h"
#include "keypoints_hrnet.h"
#include "keypoints_openpose.h"
#include "label.h"
#include "paddle_ocr.h"
#include "raw_data_copy.h"
#include "semantic_mask.h"
#include "text.h"
#include <exception>

using namespace post_processing;

BlobToMetaConverter::Ptr BlobToTensorConverter::create(BlobToMetaConverter::Initializer initializer,
                                                       const std::string &converter_name,
                                                       const std::string &custom_postproc_lib) {
    if (!custom_postproc_lib.empty())
        return std::make_unique<CustomToTensorConverter>(std::move(initializer), custom_postproc_lib);
    else if (converter_name == RawDataCopyConverter::getName())
        return std::make_unique<RawDataCopyConverter>(std::move(initializer));
    else if (converter_name == KeypointsHRnetConverter::getName())
        return std::make_unique<KeypointsHRnetConverter>(std::move(initializer));
    else if (converter_name == Keypoints3DConverter::getName())
        return std::make_unique<Keypoints3DConverter>(std::move(initializer));
    else if (converter_name == LabelConverter::getName())
        return std::make_unique<LabelConverter>(std::move(initializer));
    else if (converter_name == TextConverter::getName())
        return std::make_unique<TextConverter>(std::move(initializer));
    else if (converter_name == SemanticMaskConverter::getName())
        return std::make_unique<SemanticMaskConverter>(std::move(initializer));
    else if (converter_name == docTROCRConverter::getName())
        return std::make_unique<docTROCRConverter>(std::move(initializer));
    else if (converter_name == CLIPTokenConverter::getName())
        return std::make_unique<CLIPTokenConverter>(std::move(initializer));
    else if (converter_name == PaddleOCRConverter::getName())
        return std::make_unique<PaddleOCRConverter>(std::move(initializer));
    else if (converter_name == DetectionAnomalyConverter::getName()) {
        return std::make_unique<DetectionAnomalyConverter>(std::move(initializer));
    }

    throw std::runtime_error("ToTensorConverter \"" + converter_name + "\" is not implemented.");
}

BlobToTensorConverter::BlobToTensorConverter(BlobToMetaConverter::Initializer initializer)
    : BlobToMetaConverter(std::move(initializer)),
      raw_tensor_copying(new FeatureToggling::Runtime::RuntimeFeatureToggler()) {
    FeatureToggling::Runtime::EnvironmentVariableOptionsReader env_var_options_reader;
    raw_tensor_copying->configure(env_var_options_reader.read("ENABLE_GVA_FEATURES"));
}

GVA::Tensor BlobToTensorConverter::createTensor() const {
    GstStructure *tensor_data = nullptr;
    if (getModelProcOutputInfo()) {
        tensor_data = copy(getModelProcOutputInfo().get(), gst_structure_copy);
    } else {
        throw std::runtime_error("Failed to initialize classification result structure: model-proc is null.");
    }
    if (!tensor_data) {
        throw std::runtime_error("Failed to initialize classification result tensor.");
    }

    return GVA::Tensor(tensor_data);
}

const std::string BlobToTensorConverter::RawTensorCopyingToggle::id = "disable-tensor-copying";
const std::string BlobToTensorConverter::RawTensorCopyingToggle::deprecation_message =
    "In pipelines with gvaclassify, in addition to classification results, a raw inference tensor is added to the "
    "metadata. This functionality will be removed in future releases. Set environment variable "
    "ENABLE_GVA_FEATURES=disable-tensor-copying to disable copying to "
    "frame metadata of raw tensor after inference.";
