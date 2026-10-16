/*******************************************************************************
 * Copyright (C) 2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

/* Routing, and what happens to a source nobody asked for.
 *
 * The demuxer only ever sees buffers the muxer tagged, so these run both
 * elements. Each branch gets its own queue, because the demuxer pushes to all
 * of them from one thread and they cannot otherwise preroll independently.
 */

#include "../test_streammux/streammux_harness.h"

#include <gtest/gtest.h>

namespace {

constexpr GstClockTime kFrame = StreammuxHarness::kFrameDuration;

struct DemuxMode : public ::testing::TestWithParam<const char *> {};

/* Each source has to come out of its own branch and no other. */
TEST_P(DemuxMode, EachSourceLeavesByItsOwnBranch) {
    StreammuxHarness h(GetParam(), /*with_demux=*/true);
    h.addSource(0);
    h.addSource(1);
    h.addBranch(0);
    h.addBranch(1);
    h.start();

    for (int i = 0; i < 10; i++) {
        h.push(0, i * kFrame);
        h.push(1, i * kFrame);
    }

    EXPECT_TRUE(h.waitFor([&] { return h.branchCount(0) >= 5 && h.branchCount(1) >= 5; }))
        << "branch 0 got " << h.branchCount(0) << ", branch 1 got " << h.branchCount(1);
    EXPECT_FALSE(h.sawErrorOnBus());
}

/* A source with no branch is the application's choice, not an error. It used
 * to fail the whole buffer in passthrough mode, which stopped every other
 * source with it. */
TEST_P(DemuxMode, ASourceWithNoBranchDoesNotStopTheOthers) {
    StreammuxHarness h(GetParam(), /*with_demux=*/true);
    h.addSource(0);
    h.addSource(1);
    h.addBranch(0); /* nothing is requested for source 1 */
    h.start();

    for (int i = 0; i < 20; i++) {
        h.push(0, i * kFrame);
        h.push(1, i * kFrame);
    }

    EXPECT_TRUE(h.waitFor([&] { return h.branchCount(0) >= 10; }))
        << "the routed branch received only " << h.branchCount(0) << " buffers";
    EXPECT_FALSE(h.sawErrorOnBus()) << "an unrouted source was treated as an error";
}

/* The same, reached by taking a branch away while its source keeps running --
 * which is what a supervisor does when it stops consuming one feed. */
TEST_P(DemuxMode, RemovingABranchDoesNotStopTheOthers) {
    StreammuxHarness h(GetParam(), /*with_demux=*/true);
    h.addSource(0);
    h.addSource(1);
    h.addBranch(0);
    h.addBranch(1);
    h.start();

    for (int i = 0; i < 10; i++) {
        h.push(0, i * kFrame);
        h.push(1, i * kFrame);
    }
    ASSERT_TRUE(h.waitFor([&] { return h.branchCount(0) >= 5 && h.branchCount(1) >= 5; }));

    GstPad *pad = gst_element_get_static_pad(h.demux(), "src_1");
    ASSERT_NE(pad, nullptr);
    gst_element_release_request_pad(h.demux(), pad);
    gst_object_unref(pad);

    guint before = h.branchCount(0);
    for (int i = 10; i < 30; i++) {
        h.push(0, i * kFrame);
        h.push(1, i * kFrame);
    }

    EXPECT_TRUE(h.waitFor([&] { return h.branchCount(0) > before + 5; }))
        << "the surviving branch stopped after the other one was removed";
    EXPECT_FALSE(h.sawErrorOnBus());
}

INSTANTIATE_TEST_SUITE_P(OutputModes, DemuxMode, ::testing::Values("passthrough", "container"),
                         [](const ::testing::TestParamInfo<const char *> &info) { return std::string(info.param); });

} // namespace
