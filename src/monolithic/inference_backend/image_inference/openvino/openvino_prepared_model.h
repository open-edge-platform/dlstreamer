/*******************************************************************************
 * Copyright (C) 2018-2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

// A PreparedModel is an ov::Model that is ready to be compiled: reshaped, with the input layouts, OpenVINO
// pre-processing (color, resize, mean/scale) and batch size the pipeline needs. It also carries the facts that the rest
// of the backend asks about the model: the image input name, the batch settings, and the size, pixel format and memory
// type of the image input (which the pre-processing and the image pool are sized from).

#pragma once

#include "openvino_config_helper.h"

#include "inference_backend/image.h"

#include <openvino/openvino.hpp>

#include <memory>
#include <string>

namespace InferenceBackend {
namespace OpenVino {

// The image input of a prepared model, as the pre-processing must produce it.
struct ImageInputInfo {
    size_t width = 0;
    size_t height = 0;
    size_t batch_size = 0;
    int format = 0;      // a FourCC value
    int memory_type = 0; // a MemoryType value
};

struct PreparedModel {
    std::shared_ptr<ov::Model> model;
    std::string image_input_name;

    // Batch size of the model. 1 when OpenVINO auto-batching (batch-timeout) is used.
    int batch_size = 0;
    // -1 when auto-batching is not used.
    int batch_timeout = -1;
    // Number of requests OpenVINO auto-batching groups into one device batch; 0 when it is not used.
    int auto_batch_num_requests = 0;

    ImageInputInfo image_input;
};

// Applies the settings of the config to the model, in this order: reshape, input layouts and pre-processing, batch size.
PreparedModel prepare_model(std::shared_ptr<ov::Model> model, const ConfigHelper &config, MemoryType memory_type);

} // namespace OpenVino
} // namespace InferenceBackend
