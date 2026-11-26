/*******************************************************************************
 * Copyright (C) 2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#include "thread_pool.h"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <future>
#include <stdexcept>
#include <vector>

// ImageInferenceAsync submits every VA-API pre-processed image to inference from this pool. A task that throws used to
// end its worker thread for good; once all workers had died, nothing ran the queued tasks, and their images never went
// back to the VaApiImagePool, so the next AcquireBuffer waited forever.

TEST(ThreadPoolTest, TaskExceptionGoesToItsFuture) {
    ThreadPool pool(1);
    auto future = pool.schedule([] { throw std::runtime_error("task failed"); });
    ASSERT_EQ(future.wait_for(std::chrono::seconds(10)), std::future_status::ready);
    EXPECT_THROW(future.get(), std::runtime_error);
}

TEST(ThreadPoolTest, WorkersSurviveThrowingTasks) {
    constexpr size_t workers = 2;
    ThreadPool pool(workers);

    // More throwing tasks than workers.
    std::vector<std::future<void>> failed;
    for (size_t i = 0; i < 2 * workers; i++)
        failed.push_back(pool.schedule([] { throw std::runtime_error("task failed"); }));
    for (auto &future : failed)
        ASSERT_EQ(future.wait_for(std::chrono::seconds(10)), std::future_status::ready);

    std::atomic<int> done{0};
    std::vector<std::future<void>> ok;
    for (size_t i = 0; i < 2 * workers; i++)
        ok.push_back(pool.schedule([&done] { done++; }));
    for (auto &future : ok) {
        ASSERT_EQ(future.wait_for(std::chrono::seconds(10)), std::future_status::ready);
        EXPECT_NO_THROW(future.get());
    }
    EXPECT_EQ(done.load(), static_cast<int>(2 * workers));
}
