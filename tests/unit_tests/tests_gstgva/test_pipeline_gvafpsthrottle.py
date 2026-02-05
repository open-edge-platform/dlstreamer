# ==============================================================================
# Copyright (C) 2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================

import unittest
import os

from pipeline_runner import TestGenericPipelineRunner
from utils import *

import gi

gi.require_version("Gst", "1.0")
gi.require_version("GLib", "2.0")
from gi.repository import Gst, GLib

SCRIPT_DIR = os.path.dirname(os.path.realpath(__file__))


class FpsCounterPipelineRunner(TestGenericPipelineRunner):
    """Extended pipeline runner that queries gvafpscounter avg-fps property periodically"""

    def __init__(self, methodName='runTest'):
        super().__init__(methodName)
        self.fps_values = []
        self.fps_counter_element = None
        self.timeout_id = None
        self._state = None

    def set_pipeline(self, pipeline):
        """Override to get reference to gvafpscounter element"""
        super().set_pipeline(pipeline)
        self.fps_counter_element = self._pipeline.get_by_name("fpscounter")
        if not self.fps_counter_element:
            it = self._pipeline.iterate_elements()
            while True:
                result, element = it.next()
                if result != Gst.IteratorResult.OK:
                    break
                factory = element.get_factory()
                if factory and factory.get_name() == "gvafpscounter":
                    self.fps_counter_element = element
                    break

    def run_pipeline(self):
        """Override to start periodic FPS polling"""
        self._state = self._pipeline.set_state(Gst.State.PLAYING)
        # Query avg-fps every 1 second (1000 milliseconds)
        self.timeout_id = GLib.timeout_add(1000, self.poll_fps)
        self._mainloop.run()

    def poll_fps(self):
        """Query avg-fps property from gvafpscounter element"""
        if self.fps_counter_element:
            avg_fps = self.fps_counter_element.get_property("avg-fps")
            if avg_fps > 0:
                self.fps_values.append(avg_fps)
                print(f"FPS: {avg_fps}")
        return True  # Continue calling this function

    def kill(self):
        """Override to remove timeout"""
        if self.timeout_id:
            GLib.source_remove(self.timeout_id)
            self.timeout_id = None
        super().kill()


class TestGvaFpsThrottle(unittest.TestCase):
    """Test suite for gvafpsthrottle element"""

    def _run_and_verify_fps(self, pipeline, expected_min, expected_max, expected_label):
        """Helper method to run pipeline and verify FPS is within expected range"""
        runner = FpsCounterPipelineRunner()
        runner.set_pipeline(pipeline)
        runner.run_pipeline()

        self.assertEqual(
            len(runner.exceptions), 0, "Pipeline should run without errors"
        )
        self.assertGreater(len(runner.fps_values), 0, "Should have FPS measurements")

        avg_fps = runner.fps_values[-1]  # Use last average FPS value
        print(f"Average FPS: {avg_fps}, Expected: {expected_label}")
        self.assertGreater(
            avg_fps,
            expected_min,
            f"Average FPS {avg_fps} should be at least {expected_min}",
        )
        self.assertLess(
            avg_fps,
            expected_max,
            f"Average FPS {avg_fps} should not exceed {expected_max}",
        )

    def test_with_target_fps_10(self):
        """Test that gvafpsthrottle throttles to 10 fps"""
        pipeline = (
            "videotestsrc num-buffers=50 ! gvafpsthrottle target-fps=10 ! "
            "gvafpscounter name=fpscounter interval=1 ! fakesink"
        )
        self._run_and_verify_fps(pipeline, 9.0, 11.0, "10")

    def test_fractional_fps(self):
        """Test that gvafpsthrottle works with fractional fps values"""
        pipeline = (
            "videotestsrc num-buffers=100 ! gvafpsthrottle target-fps=29.97 ! "
            "gvafpscounter name=fpscounter interval=1 ! fakesink"
        )
        self._run_and_verify_fps(pipeline, 27.0, 33.0, "~30")

    def test_with_image_input(self):
        """Test that gvafpsthrottle works with image input"""
        image_path = os.path.join(SCRIPT_DIR, "test_files", "dog_bike_car.jpg")
        if os.path.exists(image_path):
            pipeline = (
                f"multifilesrc location={image_path} loop=true num-buffers=50 ! "
                "jpegdec ! gvafpsthrottle target-fps=10 ! "
                "gvafpscounter name=fpscounter interval=1 ! fakesink"
            )
            self._run_and_verify_fps(pipeline, 9.0, 11.0, "~10")


if __name__ == "__main__":
    unittest.main()
