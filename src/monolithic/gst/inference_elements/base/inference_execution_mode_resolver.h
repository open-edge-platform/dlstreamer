/*******************************************************************************
 * Copyright (C) 2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#pragma once

#include <gst/gst.h>

typedef struct _GvaBaseInference GvaBaseInference;

void resolve_internal_inference_mode(GvaBaseInference *base_inference);
bool validate_internal_depth_mode(GvaBaseInference *base_inference);