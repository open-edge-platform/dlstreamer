/*******************************************************************************
 * Copyright (C) 2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#include "event_session_controller.hpp"

#include <algorithm>

namespace gvaeventrecorder {

EventSessionController::EventSessionController(Config config) : config_(std::move(config)) {
}

void EventSessionController::set_config(Config config) {
    config_ = std::move(config);
}

bool EventSessionController::is_target_class(const std::string &label) const {
    return std::find(config_.target_classes.begin(), config_.target_classes.end(), label) !=
           config_.target_classes.end();
}

std::vector<uint64_t> EventSessionController::active_session_ids() const {
    std::vector<uint64_t> ids;
    ids.reserve(sessions_.size());
    for (const auto &s : sessions_)
        ids.push_back(s.id);
    return ids;
}

bool EventSessionController::is_event_active(uint64_t session_id) const {
    return std::any_of(sessions_.begin(), sessions_.end(),
                       [session_id](const Session &s) { return s.id == session_id && s.event_active; });
}

EventSessionController::FrameOutcome
EventSessionController::process_frame(uint64_t tick, const std::vector<DetectionInput> &detections) {
    FrameOutcome outcome;

    // Per-class session lifecycle:
    //
    //   Trigger --> EVENT --> POST --> Finished
    //
    //   EVENT -> POST      : no detection for more than event-gap-frames frames
    //   POST  -> Finished  : post-event-frames elapsed (POST is skipped if it is <= 1)
    //   EVENT/POST -> EVENT: target detected again
    for (auto it = sessions_.begin(); it != sessions_.end();) {
        const bool detected = std::any_of(detections.begin(), detections.end(), [this, &it](const DetectionInput &d) {
            return d.label == it->class_label && d.confidence >= config_.confidence_threshold;
        });

        if (detected) {
            it->event_active = true;
            it->missing_detection_frames = 0;
            it->post_event_remaining = 0;
            ++it;
            continue;
        }

        bool finished = false;
        if (it->event_active) {
            ++it->missing_detection_frames;
            if (it->missing_detection_frames > config_.event_gap_frames) {
                it->event_active = false;
                it->post_event_remaining = config_.post_event_frames;
                // The first frame beyond the gap already counts toward POST.
                if (it->post_event_remaining > 0)
                    --it->post_event_remaining;
                finished = it->post_event_remaining == 0;
            }
        } else if (it->post_event_remaining > 0) {
            --it->post_event_remaining;
            finished = it->post_event_remaining == 0;
        } else {
            finished = true;
        }

        if (finished) {
            outcome.finished_sessions.push_back(it->id);
            it = sessions_.erase(it);
        } else {
            ++it;
        }
    }

    // Sessions are updated first, so a slot freed on this frame can be reused below.
    for (const auto &d : detections) {
        if (!is_target_class(d.label) || d.confidence < config_.confidence_threshold)
            continue;

        const bool session_exists = std::any_of(sessions_.begin(), sessions_.end(),
                                                [&d](const Session &s) { return s.class_label == d.label; });
        if (session_exists)
            continue;

        CooldownState &cd = cooldowns_[d.label];
        const bool cooldown_active = cd.has_triggered && (tick - cd.first_trigger_tick) < config_.cooldown_value;
        if (cooldown_active)
            continue; // suppressed repeat trigger, cooldown window not restarted

        // Cooldown starts even if the trigger is dropped for capacity, so it is not retried every frame.
        cd.has_triggered = true;
        cd.first_trigger_tick = tick;

        if (sessions_.size() >= config_.max_concurrent_events) {
            ++outcome.dropped_by_overflow;
            continue; // capacity reached, event not recorded (but cooldown still started)
        }

        Session s;
        s.id = next_session_id_++;
        s.class_label = d.label;
        s.confidence = d.confidence;

        outcome.new_triggers.push_back({d.label, d.confidence, s.id});
        sessions_.push_back(s);
    }

    // True only during EVENT, not POST; the recorder decides forwarding from session IDs instead.
    outcome.forward = std::any_of(sessions_.begin(), sessions_.end(), [](const Session &s) { return s.event_active; });

    return outcome;
}

} // namespace gvaeventrecorder
