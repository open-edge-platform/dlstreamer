/*******************************************************************************
 * Copyright (C) 2021-2025 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#pragma once

#include "keypoints.h"

#include "human_pose_extractor/human_pose_extractor.h"
#include "inference_backend/image_inference.h"

#include <gst/gst.h>

#include <map>
#include <memory>
#include <string>
#include <vector>

namespace post_processing {

class KeypointsOpenPoseConverter : public KeypointsConverter {
  private:
    const HumanPoseExtractor extractor;

  public:
    KeypointsOpenPoseConverter(BlobToMetaConverter::Initializer initializer, size_t keypoints_number)
        : KeypointsConverter(std::move(initializer)), extractor(keypoints_number) {
    }

    TensorsTable convert(const OutputBlobs &output_blobs) override;

    static std::string getName() {
        return "keypoints_openpose";
    }
    static std::string getDeprecatedName() {
        return "tensor_to_keypoints_openpose";
    }
};
} // namespace post_processing
