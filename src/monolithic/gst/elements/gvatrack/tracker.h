/*******************************************************************************
 * Copyright (C) 2018-2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#pragma once

#include <memory>
#include <unordered_map>
#include <vector>

#include "itracker.h"
#include <dlstreamer/base/memory_mapper.h>
#include <dlstreamer/context.h>

#include <gst/gst.h>
#include <vas/ot.h>

namespace VasWrapper {

struct GstStructureDeleter {
    void operator()(GstStructure *s) const {
        gst_structure_free(s);
    }
};
using GstStructurePtr = std::unique_ptr<GstStructure, GstStructureDeleter>;

// Data of the last detection associated with a track, carried forward to the regions the tracker
// generates on frames without an associated detection (e.g. frames skipped by inference-interval)
struct TrackState {
    double confidence = 0.0;
    // Non-detection tensors of the region (e.g. keypoints, instance segmentation mask). They are
    // stored relative to the bounding box, so they follow the box predicted by the tracker.
    std::vector<GstStructurePtr> tensors;
};

class Tracker : public ITracker {
  public:
    Tracker(const std::string &device, vas::ot::TrackingType tracking_type, vas::ColorFormat in_color,
            const std::string &config_kv, dlstreamer::MemoryMapperPtr mapper, dlstreamer::ContextPtr context);
    ~Tracker() = default;

    void track(dlstreamer::FramePtr buffer, GVA::VideoFrame &frame_meta) override;

  private:
    std::unique_ptr<class TrackerBackend> _impl;
    std::unordered_map<int, std::string> labels;
    std::unordered_map<uint64_t, TrackState> track_states;
};

} // namespace VasWrapper
