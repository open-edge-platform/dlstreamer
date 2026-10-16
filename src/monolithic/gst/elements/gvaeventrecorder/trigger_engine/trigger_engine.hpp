/*******************************************************************************
 * Copyright (C) 2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace gvaeventrecorder {

// A single detection read from incoming frame metadata (label + confidence).
struct DetectionInput {
    std::string label;
    float confidence = 0.0f;
};

// A class that newly triggered an event-recording session on the current frame.
struct TriggerResult {
    std::string class_label;
    float confidence = 0.0f;
    uint64_t session_id = 0;
};

// Pure decision logic for the gvaeventrecorder element: which classes trigger a new
// recording session, which sessions just finished their post-event window, and whether
// the current frame belongs to an active event stream. Deliberately independent of GStreamer
// types so it can be unit tested without a running pipeline; the caller (GObject element)
// is responsible for all GstBuffer / ring-buffer bookkeeping.
class TriggerEngine {
  public:
    struct Config {
        std::vector<std::string> target_classes;
        float confidence_threshold = 0.5f;
        uint64_t cooldown_value = 150; // frames
        uint32_t post_event_frames = 30;
        uint32_t event_gap_frames = 3;
        uint32_t max_concurrent_events = 4;
    };

    struct FrameOutcome {
        bool forward = false;                    // true while any accepted event is active on this frame
        std::vector<TriggerResult> new_triggers; // sessions started on this frame
        std::vector<uint64_t> finished_sessions; // session ids whose post-event window just completed
        uint32_t dropped_by_overflow = 0;        // new triggers rejected due to max_concurrent_events
    };

    explicit TriggerEngine(Config config);

    // tick is the frame number and must be monotonically non-decreasing.
    // NOTE: call active_session_ids() *before* process_frame() for a given frame if the
    // caller needs to know which sessions were active prior to this frame's updates.
    FrameOutcome process_frame(uint64_t tick, const std::vector<DetectionInput> &detections);

    // Sessions still recording an event or collecting POST frames. Does not include sessions
    // started on the current/last process_frame() call; see new_triggers for those.
    std::vector<uint64_t> active_session_ids() const;

    // True while the session is still in its event phase (before POST started).
    bool is_event_active(uint64_t session_id) const;

    void set_config(Config config);
    const Config &config() const {
        return config_;
    }

  private:
    bool is_target_class(const std::string &label) const;

    struct CooldownState {
        uint64_t first_trigger_tick = 0;
        bool has_triggered = false;
    };

    struct Session {
        uint64_t id;
        std::string class_label;
        float confidence;
        uint32_t missing_detection_frames = 0;
        uint32_t post_event_remaining = 0;
        bool event_active = true;
    };

    Config config_;
    std::unordered_map<std::string, CooldownState> cooldowns_;
    std::vector<Session> sessions_;
    uint64_t next_session_id_ = 1;
};

} // namespace gvaeventrecorder
