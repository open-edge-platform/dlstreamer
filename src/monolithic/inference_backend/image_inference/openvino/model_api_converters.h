/*******************************************************************************
 * Copyright (C) 2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#pragma once

#include <gst/gst.h>
#include <nlohmann/json.hpp>
#include <openvino/openvino.hpp>
#include <string>

namespace ModelApiConverters {

// Supported HuggingFace architectures
const std::vector<std::string> kHfSupportedArchitectures = {"ViTForImageClassification"};

// convert varying metadata input formats (Yolo, HuggingFace, Geti, ...) to OV Model API pre-processing metadata
std::map<std::string, GstStructure *> get_model_info_preproc(const std::shared_ptr<ov::Model> model,
                                                             const std::string model_file,
                                                             const gchar *pre_proc_config);

// convert varying metadata input formats (Yolo, HuggingFace, Geti, ...) to OV Model API post-processing metadata
std::map<std::string, GstStructure *> get_model_info_postproc(const std::shared_ptr<ov::Model> model,
                                                              const std::string model_file);

} // namespace ModelApiConverters