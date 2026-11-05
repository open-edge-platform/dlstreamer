/*******************************************************************************
 * Copyright (C) 2018-2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

// ModelConfigurator prepares an ov::Model for the DL Streamer pipeline before it is compiled: reshape, input layouts,
// OpenVINO pre-processing (color, resize, mean/scale), batch size. It then describes the resulting image input
// (size, batch, pixel format, memory type), which the pre-processing and the image pool are sized from.

#pragma once

#include "openvino_config_helper.h"

#include "inference_backend/image.h"
#include "inference_backend/pre_proc.h"

#include "config.h"

#include <openvino/openvino.hpp>

#include <memory>
#include <string>

namespace InferenceBackend {
namespace OpenVino {

class ModelConfigurator {
  public:
    // Reads the original input size of the model, so construct it before calling configure().
    ModelConfigurator(std::shared_ptr<ov::Model> model, MemoryType memory_type);

    // Applies the settings of the config to the model. Afterwards model(), batch_size() and the other accessors
    // return the final values.
    void configure(const ConfigHelper &config);

    // Size, batch, pixel format and memory type of the model's image input, as the pre-processing must produce it.
    void get_model_image_input_info(size_t &width, size_t &height, size_t &batch_size, int &format,
                                    int &memory_type) const;

    const std::shared_ptr<ov::Model> &model() const {
        return _model;
    }

    // Batch size of the compiled model. 1 when OpenVINO auto-batching (batch-timeout) is used.
    int batch_size() const {
        return _batch_size;
    }

    // -1 when auto-batching is not used.
    int batch_timeout() const {
        return _batch_timeout;
    }

    // Number of requests OpenVINO auto-batching groups into one device batch; 0 when it is not used.
    int auto_batch_num_requests() const {
        return _auto_batch_num_requests;
    }

    const std::string &image_input_name() const {
        return _image_input_name;
    }

  private:
    void configure_model_inputs(const ConfigHelper &config, ov::preprocess::PrePostProcessor &preproc);
    void configure_image_input(const ConfigHelper &config, ov::preprocess::InputInfo &input,
                               const ConfigHelper::InputConfig &input_config, const ov::Output<ov::Node> &node);
    void reshape_model(const ConfigHelper &config, size_t image_height, size_t image_width, bool reshape_static);
    bool is_image_input(const ConfigHelper &config, const ov::Output<ov::Node> &input) const;

    static size_t get_model_channels_num(const ConfigHelper &config);
    static ov::Layout get_image_layout_from_shape(const ov::PartialShape &shape);
    static std::pair<ov::preprocess::ColorFormat, std::vector<std::string>>
    get_ov_color_format(const std::string &img_format);

    /// @brief Get layout for input node
    /// @param node input node
    /// @param from_shape_fallback if the node has no layout information, try determining the layout from the node
    /// shape
    /// @return layout of node or empty layout
    ov::Layout get_ov_node_layout(const ov::Output<ov::Node> &node, bool from_shape_fallback = false) const;

    std::shared_ptr<ov::Model> _model;
    std::string _model_format;
    std::string _image_input_name;
    MemoryType _memory_type;
    int _auto_batch_num_requests = 0;
    int _batch_size = 0;
    int _batch_timeout = -1;

    size_t _origin_model_in_w = 0;
    size_t _origin_model_in_h = 0;
    bool _was_resize = false;
};

} // namespace OpenVino
} // namespace InferenceBackend
