/*******************************************************************************
 * Copyright (C) 2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

/* These tests build real pipelines, so GStreamer has to be initialised before
 * any of them runs -- which gtest_main does not do. */

#include <gst/gst.h>
#include <gtest/gtest.h>

int main(int argc, char **argv) {
    gst_init(&argc, &argv);
    ::testing::InitGoogleTest(&argc, argv);
    int status = RUN_ALL_TESTS();
    gst_deinit();
    return status;
}
