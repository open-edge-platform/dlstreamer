/*******************************************************************************
 * Copyright (C) 2018-2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

// InferSlotPool is the pool of free slots of OpenVINOImageInference. A slot is one infer request together with the
// batch of frames being filled for it. The submitting thread takes a slot with acquire() and blocks while none is
// free, which limits the number of requests in flight. A slot returns to the pool with release() when its inference is
// done. close() ends the waiting at shutdown. Slot is a cheap-to-copy handle, such as a pointer; the pool does not own
// the slots.

#pragma once

#include "inference_backend/logger.h"

#include <condition_variable>
#include <deque>
#include <mutex>
#include <optional>

template <class Slot>
class InferSlotPool {
  public:
    // Makes a slot available: a new slot, or one whose inference is done. Wakes one waiting thread. Also allowed after
    // close().
    void release(Slot slot) {
        ITT_TASK("InferSlotPool::release");
        {
            std::lock_guard lock(mutex_);
            free_.push_back(slot);
        }
        condition_.notify_one();
    }

    // Returns a partly filled batch to the pool so that the next acquire() gets it and the next frame continues it.
    void put_back_partial(Slot slot) {
        ITT_TASK("InferSlotPool::put_back_partial");
        {
            std::lock_guard lock(mutex_);
            free_.push_front(slot);
        }
        condition_.notify_one();
    }

    // Blocks until a slot is free and returns it. Returns nothing once the pool is closed and has no free slot.
    std::optional<Slot> acquire() {
        ITT_TASK("InferSlotPool::acquire");
        std::unique_lock lock(mutex_);
        condition_.wait(lock, [this] { return !free_.empty() || closed_; });
        if (free_.empty())
            return std::nullopt;
        Slot slot = free_.front();
        free_.pop_front();
        return slot;
    }

    // Does not block. Another thread may change the pool right after the call returns.
    bool has_free() const {
        std::lock_guard lock(mutex_);
        return !free_.empty();
    }

    // Wakes every thread blocked in acquire(). Slots that are still free can be acquired; after that acquire()
    // returns nothing instead of blocking.
    void close() {
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
