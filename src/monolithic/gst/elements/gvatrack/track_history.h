/*******************************************************************************
 * Copyright (C) 2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#pragma once

#include <gst/analytics/analytics.h>
#include <gst/gst.h>

#include <algorithm>
#include <cstdint>
#include <stdexcept>
#include <unordered_map>

// Remembers first/last seen timestamps per track id and publishes track loss as GstAnalyticsTrackingMtd
class TrackHistory {
  public:
    struct Timestamps {
        GstClockTime first_seen;
        GstClockTime last_seen;
    };

    // Records that the track was seen at pts. first_seen of an already known id is preserved (id reacquisition).
    Timestamps touch(uint64_t id, GstClockTime pts) {
        const bool valid = GST_CLOCK_TIME_IS_VALID(pts);
        Timestamps &timestamps =
            timestamps_.try_emplace(id, Timestamps{valid ? pts : 0, valid ? pts : 0}).first->second;
        if (valid)
            timestamps.last_seen = std::max(timestamps.last_seen, pts);
        return timestamps;
    }

    void forget(uint64_t id) {
        timestamps_.erase(id);
    }

    // Adds a lost TrackingMtd to the buffer, previously delivered buffers cannot be modified. Unknown ids are ignored.
    void report_lost(GstBuffer *buffer, uint64_t id) const {
        auto it = timestamps_.find(id);
        if (it == timestamps_.end())
            return;

        GstAnalyticsRelationMeta *relation_meta = gst_buffer_get_analytics_relation_meta(buffer);
        if (!relation_meta)
            relation_meta = gst_buffer_add_analytics_relation_meta(buffer);

        GstAnalyticsTrackingMtd mtd;
        if (!relation_meta ||
            !gst_analytics_relation_meta_add_tracking_mtd(relation_meta, id, it->second.first_seen, &mtd) ||
            !gst_analytics_tracking_mtd_update_last_seen(&mtd, it->second.last_seen) ||
            !gst_analytics_tracking_mtd_set_lost(&mtd)) {
            throw std::runtime_error("Failed to add lost tracking metadata");
        }
    }

  private:
    std::unordered_map<uint64_t, Timestamps> timestamps_;
};
