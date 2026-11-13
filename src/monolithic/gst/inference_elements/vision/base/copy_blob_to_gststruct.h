/*******************************************************************************
 * Copyright (C) 2019-2025 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/
#pragma once

#include "inference_backend/image_inference.h"
#include <cstddef>
#include <tensor.h>

void CopyOutputBlobToGstStructure(InferenceBackend::OutputBlob::Ptr blob, GstStructure *gst_struct,
                                  const char *model_name, const char *layer_name, int32_t batch_size,
                                  int32_t batch_index, int32_t size = -1);

void copy_buffer_to_structure(GstStructure *structure, const void *buffer, size_t size);
