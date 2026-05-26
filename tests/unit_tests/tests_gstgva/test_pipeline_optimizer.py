# ==============================================================================
# Copyright (C) 2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================

"""Unit tests for the DL Streamer optimizer."""

import unittest
from optimizer import DLSOptimizer # pylint: disable=no-name-in-module
from utils import get_model_path


class TestOptimizer(unittest.TestCase):
    """Validate optimizer smoke-test behavior."""

    def test_optimizer_works(self):
        """Check that the optimizer returns a candidate pipeline and FPS."""
        model_path = get_model_path("yolo11s")
        pipeline = (
            "urisourcebin buffer-size=4096 "
            "uri=https://videos.pexels.com/video-files/1192116/1192116-sd_640_360_30fps.mp4 "
            f"! decodebin ! gvadetect model={model_path} ! queue ! gvawatermark "
            "! vah264enc ! h264parse ! mp4mux ! fakesink"
        )
        optimizer = DLSOptimizer()
        optimized_pipeline, fps = optimizer.optimize_for_fps(pipeline, 45)

        self.assertIsNotNone(optimized_pipeline, "Optimizer did not return optimized pipeline")
        self.assertIsNotNone(fps, "Optimizer did not return FPS value")
        self.assertGreater(fps, 0, f"FPS should be greater than 0, but got: {fps}")


if __name__ == '__main__':
    unittest.main()
