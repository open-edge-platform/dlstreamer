/*******************************************************************************
 * Copyright (C) 2022-2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#pragma once

#include <gst/base/gstbasetransform.h>

#define BATCH_CREATE_DESCRIPTION "Accumulate multiple buffers into single buffer with multiple GstMemory"

G_BEGIN_DECLS

GType batch_create_get_type(void);

G_END_DECLS
