/*******************************************************************************
 * Copyright (C) 2018-2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#include "post_processors.h"
#include "copy_blob_to_gststruct.h"
#include "gstgvaaudiodetect.h"
using namespace InferenceBackend;

void ExtractDetectionResults(AudioInferenceFrame *frame, AudioInferenceOutput *infOutput) {

    if (!frame || !infOutput)
        throw std::runtime_error("Invalid AudioInferenceFrame or AudioInferenceOutput object");

    for (const auto &blob_iter : infOutput->output_tensors) {
        // Finding layer_name specified in model_proc against output blobs
        auto model_proc_itr = infOutput->model_proc.find(blob_iter.first);
        if (model_proc_itr != infOutput->model_proc.cend()) {
            std::string layer_name = blob_iter.first;

            const float *tensor_array = reinterpret_cast<const float *>(blob_iter.second->GetData());
            size_t tensor_size = blob_iter.second->GetSize();

            int index = std::distance(tensor_array, std::max_element(tensor_array, tensor_array + tensor_size));
            std::map<uint32_t, std::pair<std::string, float>> labels = infOutput->model_proc.at(layer_name);
            auto itr = labels.find(index);
            if (itr != labels.end()) {
                float confidence = tensor_array[index];
                if (confidence >= itr->second.second) {
                    GstGVAAudioEventMeta *meta = gst_gva_buffer_add_audio_event_meta(
                        frame->buffer, itr->second.first.data(), frame->startTime, frame->endTime);

                    // Add detection tensor
                    GstStructure *detection =
                        gst_structure_new("detection", "start_timestamp", G_TYPE_UINT64, frame->startTime,
                                          "end_timestamp", G_TYPE_UINT64, frame->endTime, "label_id", G_TYPE_INT,
                                          index + 1, "confidence", G_TYPE_DOUBLE, confidence, NULL);

                    CopyOutputBlobToGstStructure(blob_iter.second, detection, infOutput->model_name.c_str(),
                                                 layer_name.c_str(), 1, 1);
                    gst_gva_audio_event_meta_add_param(meta, detection);
                }
            }
        } else {
            GST_DEBUG("gvaaudiodetect: layer_name: %s specified in model-proc not supported by model ",
                      blob_iter.first.c_str());
        }
    }
}
AudioPostProcFunction EXTRACT_RESULTS = ExtractDetectionResults;
