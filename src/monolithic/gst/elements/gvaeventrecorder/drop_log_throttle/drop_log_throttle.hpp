/*******************************************************************************
 * Copyright (C) 2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#pragma once

#include <cstdint>

namespace gvaeventrecorder {

// Aggregates repeated "event dropped" occurrences (e.g. max-concurrent-events overflow)
// into a single log line emitted at most once per configured interval, so a burst of
// drops does not flood the log. Caller decides what to do with the returned count
// (typically GST_WARNING with "<n> event(s) dropped since last log").
class DropLogThrottle {
  public:
    explicit DropLogThrottle(uint64_t interval_ms) : interval_ms_(interval_ms) {
    }

    void set_interval_ms(uint64_t interval_ms) {
        interval_ms_ = interval_ms;
    }

    void record_drop(uint32_t count = 1) {
        pending_ += count;
    }

    // now_ms: monotonic wall-clock milliseconds. Returns the pending count (and resets it)
    // once per interval_ms_, 0 otherwise.
    uint32_t maybe_flush(uint64_t now_ms) {
        if (pending_ == 0)
            return 0;
        if (!flushed_once_ || (now_ms - last_flush_ms_) >= interval_ms_) {
            uint32_t n = static_cast<uint32_t>(pending_);
            pending_ = 0;
            last_flush_ms_ = now_ms;
            flushed_once_ = true;
            return n;
        }
        return 0;
    }

    // Unconditional flush, e.g. when the element stops, so pending drops are never lost.
    uint32_t force_flush() {
        uint32_t n = static_cast<uint32_t>(pending_);
        pending_ = 0;
        return n;
    }

  private:
    uint64_t interval_ms_;
    uint64_t pending_ = 0;
    uint64_t last_flush_ms_ = 0;
    bool flushed_once_ = false;
};

} // namespace gvaeventrecorder
