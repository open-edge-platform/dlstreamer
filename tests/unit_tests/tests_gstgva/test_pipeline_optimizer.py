# ==============================================================================
# Copyright (C) 2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================

import unittest
from optimizer import get_optimized_pipeline
from utils import get_model_path

class TestOptimizer(unittest.TestCase):
    
    def test_optimizer_works(self):
        model_path = get_model_path("yolo11s")
        pipeline = f"urisourcebin buffer-size=4096 uri=https://videos.pexels.com/video-files/1192116/1192116-sd_640_360_30fps.mp4 ! decodebin ! gvadetect model={model_path} ! queue ! gvawatermark ! vah264enc ! h264parse ! mp4mux ! fakesink"
        optimized_pipeline, fps = get_optimized_pipeline(pipeline, search_duration=100)
    
        self.assertIsNotNone(optimized_pipeline, "Optimizer did not return optimized pipeline")
        self.assertIsNotNone(fps, "Optimizer did not return FPS value")
        self.assertGreater(fps, 0, f"FPS should be greater than 0, but got: {fps}")

if __name__ == '__main__':
    unittest.main()
