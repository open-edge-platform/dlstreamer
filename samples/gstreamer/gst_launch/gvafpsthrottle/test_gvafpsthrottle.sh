#!/bin/bash
# ==============================================================================
# Copyright (C) 2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================

# Test script for gvafpsthrottle element
# This script demonstrates how to use the gvafpsthrottle element in a pipeline
# Each test runs for 20 seconds

DURATION=20

echo "gvafpsthrottle element test script"
echo "==================================="
echo "Each test will run for ${DURATION} seconds"
echo ""

echo "Test 1: videotestsrc at 10 FPS"
echo "-------------------------------"
echo "Command: gst-launch-1.0 videotestsrc ! gvafpsthrottle target-fps=10 ! gvafpscounter ! fakesink"
timeout ${DURATION}s gst-launch-1.0 videotestsrc ! gvafpsthrottle target-fps=10 ! gvafpscounter ! fakesink 2>&1
echo ""

echo "Test 2: videorate element at 10 FPS (requires sync=true)"
echo "---------------------------------------------------------"
echo "Command: gst-launch-1.0 videotestsrc ! videorate ! video/x-raw,framerate=10/1 ! gvafpscounter ! fakesink sync=true"
timeout ${DURATION}s gst-launch-1.0 videotestsrc ! videorate ! video/x-raw,framerate=10/1 ! gvafpscounter ! fakesink sync=true 2>&1
echo ""

echo "All tests completed!"
