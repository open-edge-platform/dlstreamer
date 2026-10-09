/*******************************************************************************
 * Copyright (C) 2018-2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#pragma once

#include <memory>
#include <unordered_map>

#include "itracker.h"
#include <dlstreamer/base/memory_mapper.h>
#include <dlstreamer/context.h>

#include <vas/ot.h>

namespace VasWrapper {

class Tracker : public ITracker {
  public:
    Tracker(const std::string &device, vas::ot::TrackingType tracking_type, vas::ColorFormat in_color,
            const std::string &config_kv, dlstreamer::MemoryMapperPtr mapper, dlstreamer::ContextPtr context);
    ~Tracker() = default;

    void track(dlstreamer::FramePtr buffer, GVA::VideoFrame &frame_meta) override;

  private:
    std::unique_ptr<class TrackerBackend> _impl;
    std::unordered_map<int, std::string> labels;
    // Last detection confidence per tracking_id
    std::unordered_map<uint64_t, double> confidences;
};

} // namespace VasWrapper
