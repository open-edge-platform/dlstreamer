/*******************************************************************************
 * Copyright (C) 2021-2025 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#pragma once

#include "glib.h"
#include "inference_backend/image_inference.h"

#include <gst/video/gstvideometa.h>

struct _GvaBaseInference;
typedef struct _GvaBaseInference GvaBaseInference;
struct InferenceFrame;

namespace post_processing {

class FrameWrapper {
  public:
    FrameWrapper(InferenceFrame &);
    FrameWrapper(GstBuffer *, const std::string &instance_id, GMutex *meta_mutex);

    GstBuffer *buffer;
    GvaBaseInference *gva_base_inference;
    std::string model_instance_id;
    mutable GMutex *meta_mutex;

    /* not used for micro elements because they do not use coordinates restorer & regular tensor attachers */
    GstVideoRegionOfInterestMeta *roi;
    InferenceBackend::ImageTransformationParams::Ptr image_transform_info;
    size_t width;
    size_t height;
    std::vector<GstStructure *> *roi_classifications;
};

using InferenceFrames = std::vector<std::shared_ptr<InferenceFrame>>;

class FramesWrapper {
  public:
    FramesWrapper(const InferenceFrames &);
    FramesWrapper(GstBuffer *, const std::string &instance_id, GMutex *meta_mutex);

    bool empty() const;
    size_t size() const;
    FrameWrapper &operator[](size_t i);
    const FrameWrapper &operator[](size_t i) const;

    bool need_coordinate_restore();

  private:
    std::vector<FrameWrapper> _frames;
    bool created_from_buf = false;
};

} // namespace post_processing
