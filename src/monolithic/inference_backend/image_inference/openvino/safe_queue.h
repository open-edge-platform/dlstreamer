/*******************************************************************************
 * Copyright (C) 2018-2024 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

// SafeQueue is a thread-safe blocking queue. OpenVINOImageInference uses it as the pool of free infer requests:
// pop() hands out a request and blocks while none is free, push() takes it back when its inference is done.

#pragma once

#include "inference_backend/logger.h"
#include <condition_variable>
#include <mutex>
#include <queue>

template <class T>
class SafeQueue {
  public:
    // Adds an element at the back and wakes one waiting thread.
    void push(T t) {
        ITT_TASK("SafeQueue::push");
        {
            std::unique_lock<std::mutex> lock(mutex_);
            queue_.push_back(t);
        }
        condition_.notify_one();
    }

    // Adds an element at the front, so the next pop() returns it. Used to put a partly filled batch back so the next
    // frame continues it.
    void push_front(T t) {
        ITT_TASK("SafeQueue::push_front");
        {
            std::unique_lock<std::mutex> lock(mutex_);
            queue_.push_front(t);
        }
        condition_.notify_one();
    }

    // Blocks until the queue is not empty, then returns a reference to the first element without removing it. The
    // reference is only valid while no other thread pops. Not used by the OpenVINO backend.
    T &front() {
        ITT_TASK("SafeQueue::front");
        std::unique_lock<std::mutex> lock(mutex_);
        while (queue_.empty()) {
            condition_.wait(lock);
        }
        return queue_.front();
    }

    // Blocks until the queue is not empty (there is no timeout or cancel), then removes and returns the first element.
    T pop() {
        ITT_TASK("SafeQueue::pop");
        T value;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            while (queue_.empty()) {
                condition_.wait(lock);
            }
            value = queue_.front();
            queue_.pop_front();
        }
        condition_.notify_one();
        return value;
    }

    // Does not block. Another thread may change the queue right after the call returns.
    bool empty() {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.empty();
    }

    // Blocks until every element has been popped. Not used by the OpenVINO backend.
    void waitEmpty() {
        std::unique_lock<std::mutex> lock(mutex_);
        while (!queue_.empty()) {
            condition_.wait(lock);
        }
    }

  private:
    std::deque<T> queue_;
    mutable std::mutex mutex_;
    std::condition_variable condition_;
};
