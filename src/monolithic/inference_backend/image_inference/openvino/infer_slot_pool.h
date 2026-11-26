/*******************************************************************************
 * Copyright (C) 2018-2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

// InferSlotPool is the pool of empty slots of OpenVinoImageInference. A slot is one infer request together with the
// batch of frames being filled for it. The submitting thread takes a slot with Acquire() and blocks while none is
// free, which limits the number of requests in flight. A slot returns to the pool with Release() when its inference is
// done. Close() ends the waiting at shutdown. Slot is a cheap-to-copy handle, such as a pointer; the pool does not own
// the slots.

#pragma once

#include "inference_backend/logger.h"

#include <chrono>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <optional>

template <class Slot>
class InferSlotPool {
  public:
    // Slots come back when inference requests complete; one that does not within this time means the requests of the
    // model instance stopped completing, and the submitting thread (which holds the instance's locks) must not wait
    // forever (issue #1240).
    static constexpr std::chrono::seconds kAcquireTimeout{30};
    // Makes a slot available: a new slot, or one whose inference is done. Wakes one waiting thread. Also allowed after
    // Close().
    void Release(Slot slot) {
        ITT_TASK("InferSlotPool::Release");
        {
            std::lock_guard lock(mutex_);
            free_.push_back(slot);
        }
        condition_.notify_one();
    }

    // Blocks until a slot is free and returns it. Returns nothing once the pool is closed, or when no slot became free
    // within kAcquireTimeout (see IsClosed() to tell the two apart).
    std::optional<Slot> Acquire() {
        ITT_TASK("InferSlotPool::Acquire");
        std::unique_lock lock(mutex_);
        if (!condition_.wait_for(lock, kAcquireTimeout, [this] { return !free_.empty() || closed_; }) || closed_)
            return std::nullopt;
        Slot slot = free_.front();
        free_.pop_front();
        return slot;
    }

    bool IsClosed() const {
        std::lock_guard lock(mutex_);
        return closed_;
    }

    // Does not block. Another thread may change the pool right after the call returns.
    bool HasFree() const {
        std::lock_guard lock(mutex_);
        return !free_.empty();
    }

    // Wakes every thread blocked in Acquire(); from now on Acquire() returns nothing.
    void Close() {
        {
            std::lock_guard lock(mutex_);
            closed_ = true;
        }
        condition_.notify_all();
    }

  private:
    std::deque<Slot> free_;
    bool closed_ = false;
    mutable std::mutex mutex_;
    std::condition_variable condition_;
};
