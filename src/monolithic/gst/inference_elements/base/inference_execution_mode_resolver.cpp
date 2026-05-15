/*******************************************************************************
 * Copyright (C) 2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#include "inference_execution_mode_resolver.h"

#include "gstgvaclassify.h"
#include "gva_base_inference.h"
#include "model_api_converters.h"
#include "model_proc_provider.h"

#include <map>
#include <openvino/openvino.hpp>
#include <string>

namespace {

bool is_depth_estimation_output_processor(const std::map<std::string, GstStructure *> &output_processors) {
    for (const auto &entry : output_processors) {
        if (!entry.second || !gst_structure_has_field(entry.second, "converter"))
            continue;

        const gchar *converter = gst_structure_get_string(entry.second, "converter");
        if (converter && std::string(converter) == "depth_estimation")
            return true;
    }

    return false;
}

bool model_requires_depth_roi_mode(GvaBaseInference *base_inference) {
    if (!base_inference || !base_inference->model)
        return false;

    if (base_inference->model_proc && base_inference->model_proc[0]) {
        ModelProcProvider model_proc_provider;
        model_proc_provider.readJsonFile(base_inference->model_proc);
        auto output_processors = model_proc_provider.parseOutputPostproc();
        const bool is_depth_model = is_depth_estimation_output_processor(output_processors);
        for (auto &entry : output_processors)
            gst_structure_free(entry.second);
        return is_depth_model;
    }

    if (base_inference->ov_extension_lib && base_inference->ov_extension_lib[0] != '\0') {
        static ov::Core extension_core;
        extension_core.add_extension(base_inference->ov_extension_lib);
        auto model = extension_core.read_model(base_inference->model);
        auto output_processors = ModelApiConverters::get_model_info_postproc(model, base_inference->model);
        const bool is_depth_model = is_depth_estimation_output_processor(output_processors);
        for (auto &entry : output_processors)
            gst_structure_free(entry.second);
        return is_depth_model;
    }

    static ov::Core core;
    auto model = core.read_model(base_inference->model);
    auto output_processors = ModelApiConverters::get_model_info_postproc(model, base_inference->model);
    const bool is_depth_model = is_depth_estimation_output_processor(output_processors);
    for (auto &entry : output_processors)
        gst_structure_free(entry.second);
    return is_depth_model;
}

} // namespace

void resolve_internal_inference_mode(GvaBaseInference *base_inference) {
    if (!base_inference)
        return;

    base_inference->effective_inference_region = base_inference->inference_region;
    base_inference->depth_inference_to_roi_mode = FALSE;

    if (base_inference->type != GST_GVA_CLASSIFY_TYPE)
        return;

    if (base_inference->inference_region != ROI_LIST)
        return;

    if (!model_requires_depth_roi_mode(base_inference))
        return;

    // Keep ROI_LIST as the public/user-visible mode, but execute as FULL_FRAME internally.
    // Post-processing will attach per-ROI depth summaries after the single full-frame inference.
    base_inference->effective_inference_region = FULL_FRAME;
    base_inference->depth_inference_to_roi_mode = TRUE;

    GST_INFO_OBJECT(base_inference,
                    "Activating internal full-frame depth mode: inference will run on the whole frame and attach "
                    "depth summaries to matching ROI metadata.");
}

bool validate_internal_depth_mode(GvaBaseInference *base_inference) {
    if (!base_inference || !base_inference->depth_inference_to_roi_mode)
        return true;

    auto *gvaclassify = reinterpret_cast<GstGvaClassify *>(base_inference);
    if (gvaclassify->reclassify_interval != 1) {
        GST_ERROR_OBJECT(base_inference,
                         "Depth classify-on-ROI mode requires reclassify-interval=1 because inference runs once per "
                         "frame, not once per ROI.");
        return false;
    }

    return true;
}