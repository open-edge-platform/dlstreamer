# ==============================================================================
# Copyright (C) 2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================

import unittest
import time
import re
from optimizer import DLSOptimizer # pylint: disable=no-name-in-module
from utils import get_model_path

class TestOptimizer(unittest.TestCase):
    
    def setUp(self):
        self.model_path = get_model_path("yolo11s")
        self.simple_pipeline = f"urisourcebin buffer-size=4096 uri=https://videos.pexels.com/video-files/1192116/1192116-sd_640_360_30fps.mp4 ! decodebin ! gvadetect model={self.model_path} ! queue ! gvawatermark ! fakesink"

    def test_iter_optimize_for_fps_and_get_optimal_pipeline(self):
        """Test iter_optimize_for_fps with simple CPU pipeline and check candidate modifications"""
        optimizer = DLSOptimizer()
        candidates = []
        # Iterate through candidates and collect pipelines and their FPS
        for candidate_result in optimizer.iter_optimize_for_fps(self.simple_pipeline):
            if isinstance(candidate_result, tuple) and len(candidate_result) >= 2:
                pipeline = candidate_result[0]
                fps = candidate_result[1]
                candidates.append((pipeline, fps))
                print(f"Tested: {pipeline} @ {fps} FPS")
            else:
                continue
        # We expect to have multiple candidates tested, at least more than 1
        self.assertGreater(len(candidates), 1, 
                        f"Expected more than 1 tested pipeline, got {len(candidates)}")
        # Find the candidate with the highest FPS
        best_candidate = max(candidates, key=lambda x: x[1])
        best_candidate_pipeline, best_candidate_fps = best_candidate
        
        print(f"Best from candidates: {best_candidate_pipeline} @ {best_candidate_fps} FPS")
        # Get the optimal pipeline and FPS from the optimizer
        optimal_pipeline, optimal_fps, _ = optimizer.get_optimal_pipeline()
        print(f"Optimal pipeline: {optimal_pipeline} @ {optimal_fps} FPS")
        # Assert that the best candidate matches the optimal pipeline and FPS (allowing some tolerance for FPS)
        self.assertEqual(best_candidate_pipeline, optimal_pipeline,
                        f"Best candidate pipeline {best_candidate_pipeline} doesn't match "
                        f"optimal pipeline {optimal_pipeline}")
        # Allow a small tolerance for FPS comparison, since it can vary slightly due to system load and other factors
        self.assertAlmostEqual(best_candidate_fps, optimal_fps, places=2,
                            msg=f"FPS mismatch: candidate {best_candidate_fps} vs optimal {optimal_fps}")
        
        print(f"✓ Test passed: Found {len(candidates)} candidates, "
            f"best matches optimal pipeline")

    # def test_iter_optimize_for_streams_and_get_baseline_pipeline(self):
    #     """Test iter_optimize_for_streams with simple CPU pipeline and check baseline against original"""
    #     optimizer = DLSOptimizer()
    #     search_duration = 30  # seconds
    #     sample_duration = 10  # seconds
    #     candidates = []

    #     # Iterate through candidates and collect pipelines and their stream counts
    #     for candidate_result in optimizer.iter_optimize_for_streams(self.simple_pipeline):
    #         if isinstance(candidate_result, tuple) and len(candidate_result) >= 2:
    #             pipeline = candidate_result[0]
    #             stream_count = candidate_result[2]
    #             fps_count = candidate_result[1]
    #             candidates.append((pipeline, stream_count, fps_count))
    #             print(f"Tested: {pipeline} @ {stream_count} streams @ {fps_count} FPS")
    #         else:
    #             continue

    #     # We expect to have multiple candidates tested, at least more than 1
    #     self.assertGreater(len(candidates), 1, 
    #                     f"Expected more than 1 tested pipeline, got {len(candidates)}")
        
    #     # Find the candidate with the highest stream count
    #     best_candidate = max(candidates, key=lambda x: x[1])
    #     best_candidate_pipeline, best_candidate_streams, best_candidate_fps = best_candidate
        
    #     print(f"Best from candidates: {best_candidate_pipeline} @ {best_candidate_streams} streams @ {best_candidate_fps} FPS")
        
    #     # Get the baseline pipeline and stream count from the optimizer
    #     baseline_pipeline, baseline_streams = optimizer.get_baseline_pipeline()
    #     print(f"Baseline pipeline: {baseline_pipeline} @ {baseline_streams} streams")
        
    #     # Compare baseline pipeline with the original simple_pipeline
    #     print(f"Original pipeline: {self.simple_pipeline}")
        
    #     # Check if baseline pipeline matches the original pipeline we started with
    #     self.assertEqual(baseline_pipeline, self.simple_pipeline,
    #                     f"Baseline pipeline {baseline_pipeline} doesn't match "
    #                     f"original pipeline {self.simple_pipeline}")
        
    #     # Verify that optimization actually found better candidates than baseline
    #     self.assertGreaterEqual(best_candidate_streams, baseline_streams,
    #                         f"Best candidate streams {best_candidate_streams} should be >= "
    #                         f"baseline streams {baseline_streams}")
        
    #     print(f"✓ Test passed: Found {len(candidates)} candidates, "
    #         f"baseline matches original pipeline, best candidate has {best_candidate_streams} streams @ {best_candidate_fps} FPS "
    #         f"vs baseline {baseline_streams} streams")

    def test_optimize_for_fps_and_get_optimal_pipeline(self):
        """Test optimize_for_fps and get_optimal_pipeline with simple CPU pipeline"""
        optimizer = DLSOptimizer()
        optimized_pipeline, fps = optimizer.optimize_for_fps(self.simple_pipeline, 100)
        self.assertIsNotNone(optimized_pipeline, "Optimizer did not return optimized pipeline")
        self.assertIsNotNone(fps, "Optimizer did not return FPS value")
        self.assertGreater(fps, 0, f"FPS should be greater than 0, but got: {fps}")
        
        optimal_pipeline, optimal_fps, _ = optimizer.get_optimal_pipeline()
        print(f"Optimal pipeline: {optimal_pipeline} @ {optimal_fps} FPS")
        
        # Check that the optimal pipeline matches the one returned by optimize_for_fps
        self.assertEqual(optimal_pipeline, optimized_pipeline,
                        f"Optimal pipeline {optimal_pipeline} doesn't match "
                        f"optimized pipeline {optimized_pipeline}")
        
        # Allow a small tolerance for FPS comparison
        self.assertAlmostEqual(optimal_fps, fps, places=2,
                            msg=f"FPS mismatch: optimized {fps} vs optimal {optimal_fps}")

        print(f"✓ Test passed: Optimized pipeline matches optimal pipeline with FPS {fps}")

    def test_optimize_for_streams_and_get_optimal_pipeline(self):
        """Test optimize_for_streams and get_optimal_pipeline with simple CPU pipeline"""
        optimizer = DLSOptimizer()
        optimized_pipeline, fps, streams = optimizer.optimize_for_streams(self.simple_pipeline,180)
        self.assertIsNotNone(optimized_pipeline, "Optimizer did not return optimized pipeline")
        self.assertIsNotNone(fps, "Optimizer did not return FPS value")
        self.assertGreater(fps, 0, f"FPS should be greater than 0, but got: {fps}")
        self.assertGreater(streams, 0, f"Streams should be greater than 0, but got: {streams}")

        optimal_pipeline, optimal_fps, optimal_streams = optimizer.get_optimal_pipeline()
        print(f"Optimal pipeline: {optimal_pipeline} @ {optimal_fps} FPS @ {optimal_streams} STREAMS")
        
        # Check that the optimal pipeline matches the one returned by optimize_for_fps
        self.assertEqual(optimal_pipeline, optimized_pipeline,
                        f"Optimal pipeline {optimal_pipeline} doesn't match "
                        f"optimized pipeline {optimized_pipeline}")
        
        # Allow a small tolerance for FPS comparison
        self.assertAlmostEqual(optimal_fps, fps, places=2,
                            msg=f"FPS mismatch: optimized {fps} vs optimal {optimal_fps}")

        # Check that the number of streams is equal
        self.assertEqual(streams, optimal_streams,
                        f"Streams from optimal pipeline {optimal_streams} doesn't match "
                        f"streams from optimized pipeline {streams}")

        print(f"✓ Test passed: Optimized pipeline matches optimal pipeline with FPS {fps}")

if __name__ == '__main__':
    unittest.main()
