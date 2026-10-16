/*******************************************************************************
 * Copyright (C) 2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#pragma once

#include <cstddef>
#include <deque>
#include <functional>
#include <vector>

namespace gvaeventrecorder {

// Fixed-capacity sliding window, continuously fed and never reset by triggers (see
// PreEventRing::snapshot). Generic over T so it can be unit tested with plain values;
// the element uses T = GstBuffer* with an on_evict callback that unrefs evicted buffers.
template <typename T>
class PreEventRing {
  public:
    using EvictFn = std::function<void(T &)>;

    explicit PreEventRing(std::size_t capacity, EvictFn on_evict = nullptr)
        : capacity_(capacity), on_evict_(std::move(on_evict)) {
    }

    // Shrinking capacity evicts the oldest items immediately; growing just raises the limit.
    void set_capacity(std::size_t capacity) {
        capacity_ = capacity;
        trim();
    }

    void push(T value) {
        frames_.push_back(std::move(value));
        trim();
    }

    // Oldest-first copy of the currently buffered items, e.g. to seed a new recording session.
    std::vector<T> snapshot() const {
        return std::vector<T>(frames_.begin(), frames_.end());
    }

    std::size_t size() const {
        return frames_.size();
    }
    std::size_t capacity() const {
        return capacity_;
    }

  private:
    void trim() {
        while (frames_.size() > capacity_) {
            if (on_evict_)
                on_evict_(frames_.front());
            frames_.pop_front();
        }
    }

    std::size_t capacity_;
    EvictFn on_evict_;
    std::deque<T> frames_;
};

} // namespace gvaeventrecorder
