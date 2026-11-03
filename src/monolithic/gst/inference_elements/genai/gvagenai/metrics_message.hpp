/*******************************************************************************
 * Copyright (C) 2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#pragma once

#include "backends/genai_backend.hpp"

#include <gst/gst.h>

namespace genai {

// Structure name and schema version of the per-request bus message posted with post-metrics=true.
// Consumers must check "version" and skip messages with a version they do not know.
constexpr const char *GENAI_REQUEST_MESSAGE_NAME = "dlstreamer/genai-request";
constexpr guint GENAI_REQUEST_MESSAGE_VERSION = 1;

/**
 * @brief Build the "dlstreamer/genai-request" structure for one generation request
 * @param metrics Performance of that request only, as reported by the backend
 * @param frames Number of frames the request was generated from
 * @return New structure owned by the caller
 */
GstStructure *create_request_metrics_structure(const GenAIRequestMetrics &metrics, guint frames);

} // namespace genai
