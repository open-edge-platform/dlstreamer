/*******************************************************************************
 * Copyright (C) 2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#pragma once

#include <condition_variable>
#include <cstdint>
#include <mutex>

// A mutex that hands itself to waiting threads in arrival order (a ticket lock).
//
// std::mutex makes no ordering promise, and in practice the thread that just released it is the most likely to take
// it again. When the streams sharing one inference instance all block on the instance mutex because the device is
// saturated, that favours the same stream over and over: it keeps its frame rate while the others fall behind. With
// arrival order every stream gets the device in turn, so a shortfall is shared out evenly.
//
// Meets the BasicLockable requirements, so it works with std::unique_lock and std::lock_guard.
class FairMutex {
  public:
    void lock() {
        std::unique_lock<std::mutex> guard(mutex_);
        const uint64_t ticket = next_ticket_++;
        turn_.wait(guard, [&] { return ticket == now_serving_; });
    }

    void unlock() {
        {
            std::lock_guard<std::mutex> guard(mutex_);
            ++now_serving_;
        }
        // Waiters test their own ticket, so every one of them must look.
        turn_.notify_all();
    }

  private:
    std::mutex mutex_;
    std::condition_variable turn_;
    uint64_t next_ticket_ = 0;
    uint64_t now_serving_ = 0;
};
