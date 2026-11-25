/*******************************************************************************
 * Copyright (C) 2018-2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#pragma once

#include <dlstreamer/frame.h>
#include <video_frame.h>

class ITracker {
  public:
    virtual ~ITracker() = default;
    virtual void track(dlstreamer::FramePtr buffer, GVA::VideoFrame &frame_meta, GstBuffer *gst_buffer) = 0;
};
