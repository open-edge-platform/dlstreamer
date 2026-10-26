/*******************************************************************************
 * Copyright (C) 2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

/* Single-stream fault isolation: a source that stops delivering must not hold
 * up the ones that are still healthy, and the application must be able to find
 * out which one it was. See issue #1108.
 */

#include "streammux_harness.h"

#include <gtest/gtest.h>

namespace {

constexpr GstClockTime kFrame = StreammuxHarness::kFrameDuration;

/* Four sources, one silent. Batches must keep coming, made of the three that
 * are delivering, instead of every one of them waiting out max-wait-time for
 * the one that never arrives. */
TEST(StreammuxIsolation, BatchesStopWaitingForASilentSource) {
    StreammuxHarness h;
    /* A long wait is what makes the difference visible. Holding every batch
     * open for a source that will never deliver costs this much per batch;
     * recognising that it is gone costs it only until it is recognised. */
    const guint64 wait_ns = 200 * GST_MSECOND;
    g_object_set(h.mux(), "max-wait-time", wait_ns, nullptr);
    for (guint s = 0; s < 4; s++)
        h.addSource(s);
    h.start();

    for (int i = 0; i < 4; i++)
        for (guint s = 0; s < 4; s++)
            h.push(s, i * kFrame);
    ASSERT_TRUE(h.waitFor([&] { return h.outputCount() >= 12; }, 3000));

    /* Source 0 goes silent without an EOS -- a dead feed, not a finished one. */
    guint before = h.outputCount();
    const int rounds = 20;
    for (int i = 4; i < 4 + rounds; i++)
        for (guint s = 1; s < 4; s++)
            h.push(s, i * kFrame);

    /* Waiting out max-wait-time for every one of these would take four
     * seconds; two is comfortably past what it should need and well short of
     * what it would need if nothing had changed. */
    EXPECT_TRUE(h.waitFor([&] { return h.outputCount() >= before + rounds * 3; }, 2000))
        << "the three healthy sources produced only " << h.outputCount() - before << " of " << rounds * 3
        << " buffers while one source was silent";
}

/* Which source went bad has to reach the application, so posting it on the bus
 * is part of the contract rather than a debugging aid. */
TEST(StreammuxIsolation, ASilentSourceIsReportedOnTheBus) {
    StreammuxHarness h;
    for (guint s = 0; s < 4; s++)
        h.addSource(s);
    h.start();

    for (int i = 0; i < 6; i++)
        for (guint s = 0; s < 4; s++)
            h.push(s, i * kFrame);
    ASSERT_TRUE(h.waitFor([&] { return h.outputCount() >= 16; }));
    h.drainStreamStates(); /* ignore anything from starting up */

    for (int i = 6; i < 40; i++)
        for (guint s = 1; s < 4; s++)
            h.push(s, i * kFrame);

    std::vector<StreammuxHarness::StreamState> stalled;
    ASSERT_TRUE(h.waitFor(
        [&] {
            for (const auto &st : h.drainStreamStates())
                if (st.state == "stalled")
                    stalled.push_back(st);
            return !stalled.empty();
        },
        3000))
        << "nothing was posted about the source that stopped delivering";

    EXPECT_EQ(stalled.front().pad_index, 0u) << "the wrong source was named";
    EXPECT_EQ(stalled.front().total_pads, 4u);
    EXPECT_EQ(stalled.front().active_pads, 3u);
}

/* Recovery is judged on delivery, not on being batched: a source returning
 * from a stall sends a backlog that is too old to use, and if recovery needed
 * it to contribute it could never recover at all. */
TEST(StreammuxIsolation, ASourceIsActiveAgainAsSoonAsItDelivers) {
    StreammuxHarness h;
    for (guint s = 0; s < 4; s++)
        h.addSource(s);
    h.start();

    for (int i = 0; i < 6; i++)
        for (guint s = 0; s < 4; s++)
            h.push(s, i * kFrame);
    ASSERT_TRUE(h.waitFor([&] { return h.outputCount() >= 16; }));
    h.drainStreamStates();

    for (int i = 6; i < 40; i++)
        for (guint s = 1; s < 4; s++)
            h.push(s, i * kFrame);

    bool stalled = false;
    ASSERT_TRUE(h.waitFor(
        [&] {
            for (const auto &st : h.drainStreamStates())
                if (st.pad_index == 0 && st.state == "stalled")
                    stalled = true;
            return stalled;
        },
        3000));

    /* It comes back with arrears -- too old to be batched, but it is alive. */
    for (int i = 6; i < 20; i++)
        h.push(0, i * kFrame);

    bool active = false;
    EXPECT_TRUE(h.waitFor(
        [&] {
            for (const auto &st : h.drainStreamStates())
                if (st.pad_index == 0 && st.state == "active")
                    active = true;
            return active;
        },
        3000))
        << "a source that is delivering again was still reported as stalled";
}

} // namespace
