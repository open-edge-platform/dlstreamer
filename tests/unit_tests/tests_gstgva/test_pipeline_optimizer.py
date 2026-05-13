# ==============================================================================
# Copyright (C) 2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================

import unittest
import time
import signal
import re
from optimizer import DLSOptimizer # pylint: disable=no-name-in-module
from utils import get_model_path
from openvino import Core

class TestOptimizer(unittest.TestCase):
    
    def setUp(self):
        self.model_path = get_model_path("yolo11s")
        self.simple_pipeline = f"urisourcebin buffer-size=4096 uri=https://videos.pexels.com/video-files/1192116/1192116-sd_640_360_30fps.mp4 ! decodebin ! gvadetect model={self.model_path} ! queue ! gvawatermark ! fakesink"
        self.complex_pipeline = f"urisourcebin buffer-size=4096 uri=https://videos.pexels.com/video-files/1192116/1192116-sd_640_360_30fps.mp4 name=src1 ! decodebin ! gvadetect model={self.model_path} ! gvawatermark ! " \
                               f"fakesink urisourcebin buffer-size=4096 uri=https://videos.pexels.com/video-files/1192116/1192116-sd_640_360_30fps.mp4 name=src2 ! decodebin ! gvadetect model={self.model_path} ! gvawatermark ! fakesink"

    def test_iter_optimize_for_fps_and_get_optimal_pipeline(self):
        """Test iter_optimize_for_fps with simple CPU pipeline and check candidate modifications"""
        optimizer = DLSOptimizer()
        candidates = []
        timeout = 60  # 1 minute in seconds
        start_time = time.time()
        
        # Iterate through candidates and collect pipelines and their FPS
        for pipeline, fps in optimizer.iter_optimize_for_fps(self.simple_pipeline):  
            candidates.append((pipeline, fps))  
            print(f"Tested: {pipeline} @ {fps} FPS")
            
            # Check timeout
            if time.time() - start_time > timeout:
                print(f"Timeout reached after {timeout} seconds")
                break

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
        
        elapsed_time = time.time() - start_time
        print(f"✓ Test passed: Found {len(candidates)} candidates in {elapsed_time:.1f} seconds, "
            f"best matches optimal pipeline")

    def test_iter_optimize_for_streams_with_timeout_and_get_baseline_pipeline(self):
        """Test iter_optimize_for_streams with timeout and check if stream count changes"""
        optimizer = DLSOptimizer()
        candidates = []
        timeout_duration =600  # 6 minutes in seconds
        start_time = time.time()
        timeout_reached = False

        print(f"Starting optimization with {timeout_duration/60:.1f} minute timeout...")

        try:
            # Iterate through candidates with timeout check
            for pipeline, fps_count, stream_count in optimizer.iter_optimize_for_streams(self.simple_pipeline):
                candidates.append((pipeline, stream_count, fps_count))
                print(f"Tested: {pipeline} @ {stream_count} streams @ {fps_count} FPS")
                
                # Check if timeout reached
                elapsed_time = time.time() - start_time
                if elapsed_time >= timeout_duration:
                    print(f"Timeout reached after {elapsed_time/60:.1f} minutes")
                    timeout_reached = True
                    break         
        except StopIteration:
            print("Optimization completed naturally (all candidates tested)")
        except Exception as e:
            print(f"Optimization stopped due to error: {e}")

        elapsed_time = time.time() - start_time
        print(f"Optimization finished after {elapsed_time/60:.1f} minutes")
        print(f"Total candidates collected: {len(candidates)}")

        self.assertGreater(len(candidates), 0, 
                        "No candidates were collected during optimization") 
        stream_counts = [candidate[1] for candidate in candidates]
        unique_stream_counts = set(stream_counts)

        self.assertGreater(len(unique_stream_counts), 1,
                        f"Stream counts didn't vary. All candidates had same stream count: {stream_counts}")

        print(f"✓ Stream counts varied: {sorted(unique_stream_counts)}")

        # Find the candidate with the highest stream count
        best_candidate = max(candidates, key=lambda x: x[1])
        best_candidate_pipeline, best_candidate_streams, best_candidate_fps = best_candidate
        print(f"Best candidate: {best_candidate_pipeline} @ {best_candidate_streams} streams @ {best_candidate_fps} FPS")

        # Test baseline pipeline functionality
        baseline_pipeline, baseline_fps, baseline_streams = optimizer.get_baseline_pipeline()
        print(f"Baseline pipeline: {baseline_pipeline} @ {baseline_streams} streams @ {baseline_fps} FPS")

        # Compare baseline pipeline with the original simple_pipeline
        print(f"Original pipeline: {self.simple_pipeline}")

        # Check if baseline pipeline matches the original pipeline we started with
        self.assertEqual(baseline_pipeline, self.simple_pipeline,
                        f"Baseline pipeline {baseline_pipeline} doesn't match "
                        f"original pipeline {self.simple_pipeline}")

        print(f"✓ Test passed: Collected {len(candidates)} candidates over {elapsed_time/60:.1f} minutes, "
            f"stream counts varied ({len(unique_stream_counts)} different values), "
            f"baseline matches original pipeline")

    def test_optimize_for_fps_and_get_optimal_pipeline_and_get_baseline_pipeline(self):
        """Test optimize_for_fps and get_optimal_pipeline with simple CPU pipeline"""
        optimizer = DLSOptimizer()
        optimized_pipeline, fps = optimizer.optimize_for_fps(self.simple_pipeline, 60)
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

        # Get the baseline pipeline,fps and stream count from the optimizer
        baseline_pipeline, baseline_fps, baseline_streams = optimizer.get_baseline_pipeline()
        print(f"Baseline pipeline: {baseline_pipeline} @ {baseline_streams} streams @{baseline_fps} fps")

        # Compare baseline pipeline with the original simple_pipeline
        print(f"Original pipeline: {self.simple_pipeline}")

        # Check if baseline pipeline matches the original pipeline we started with
        self.assertEqual(baseline_pipeline, self.simple_pipeline,
                        f"Baseline pipeline {baseline_pipeline} doesn't match "
                        f"original pipeline {self.simple_pipeline}")

        print(f"✓ Test passed: Optimized pipeline matches optimal pipeline with FPS {fps}")

    def test_optimize_for_streams_and_get_optimal_pipeline(self):
        """Test optimize_for_streams and get_optimal_pipeline with simple CPU pipeline"""
        optimizer = DLSOptimizer()
        optimized_pipeline, fps, streams = optimizer.optimize_for_streams(self.simple_pipeline,120)
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

    def test_set_sample_duration_with_optimize_for_fps(self):
        """Test that set_sample_duration() affects sampling time per candidate"""
        
        short_duration = 5
        long_duration = 15
        
        # Test short duration
        optimizer1 = DLSOptimizer()
        optimizer1.set_sample_duration(short_duration)
        
        candidates_short = []
        start_time = time.time()
        
        for pipeline, fps in optimizer1.optimize_for_fps(self.simple_pipeline,60):
            candidates_short.append((pipeline, fps))
        
        elapsed_time_short = time.time() - start_time
        
        # Test long duration
        optimizer2 = DLSOptimizer()
        optimizer2.set_sample_duration(long_duration)
        
        candidates_long = []
        start_time = time.time()
        
        for pipeline, fps in optimizer2.optimize_for_fps(self.simple_pipeline,60):
            candidates_long.append((pipeline, fps))
        
        elapsed_time_long = time.time() - start_time
        
        # Assertions
        # Long duration should take more time than short duration
        self.assertGreater(elapsed_time_long, elapsed_time_short,
                        f"Long duration ({elapsed_time_long:.1f}s) should be greater than short duration ({elapsed_time_short:.1f}s)")
        
        # Number of candidates should be the same (iter function tests all candidates regardless of sample_duration)
        self.assertEqual(len(candidates_short), len(candidates_long),
                        f"Both should test same number of candidates: {len(candidates_short)} == {len(candidates_long)}")
        
        print(f"Short: {len(candidates_short)} candidates in {elapsed_time_short:.1f}s")
        print(f"Long: {len(candidates_long)} candidates in {elapsed_time_long:.1f}s")
        print(f"✓ Test passed")

    def test_set_allowed_devices(self):
        """Test that set_allowed_devices() excludes specified devices from optimization"""

        # Get all available devices from OpenVINO
        core = Core()
        all_devices = core.available_devices
        print(f"All available devices: {all_devices}")
        
        # Skip test if not enough devices
        if len(all_devices) < 2:
            self.skipTest(f"Need at least 2 devices for testing, found: {all_devices}")
        
        # Select subset - exclude last device
        allowed_devices = all_devices[:-1]  # all except last
        excluded_device = all_devices[-1]   # last device to exclude
        
        print(f"Allowed devices: {allowed_devices}")
        print(f"Excluded device: {excluded_device}")
        
        # Set up optimizer with device restriction
        optimizer = DLSOptimizer()
        optimizer.set_allowed_devices(allowed_devices)
        
        # Collect candidates to see what devices are being tested
        candidates = []
        iteration_count = 0
        for pipeline, fps in optimizer.optimize_for_fps(self.simple_pipeline,60):
            candidates.append((pipeline, fps))
            print(f"Tested: {pipeline} @ {fps} FPS")
        
        # Assertions
        self.assertGreater(len(candidates), 0, "Should test at least one candidate")
        
        # Verify excluded device does not appear in any pipeline
        for pipeline, fps in candidates:
            self.assertNotIn(excluded_device, pipeline, 
                        f"Excluded device '{excluded_device}' found in pipeline: {pipeline}")
        
        # Verify at least one allowed device appears in pipelines
        allowed_device_found = False
        for pipeline, fps in candidates:
            for allowed_device in allowed_devices:
                if allowed_device in pipeline:
                    allowed_device_found = True
                    break
            if allowed_device_found:
                break
        
        self.assertTrue(allowed_device_found, 
                    f"None of the allowed devices {allowed_devices} found in any pipeline")
        
        print(f"✓ Test passed: Excluded device '{excluded_device}' not found in any pipeline")
        print(f"✓ Only allowed devices {allowed_devices} were used")

    def test_enable_cross_stream_batching(self):
        """Test that enable_cross_stream_batching works and sets instance-id"""

        optimizer = DLSOptimizer()
        optimizer.enable_cross_stream_batching(True)
        start_time = time.time()
        timeout = 10

        found_instance_ids = False

        for candidate_result in optimizer.iter_optimize_for_fps(self.complex_pipeline):
            candidate_pipeline = candidate_result[0]
            print(f"Pipeline: {candidate_pipeline}")
            model_instance_ids = re.findall(r'model-instance-id=(\w+)', candidate_pipeline)
            
            if len(model_instance_ids) > 1 and len(set(model_instance_ids)) == 1:
                found_instance_ids = True
                print(f"✓ Found same model-instance-id: {model_instance_ids[0]}")
                break
            elif len(model_instance_ids) > 0:
                print(f"Found different instance-ids: {model_instance_ids}")
            else:
                print("No model-instance-id found")

            if time.time() - start_time > timeout:
                print("Timeout reached")
                break

        self.assertTrue(found_instance_ids, "Should find candidates with same model-instance-id for cross-stream batching")
        print("✓ Cross-stream batching test passed!")

if __name__ == '__main__':
    unittest.main()
