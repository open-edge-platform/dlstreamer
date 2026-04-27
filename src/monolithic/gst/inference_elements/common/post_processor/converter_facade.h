/*******************************************************************************
 * Copyright (C) 2021-2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#pragma once

#include "blob_to_meta_converter.h"
#include "coordinates_restorer.h"
#include "meta_attacher.h"

#include "inference_backend/image_inference.h"

#include <gst/gst.h>

#include <map>
#include <memory>
#include <string>
#include <unordered_set>
#include <vector>

namespace post_processing {

class ConverterFacade {
  private:
    void setLayerNames(const GstStructure *s);
    ModelOutputsInfo extractProcessedModelOutputsInfo(const ModelOutputsInfo &all_output_blobs) const;
    OutputBlobs extractProcessedOutputBlobs(const OutputBlobs &all_output_blobs) const;

    CoordinatesRestorer::Ptr createCoordinatesRestorer(ConverterType, AttachType, const ModelImageInputInfo &,
                                                       GstStructure *model_proc_output_info = nullptr);

  protected:
    std::unordered_set<std::string> layer_names_to_process;
    bool process_all_outputs;

    BlobToMetaConverter::Ptr blob_to_meta;
    CoordinatesRestorer::Ptr coordinates_restorer;
    MetaAttacher::Ptr meta_attacher;

  public:
    ConverterFacade(std::unordered_set<std::string> all_layer_names, GstStructure *model_proc_output_info,
                    ConverterType converter_type, AttachType attach_type, const ModelImageInputInfo &input_image_info,
                    const ModelOutputsInfo &outputs_info, const std::string &model_name,
                    const std::vector<std::string> &labels, const std::string &custom_postproc_lib,
                    bool skip_raw_tensors);
    ConverterFacade(GstStructure *model_proc_output_info, ConverterType converter_type, AttachType attach_type,
                    const ModelImageInputInfo &input_image_info, const ModelOutputsInfo &outputs_info,
                    const std::string &model_name, const std::vector<std::string> &labels,
                    const std::string &custom_postproc_lib, bool skip_raw_tensors);

    void convert(const OutputBlobs &all_output_blobs, FramesWrapper &frames) const;

    ConverterFacade() = default;
    ConverterFacade(const ConverterFacade &) = delete;
    ConverterFacade(ConverterFacade &&) = default;
    ConverterFacade &operator=(const ConverterFacade &) = delete;
    ConverterFacade &operator=(ConverterFacade &&) = default;

    ~ConverterFacade() = default;
};

} // namespace post_processing
