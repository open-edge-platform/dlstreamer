/*******************************************************************************
 * Copyright (C) 2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#include "trigger_engine.hpp"

#include <algorithm>

namespace gvaeventrecorder {

TriggerEngine::TriggerEngine(Config config) : config_(std::move(config)) {
}

void TriggerEngine::set_config(Config config) {
    config_ = std::move(config);
}

bool TriggerEngine::is_target_class(const std::string &label) const {
    return std::find(config_.target_classes.begin(), config_.target_classes.end(), label) !=
           config_.target_classes.end();
}

std::vector<uint64_t> TriggerEngine::active_session_ids() const {
    std::vector<uint64_t> ids;
    ids.reserve(sessions_.size());
    for (const auto &s : sessions_)
        ids.push_back(s.id);
    return ids;
}

bool TriggerEngine::is_event_active(uint64_t session_id) const {
    return std::any_of(sessions_.begin(), sessions_.end(),
                       [session_id](const Session &s) { return s.id == session_id && s.event_active; });
}

TriggerEngine::FrameOutcome TriggerEngine::process_frame(uint64_t tick, const std::vector<DetectionInput> &detections) {
    FrameOutcome outcome;

    // Keep a session through the full event; start POST only after the class disappears.
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

    outcome.forward = std::any_of(sessions_.begin(), sessions_.end(), [](const Session &s) { return s.event_active; });

    return outcome;
}

} // namespace gvaeventrecorder
