/*******************************************************************************
 * Copyright (C) 2021-2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#pragma once

#include "environment_variable_options_reader.h"
#include "gst_smart_pointer_types.hpp"
#include "inference_backend/image_inference.h"
#include "post_proc_common.h"
#include "tensor.h"

#include <gst/gst.h>

#include <map>
#include <memory>
#include <string>

namespace post_processing {

class BlobToMetaConverter {
  public:
    struct Initializer {
        std::string model_name;
        ModelImageInputInfo input_image_info;
        ModelOutputsInfo outputs_info;

        GstStructureUniquePtr model_proc_output_info;
        std::vector<std::string> labels;
        bool skip_raw_tensors = false;
    };

  private:
    const std::string model_name;
    const ModelImageInputInfo input_image_info;
    const ModelOutputsInfo outputs_info;

    GstStructureUniquePtr model_proc_output_info;
    const std::vector<std::string> labels;
    const bool skip_raw_tensors;

  protected:
    const ModelImageInputInfo &getModelInputImageInfo() const {
        return input_image_info;
    }
    const ModelOutputsInfo &getModelOutputsInfo() const {
        return outputs_info;
    }
    const std::string &getModelName() const {
        return model_name;
    }

    const GstStructureUniquePtr &getModelProcOutputInfo() const {
        return model_proc_output_info;
    }

    // Returns true if converter should skip adding raw tensors to metadata (inference results).
    // This is used for optimization when raw tensors are large (like depth maps) and not needed in post-processing.
    bool skipRawTensors() const {
        return skip_raw_tensors;
    }

    const std::string &getLabelByLabelId(size_t label_id) const {
        static const std::string empty_label;
        const auto &labels = getLabels();
        if (labels.empty())
            return empty_label;
        if (label_id >= labels.size())
            throw std::runtime_error("Label id is out of range");
        return labels.at(label_id);
    }

    size_t getIdByLabel(const std::string &label) const {
        const auto &labels = getLabels();
        auto it = std::find(labels.begin(), labels.end(), label);
        if (it == labels.end())
            return 0;
        return std::distance(labels.begin(), it);
    }

  public:
    BlobToMetaConverter(Initializer initializer);

    virtual TensorsTable convert(const OutputBlobs &output_blobs) = 0;

    using Ptr = std::unique_ptr<BlobToMetaConverter>;
    static Ptr create(Initializer initializer, ConverterType converter_type,
                      const std::string &displayed_layer_name_in_meta, const std::string &custom_postproc_lib);

    const std::vector<std::string> &getLabels() const {
        return labels;
    }

    virtual ~BlobToMetaConverter() = default;
};

} // namespace post_processing
