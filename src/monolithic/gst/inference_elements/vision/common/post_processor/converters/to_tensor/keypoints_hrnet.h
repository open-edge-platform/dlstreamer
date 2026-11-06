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

class KeypointsHRnetConverter : public KeypointsConverter {
  public:
    KeypointsHRnetConverter(BlobToMetaConverter::Initializer initializer) : KeypointsConverter(std::move(initializer)) {
    }

    TensorsTable convert(const OutputBlobs &output_blobs) override;

    static std::string getName() {
        return "keypoints_hrnet";
    }
    static std::string getDeprecatedName() {
        return "tensor_to_keypoints_hrnet";
    }
};
} // namespace post_processing
