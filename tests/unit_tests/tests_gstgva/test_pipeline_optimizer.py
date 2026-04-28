# ==============================================================================
# Copyright (C) 2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================

import unittest
import time
from optimizer import DLSOptimizer # pylint: disable=no-name-in-module
from utils import get_model_path

class TestOptimizer(unittest.TestCase):
    
    def setUp(self):
        self.model_path = get_model_path("yolo11s")
        self.simple_pipeline = f"urisourcebin buffer-size=4096 uri=https://videos.pexels.com/video-files/1192116/1192116-sd_640_360_30fps.mp4 ! decodebin ! gvadetect model={self.model_path} device=CPU batch-size=1 nireq=1 ! queue ! gvawatermark ! vah264enc ! h264parse ! mp4mux ! fakesink"
        self.optimizer = DLSOptimizer()
    
    def test_optimizer_works(self):
        optimized_pipeline, fps = self.optimizer.optimize_for_fps(self.simple_pipeline, 100)
    
        self.assertIsNotNone(optimized_pipeline, "Optimizer did not return optimized pipeline")
        self.assertIsNotNone(fps, "Optimizer did not return FPS value")
        self.assertGreater(fps, 0, f"FPS should be greater than 0, but got: {fps}")

    def test_iter_optimize_for_fps(self):
        """Test iter_optimize_for_fps with simple CPU pipeline and check candidate modifications"""
        candidates = []
        
        # Collect candidates from iterator
        for candidate_pipeline, fps in self.optimizer.iter_optimize_for_fps(self.simple_pipeline):
            candidates.append((candidate_pipeline, fps))
            if len(candidates) >= 3:  # Collect first 3 candidates
                break
        
        self.assertGreater(len(candidates), 0, "No candidates generated")
        
        # Check that candidates modify device, batch-size, and nireq
        device_modified = False
        batch_size_modified = False
        nireq_modified = False
        
        for candidate_pipeline, _ in candidates:
            if "device=GPU" in candidate_pipeline or "device=NPU" in candidate_pipeline:
                device_modified = True
            if "batch-size=2" in candidate_pipeline or "batch-size=4" in candidate_pipeline:
                batch_size_modified = True
            if "nireq=2" in candidate_pipeline or "nireq=4" in candidate_pipeline:
                nireq_modified = True
        
        self.assertTrue(device_modified, "Candidates should modify device attribute")
        self.assertTrue(batch_size_modified, "Candidates should modify batch-size attribute")
        self.assertTrue(nireq_modified, "Candidates should modify nireq attribute")

    def test_iter_optimize_for_streams(self):
        """Test iter_optimize_for_streams and check stream values > 1"""
        candidates = []
        
        # Collect candidates from iterator
        for candidate_pipeline, streams in self.optimizer.iter_optimize_for_streams(self.simple_pipeline):
            candidates.append((candidate_pipeline, streams))
            if len(candidates) >= 3:
                break
        
        self.assertGreater(len(candidates), 0, "No candidates generated")
        
        # Check that candidates have stream values > 1
        stream_values_found = []
        for _, streams in candidates:
            stream_values_found.append(streams)
        
        self.assertTrue(any(streams > 1 for streams in stream_values_found), 
                       "Candidates should include stream values > 1")

    def test_get_optimal_pipeline(self):
        """Test get_optimal_pipeline returns GPU pipeline with batch size & nireq > 1"""
        # Run optimization first
        list(self.optimizer.iter_optimize_for_fps(self.simple_pipeline))
        
        optimal_pipeline = self.optimizer.get_optimal_pipeline()
        
        self.assertIsNotNone(optimal_pipeline, "Optimal pipeline should not be None")
        self.assertIn("device=GPU", optimal_pipeline, "Optimal pipeline should run on GPU")
        
        # Check for batch-size > 1
        batch_size_found = False
        nireq_found = False
        
        if "batch-size=2" in optimal_pipeline or "batch-size=4" in optimal_pipeline or "batch-size=8" in optimal_pipeline:
            batch_size_found = True
        if "nireq=2" in optimal_pipeline or "nireq=4" in optimal_pipeline or "nireq=8" in optimal_pipeline:
            nireq_found = True
            
        self.assertTrue(batch_size_found, "Optimal pipeline should have batch-size > 1")
        self.assertTrue(nireq_found, "Optimal pipeline should have nireq > 1")

    def test_get_baseline_pipeline(self):
        """Test get_baseline_pipeline returns exact same pipeline as input"""
        # Run optimization first
        list(self.optimizer.iter_optimize_for_fps(self.simple_pipeline))
        
        baseline_pipeline = self.optimizer.get_baseline_pipeline()
        
        self.assertEqual(baseline_pipeline, self.simple_pipeline, 
                        "Baseline pipeline should be identical to input pipeline")

    def test_optimize_for_fps_different_durations(self):
        """Test optimize_for_fps with different search durations"""
        short_duration = 60   # seconds
        long_duration = 120   # seconds
        
        # Test short duration
        start_time = time.time()
        optimized_pipeline_short, fps_short = self.optimizer.optimize_for_fps(
            self.simple_pipeline, short_duration)
        short_elapsed = time.time() - start_time
        
        # Reset optimizer for clean test
        self.setUp()
        
        # Test long duration
        start_time = time.time()
        optimized_pipeline_long, fps_long = self.optimizer.optimize_for_fps(
            self.simple_pipeline, long_duration)
        long_elapsed = time.time() - start_time
        
        # Verify both return valid results
        self.assertIsNotNone(optimized_pipeline_short)
        self.assertIsNotNone(optimized_pipeline_long)
        self.assertGreater(fps_short, 0)
        self.assertGreater(fps_long, 0)
        
        # Verify different durations result in different run times
        self.assertLess(short_elapsed, long_elapsed, 
                    "Short duration should take less time than long duration")
        
        # Verify that different durations result in different optimized pipelines
        self.assertNotEqual(optimized_pipeline_short, optimized_pipeline_long,
                        "Different search durations should result in different optimized pipelines")
        
        # Verify that different durations result in different FPS values
        self.assertNotEqual(fps_short, fps_long,
                        "Different search durations should result in different FPS values")

    def test_optimize_for_streams_different_durations(self):
        """Test optimize_for_streams with different search durations"""
        short_duration = 60   # seconds
        long_duration = 120   # seconds
        
        # Test short duration
        start_time = time.time()
        optimized_pipeline_short, streams_short = self.optimizer.optimize_for_streams(
            self.simple_pipeline, short_duration)
        short_elapsed = time.time() - start_time
        
        # Reset optimizer for clean test
        self.setUp()
        
        # Test long duration
        start_time = time.time()
        optimized_pipeline_long, streams_long = self.optimizer.optimize_for_streams(
            self.simple_pipeline, long_duration)
        long_elapsed = time.time() - start_time
        
        # Verify both return valid results
        self.assertIsNotNone(optimized_pipeline_short)
        self.assertIsNotNone(optimized_pipeline_long)
        self.assertGreater(streams_short, 0)
        self.assertGreater(streams_long, 0)
        
        # Verify different durations result in different run times
        self.assertLess(short_elapsed, long_elapsed, 
                    "Short duration should take less time than long duration")
        
        # Verify that different durations result in different optimized pipelines
        self.assertNotEqual(optimized_pipeline_short, optimized_pipeline_long,
                        "Different search durations should result in different optimized pipelines")
        
        # Verify that different durations result in different stream values
        self.assertNotEqual(streams_short, streams_long,
                        "Different search durations should result in different stream values")

    def test_set_sample_duration(self):
        """Test set_sample_duration with different values"""
        short_sample = 10  # seconds
        long_sample = 30   # seconds
        
        # Test with short sample duration
        self.optimizer.set_sample_duration(short_sample)
        start_time = time.time()
        candidates_short = list(self.optimizer.iter_optimize_for_fps(self.simple_pipeline))
        short_elapsed = time.time() - start_time
        
        self.setUp()  # Reset optimizer to clean state
        
        # Test with long sample duration
        self.optimizer.set_sample_duration(long_sample)
        start_time = time.time()
        candidates_long = list(self.optimizer.iter_optimize_for_fps(self.simple_pipeline))
        long_elapsed = time.time() - start_time
        
        # Both should return candidates
        self.assertGreater(len(candidates_short), 0)
        self.assertGreater(len(candidates_long), 0)
        
        # Different sample durations should result in different run times
        # Note: This might be approximate due to overhead
        self.assertNotEqual(short_elapsed, long_elapsed, 
                           "Different sample durations should result in different run times")

    def test_set_allowed_devices(self):
        """Test set_allowed_devices limits device usage in candidates"""
        # Set allowed devices to only CPU
        allowed_devices = ["CPU"]
        self.optimizer.set_allowed_devices(allowed_devices)
        
        candidates = []
        for candidate_pipeline, _ in self.optimizer.iter_optimize_for_fps(self.simple_pipeline):
            candidates.append(candidate_pipeline)
            if len(candidates) >= 3:
                break
        
        self.assertGreater(len(candidates), 0, "No candidates generated")
        
        # Check that only allowed devices are used
        for candidate in candidates:
            device_found = False
            for device in allowed_devices:
                if f"device={device}" in candidate:
                    device_found = True
                    break
            
            # Should not contain disallowed devices (e.g., GPU)
            self.assertNotIn("device=GPU", candidate, 
                           "Candidates should not use disallowed devices")

if __name__ == '__main__':
    unittest.main()
