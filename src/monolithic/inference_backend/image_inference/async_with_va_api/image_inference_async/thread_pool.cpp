/*******************************************************************************
 * Copyright (C) 2019-2024 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#include "thread_pool.h"

#include "config.h"
#include "inference_backend/logger.h"
#include "utils.h"

#include <string>

#ifdef ENABLE_ITT
#include "ittnotify.h"
#define ITT_THREAD_NAME()                                                                                              \
    {                                                                                                                  \
        static size_t thread_counter = 0;                                                                              \
        std::unique_lock<std::mutex> lock(_mutex);                                                                     \
        std::string thread_name = "gva::threadpool::id::" + std::to_string(thread_counter);                            \
        __itt_thread_set_name(thread_name.c_str());                                                                    \
        thread_counter++;                                                                                              \
    }
#else
#define ITT_THREAD_NAME()
#endif

ThreadPool::ThreadPool(size_t size) {
    for (size_t i = 0; i < size; ++i) {
        _threads.emplace_back(&ThreadPool::_task_runner, this);
    }
}

ThreadPool::~ThreadPool() {
    _terminate = true;
    _condition_variable.notify_all();
    for (auto &t : _threads) {
        if (t.joinable()) {
            t.join();
        }
    }
}

std::future<void> ThreadPool::schedule(const std::function<void()> &callable) {
    std::shared_ptr<std::promise<void>> p = std::make_shared<std::promise<void>>();
    std::future<void> future = p->get_future();
    {
        std::unique_lock<std::mutex> lock(_mutex);
        _tasks.push([callable, p]() {
            try {
                callable();
            } catch (...) {
                p->set_exception(std::current_exception());
                throw;
            }
            p->set_value();
        });
    }

    _condition_variable.notify_one();
    return future;
}

void ThreadPool::_task_runner() {
    ITT_THREAD_NAME();
    while (!_terminate) {
        std::function<void()> task;

        // Pop a task
        {
            std::unique_lock<std::mutex> lock(_mutex);
            _condition_variable.wait_for(lock, std::chrono::seconds(1),
                                         [this]() -> bool { return !_tasks.empty() || _terminate; });
            if (!_tasks.empty()) {
                task = _tasks.front();
                _tasks.pop();
            }
        }

        if (!task)
            continue;
        // A failing task must not take the worker thread with it: the pool has only a few threads, and every lost one
        // leaves tasks (and the pool images they hold) waiting forever.
        try {
            task();
        } catch (const std::exception &e) {
            GVA_ERROR("Error was happened during in another thread: %s", Utils::createNestedErrorMsg(e).c_str());
        } catch (...) {
            GVA_ERROR("Unknown error was happened during in another thread");
        }
    }
}
