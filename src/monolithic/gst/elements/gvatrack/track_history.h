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
#include <unordered_map>

// Remembers first/last seen timestamps per track id and publishes track loss as GstAnalyticsTrackingMtd
class TrackHistory {
  public:
    struct Timestamps {
        GstClockTime first_seen;
        GstClockTime last_seen;
    };

    // Records that the track was seen at pts. first_seen of an already known id is preserved (id reacquisition).
    // Invalid pts are ignored, first_seen is set by the first valid one (reported as 0 until then).
    Timestamps touch(uint64_t id, GstClockTime pts) {
        Timestamps &timestamps =
            timestamps_.try_emplace(id, Timestamps{GST_CLOCK_TIME_NONE, GST_CLOCK_TIME_NONE}).first->second;
        if (GST_CLOCK_TIME_IS_VALID(pts)) {
            if (!GST_CLOCK_TIME_IS_VALID(timestamps.first_seen))
                timestamps.first_seen = timestamps.last_seen = pts;
            else
                timestamps.last_seen = std::max(timestamps.last_seen, pts);
        }
        return published(timestamps);
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

        const Timestamps times = published(it->second);
        GstAnalyticsTrackingMtd mtd;
        if (!relation_meta ||
            !gst_analytics_relation_meta_add_tracking_mtd(relation_meta, id, times.first_seen, &mtd) ||
            !gst_analytics_tracking_mtd_update_last_seen(&mtd, times.last_seen) ||
            !gst_analytics_tracking_mtd_set_lost(&mtd)) {
            GST_WARNING("Failed to add lost tracking metadata for track %" G_GUINT64_FORMAT, id);
        }
    }

  private:
    static Timestamps published(const Timestamps &t) {
        return GST_CLOCK_TIME_IS_VALID(t.first_seen) ? t : Timestamps{0, 0};
    }

    // GST_CLOCK_TIME_NONE marks a track that has not been seen with a valid pts yet
    std::unordered_map<uint64_t, Timestamps> timestamps_;
};
