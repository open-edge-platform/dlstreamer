/*******************************************************************************
 * Copyright (C) 2021-2025 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#include "frame_wrapper.h"

#include "gva_base_inference.h"
#include <processor_types.h>

using namespace post_processing;

/* class FrameWrapper */

FrameWrapper::FrameWrapper(InferenceFrame &frame)
        : buffer(frame.buffer), gva_base_inference(frame.gva_base_inference),
            model_instance_id(frame.gva_base_inference->model_instance_id),
      meta_mutex(&frame.gva_base_inference->meta_mutex), roi(&frame.roi),
      image_transform_info(frame.image_transform_info), width(frame.info->width), height(frame.info->height),
      roi_classifications(&frame.roi_classifications) {
}

// This constructor is only called for micro-elements, initialization of the rest of the fields is not required because
// they are not used there
FrameWrapper::FrameWrapper(GstBuffer *buf, const std::string &instance_id, GMutex *meta_mutex)
        : buffer(buf), gva_base_inference(nullptr), model_instance_id(instance_id), meta_mutex(meta_mutex), roi(nullptr),
            image_transform_info(nullptr), width(0), height(0), roi_classifications(nullptr) {
}

/* class FramesWrapper */

FramesWrapper::FramesWrapper(const InferenceFrames &frames) : created_from_buf(false) {
    _frames.reserve(frames.size());
    for (auto &e : frames) {
        FrameWrapper fw(*e);
        _frames.push_back(fw);
    }
}

FramesWrapper::FramesWrapper(GstBuffer *buffer, const std::string &instance_id, GMutex *meta_mutex)
    : created_from_buf(true) {
    FrameWrapper fw(buffer, instance_id, meta_mutex);
    _frames.push_back(fw);
}

bool FramesWrapper::empty() const {
    return _frames.empty();
}

size_t FramesWrapper::size() const {
    return _frames.size();
}

FrameWrapper &FramesWrapper::operator[](size_t i) {
    return _frames[i];
}

const FrameWrapper &FramesWrapper::operator[](size_t i) const {
    return _frames[i];
}

bool FramesWrapper::need_coordinate_restore() {
    return !created_from_buf;
}
