/*******************************************************************************
 * Copyright (C) 2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

/* When the muxer should and should not end the stream.
 *
 * These two belong together: the first is the bug from issue #1161, where
 * releasing a finished source ended the whole pipeline, and the second is the
 * guard against over-correcting it into a muxer that never ends anything.
 */

#include "streammux_harness.h"

#include <gtest/gtest.h>

namespace {

/* Issue #1161: releasing a pad that has already reached EOS must not end the
 * stream for the sources that are still running. */
TEST(StreammuxEos, ReleasingAFinishedSourceDoesNotEndTheStream) {
    StreammuxHarness h;
    h.addSource(0);
    h.addSource(1);
    h.start();

    for (int i = 0; i < 5; i++) {
        h.push(0, i * StreammuxHarness::kFrameDuration);
        h.push(1, i * StreammuxHarness::kFrameDuration);
    }
    ASSERT_TRUE(h.waitFor([&] { return h.outputCount() >= 4; }));

    /* Source 0 finishes and the application takes its pad away. */
    h.pushEos(0);
    h.releaseSource(0);

    /* Let the queues drain. The muxer only decides whether the stream is over
     * when it has nothing left to batch, so a test that keeps pushing never
     * reaches the decision it is trying to check. */
    guint settled = 0;
    ASSERT_TRUE(h.waitFor([&] {
        guint now = h.outputCount();
        if (now == settled)
            return true;
        settled = now;
        return false;
    }));

    EXPECT_EQ(h.srcEventCount(GST_EVENT_EOS), 0u) << "the muxer ended the stream while a source was still running";

    /* And it has to still be working, not merely quiet. */
    for (int i = 5; i < 15; i++)
        h.push(1, i * StreammuxHarness::kFrameDuration);
    EXPECT_TRUE(h.waitFor([&] { return h.outputCount() > settled; })) << "the surviving source stopped being delivered";
    EXPECT_EQ(h.srcEventCount(GST_EVENT_EOS), 0u);
    EXPECT_FALSE(h.sawErrorOnBus());
}

/* The mirror image: once every source really has finished, EOS has to go out,
 * or a pipeline built on this element would never shut down. */
TEST(StreammuxEos, StreamEndsWhenEverySourceHasFinished) {
    StreammuxHarness h;
    h.addSource(0);
    h.addSource(1);
    h.start();

    for (int i = 0; i < 5; i++) {
        h.push(0, i * StreammuxHarness::kFrameDuration);
        h.push(1, i * StreammuxHarness::kFrameDuration);
    }
    h.pushEos(0);
    h.pushEos(1);

    EXPECT_TRUE(h.waitFor([&] { return h.srcEventCount(GST_EVENT_EOS) > 0; }))
        << "every source finished but the muxer never ended the stream";
    EXPECT_FALSE(h.sawErrorOnBus());
}

/* A source that finishes before it ever reported caps leaves the muxer unable
 * to negotiate. It still has to end the stream properly rather than pushing a
 * bare EOS onto a pad downstream has never been told anything about. */
TEST(StreammuxEos, EndingWithoutCapsStillSendsTheOpeningEvents) {
    StreammuxHarness h;
    h.addSource(0);
    h.addSource(1);
    h.start();

    h.pushEos(0);
    h.pushEos(1);

    ASSERT_TRUE(h.waitFor([&] { return h.srcEventCount(GST_EVENT_EOS) > 0; }));
    EXPECT_GE(h.srcEventCount(GST_EVENT_SEGMENT), 1u);

    /* It has to be the muxer's own stream-start. Seeing any stream-start
     * proves nothing: a source's own used to be forwarded straight through,
     * which left downstream holding an id for one of the inputs rather than
     * for the stream the muxer presents. */
    EXPECT_TRUE(h.sawOwnStreamStart()) << "the stream ended without the muxer ever identifying it";
}

} // namespace
