/*******************************************************************************
 * Copyright (C) 2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

/* How batch time behaves when sources misbehave.
 *
 * Every case is driven by timestamps the test chooses, so what is being
 * checked is visible in the test rather than inferred from a measurement. Each
 * one also arranges the moment it needs: these faults only show up when a
 * particular pad is the only one with anything queued, which does not happen
 * by itself if the test keeps every source fed.
 */

#include "streammux_harness.h"

#include <gtest/gtest.h>

namespace {

constexpr GstClockTime kFrame = StreammuxHarness::kFrameDuration;

/* Wait until the muxer has nothing left to push, so the next buffer pushed is
 * the only one it can choose from. */
void drain(StreammuxHarness &h) {
    guint settled = 0;
    ASSERT_TRUE(h.waitFor([&] {
        guint now = h.outputCount();
        if (now == settled && now > 0)
            return true;
        settled = now;
        return false;
    }));
}

/* Batch time must not go backwards. A source that goes quiet and then delivers
 * its arrears used to pull the anchor back behind the batch already pushed --
 * and in container mode the anchor is the output buffer's PTS, so that reached
 * downstream as time running backwards. */
TEST(StreammuxBatching, BatchTimestampsNeverGoBackwards) {
    StreammuxHarness h("container");
    h.addSource(0);
    h.addSource(1);
    h.start();

    for (int i = 0; i < 5; i++) {
        h.push(0, i * kFrame);
        h.push(1, i * kFrame);
    }
    drain(h);

    /* Source 1 alone moves the batch on, so the anchor follows it. */
    for (int i = 5; i < 12; i++)
        h.push(1, i * kFrame);
    drain(h);

    /* Source 0 then delivers frames that are behind the batch already pushed,
     * but by less than pts-tolerance. That margin matters: anything further
     * behind was already being discarded on arrival, so it never reached the
     * anchor and never showed this. These are late enough to move batch time
     * backwards and close enough to slip through. */
    const GstClockTime last = 11 * kFrame;
    const GstClockTime margin = 10 * GST_MSECOND; /* default pts-tolerance is 20 ms */
    for (int i = 0; i < 6; i++)
        h.push(0, last - margin + i * kFrame);
    drain(h);

    const auto &seen = h.batchPtsSeen();
    ASSERT_GE(seen.size(), 2u);
    for (size_t i = 1; i < seen.size(); i++)
        EXPECT_GE(seen[i], seen[i - 1]) << "batch " << i << " is timestamped before the one before it (" << seen[i]
                                        << " < " << seen[i - 1] << ")";
}

/* One buffer with an absurd timestamp used to become the anchor and leave
 * every later buffer looking stale, so the element went quiet for good -- and
 * silently, since the batching loop is also where it would have noticed.
 *
 * For the stray buffer to become the anchor it has to be the only thing
 * queued, so the test drains first and pushes it on its own. */
TEST(StreammuxBatching, AStrayFutureTimestampDoesNotSilenceTheElement) {
    StreammuxHarness h;
    h.addSource(0);
    h.addSource(1);
    h.start();

    for (int i = 0; i < 10; i++) {
        h.push(0, i * kFrame);
        h.push(1, i * kFrame);
    }
    drain(h);

    guint before = h.outputCount();
    h.push(0, 3600 * GST_SECOND); /* an hour ahead, and alone */
    ASSERT_TRUE(h.waitFor([&] { return h.outputCount() > before; })) << "the stray buffer was never pushed";

    before = h.outputCount();
    for (int i = 10; i < 40; i++) {
        h.push(0, i * kFrame);
        h.push(1, i * kFrame);
    }

    EXPECT_TRUE(h.waitFor([&] { return h.outputCount() > before + 20; }))
        << "output stopped after one stray timestamp (" << h.outputCount() - before << " buffers since)";
}

/* sync-mode=first-pts rebases each source to zero. An RTSP source that
 * reconnects starts its timestamps over, and the rebase has to continue the
 * normalized timeline rather than restart it -- a source that drops back to
 * zero while the others are seconds ahead is discarded as late, for good.
 *
 * Counted per branch, because what has to be shown is that *this* source comes
 * back, not that the muxer is still producing something. */
TEST(StreammuxBatching, ASourceSurvivesItsTimelineRestarting) {
    StreammuxHarness h("passthrough", /*with_demux=*/true);
    gst_util_set_object_arg(G_OBJECT(h.mux()), "sync-mode", "first-pts");
    h.addSource(0);
    h.addSource(1);
    h.addBranch(0);
    h.addBranch(1);
    h.start();

    const GstClockTime far = 40 * GST_SECOND; /* source 0 starts well into its own timeline */
    for (int i = 0; i < 20; i++) {
        h.push(0, far + i * kFrame);
        h.push(1, i * kFrame);
    }
    ASSERT_TRUE(h.waitFor([&] { return h.branchCount(0) >= 10; }));
    guint before = h.branchCount(0);

    /* Source 0 reconnects: timestamps from zero again, DISCONT on the first. */
    h.push(0, 0, GST_BUFFER_FLAG_DISCONT);
    for (int i = 1; i < 30; i++) {
        h.push(0, i * kFrame);
        h.push(1, (20 + i) * kFrame);
    }

    EXPECT_TRUE(h.waitFor([&] { return h.branchCount(0) > before + 10; }))
        << "the reconnected source delivered only " << h.branchCount(0) - before << " frames after restarting";
}

} // namespace
