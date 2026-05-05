/*******************************************************************************
 * Copyright (C) 2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#pragma once

#include "../renderer/render_prim.h"
#include <gst/gst.h>
#include <vector>

class MetaExtractor {
  public:
    static std::vector<render::Prim> extractWatermarkPrimitives(GstBuffer *buffer);

  private:
    static std::vector<render::Prim> extractTextPrimitives(GstBuffer *buffer);
    static std::vector<render::Prim> extractShapePrimitives(GstBuffer *buffer);
    static std::vector<render::Prim> extractCirclePrimitives(GstBuffer *buffer);
};
