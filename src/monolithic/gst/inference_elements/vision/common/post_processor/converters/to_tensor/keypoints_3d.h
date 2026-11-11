/*******************************************************************************
 * Copyright (C) 2021-2025 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#pragma once

#include "keypoints.h"

#include "inference_backend/image_inference.h"

#include <gst/gst.h>

#include <map>
#include <memory>
#include <string>
#include <vector>

namespace post_processing {

class Keypoints3DConverter : public KeypointsConverter {
  public:
    Keypoints3DConverter(BlobToMetaConverter::Initializer initializer) : KeypointsConverter(std::move(initializer)) {
    }

    TensorsTable convert(const OutputBlobs &output_blobs) override;

    static std::string getName() {
        return "keypoints_3d";
    }
    static std::string getDeprecatedName() {
        return "tensor_to_keypoints_3d";
    }
};
} // namespace post_processing
