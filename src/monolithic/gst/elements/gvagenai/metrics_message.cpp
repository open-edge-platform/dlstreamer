/*******************************************************************************
 * Copyright (C) 2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#include "metrics_message.hpp"

namespace genai {

GstStructure *create_request_metrics_structure(const GenAIRequestMetrics &metrics, guint frames) {
    GstStructure *s = gst_structure_new_empty(GENAI_REQUEST_MESSAGE_NAME);
    gst_structure_set(s, "version", G_TYPE_UINT, GENAI_REQUEST_MESSAGE_VERSION, NULL);
    gst_structure_set(s, "frames", G_TYPE_UINT, frames, NULL);
    gst_structure_set(s, "ttft_ms", G_TYPE_DOUBLE, metrics.ttft_ms, NULL);
    gst_structure_set(s, "tpot_ms", G_TYPE_DOUBLE, metrics.tpot_ms, NULL);
    gst_structure_set(s, "generate_duration_ms", G_TYPE_DOUBLE, metrics.generate_duration_ms, NULL);
    gst_structure_set(s, "inference_duration_ms", G_TYPE_DOUBLE, metrics.inference_duration_ms, NULL);
    gst_structure_set(s, "prepare_embeddings_duration_ms", G_TYPE_DOUBLE, metrics.prepare_embeddings_duration_ms, NULL);
    gst_structure_set(s, "num_input_tokens", G_TYPE_UINT64, static_cast<guint64>(metrics.num_input_tokens), NULL);
    gst_structure_set(s, "num_generated_tokens", G_TYPE_UINT64, static_cast<guint64>(metrics.num_generated_tokens),
                      NULL);
    return s;
}

} // namespace genai
