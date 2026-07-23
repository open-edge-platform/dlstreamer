/*******************************************************************************
 * Copyright (C) 2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#pragma once

#include <glib.h>

G_BEGIN_DECLS

/**
 * @brief Plain-C backend configuration
 *
 * Grouped GObject-property storage for the gvagenai element. Filled by the
 * element's property setters and passed directly to
 * genai::GenAIBackendRegistry::create_backend(). Only the fields relevant to
 * the selected backend are used.
 */
typedef struct _GenAIBackendConfig {
    // Backend selection: "openvino" (default) or "openai-http"
    gchar *backend;

    // Common: local model path (OpenVINO) or model name (HTTP)
    gchar *model;

    // OpenVINO-specific
    gchar *device; // "CPU", "GPU", "NPU"
    gchar *cache_path;
    gchar *generation_config;
    gchar *scheduler_config;
    gboolean include_metrics; // Include performance metrics in JSON output

    // HTTP-specific
    gchar *server_url; // e.g., "http://localhost:8000/v1"
    gchar *api_key;    // Optional: Bearer token or API key
    gchar *timeout_ms; // Optional: request timeout in milliseconds
} GenAIBackendConfig;

G_END_DECLS
