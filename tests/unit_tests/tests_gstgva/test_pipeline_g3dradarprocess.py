# ==============================================================================
# Copyright (C) 2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================

import unittest
import os
import json

import gi
gi.require_version("Gst", "1.0")
from gi.repository import Gst

# pylint: disable=wrong-import-position
from pipeline_runner import TestGenericPipelineRunner
# pylint: enable=wrong-import-position

SCRIPT_DIR = os.path.dirname(os.path.realpath(__file__))


class TestG3DRadarProcess(unittest.TestCase):
    """Test suite for g3dradarprocess element

    IMPORTANT: This test requires oneAPI environment to be sourced before running.
    Run the following command before running this test:
        source /opt/intel/oneapi/setvars.sh
    """

    @classmethod
    def setUpClass(cls):
        """Verify oneAPI environment is properly configured"""
        # Check if libradar is installed
        libradar_path = "/usr/lib/libradar.so"
        if not os.path.exists(libradar_path):
            print("=" * 70)
            print("ERROR: libradar.so not found!")
            print(f"Expected location: {libradar_path}")
            print("Please run:")
            print("    ./scripts/install_radar_dependencies.sh")
            print("=" * 70)
            raise unittest.SkipTest(f"libradar.so not found at {libradar_path}")

        # Check if required MKL library path is in LD_LIBRARY_PATH
        ld_library_path = os.environ.get('LD_LIBRARY_PATH', '')
        if 'mkl' not in ld_library_path.lower() and 'oneapi' not in ld_library_path.lower():
            print("=" * 70)
            print("ERROR: oneAPI environment is not configured!")
            print("Please run before executing this test:")
            print("    source /opt/intel/oneapi/setvars.sh")
            print("=" * 70)
            raise unittest.SkipTest(
                "oneAPI environment not configured. "
                "Run: source /opt/intel/oneapi/setvars.sh"
            )

    def setUp(self):
        """Set up test fixtures"""
        self.test_files_dir = os.path.join(SCRIPT_DIR, "test_files")
        # multifilesrc needs a pattern with %06d, not a specific filename
        self.radar_data_pattern = os.path.join(self.test_files_dir, "%06d.bin")
        self.radar_data_file = os.path.join(self.test_files_dir, "000559.bin")
        self.radar_config_file = os.path.join(
            self.test_files_dir, "RadarConfig_raddet.json"
        )
        self.output_json = os.path.join(self.test_files_dir, "radar_test_output.json")

        # Verify test files exist
        self.assertTrue(
            os.path.exists(self.radar_data_file),
            f"Radar data file not found: {self.radar_data_file}",
        )
        self.assertTrue(
            os.path.exists(self.radar_config_file),
            f"Radar config file not found: {self.radar_config_file}",
        )

        # Clean up output file if it exists
        if os.path.exists(self.output_json):
            os.remove(self.output_json)

    def tearDown(self):
        """Clean up test artifacts"""
        if os.path.exists(self.output_json):
            os.remove(self.output_json)

    def test_g3dradarprocess_basic(self):
        """Test basic g3dradarprocess pipeline with single frame"""
        # Check if g3dradarprocess element is available
        element = Gst.ElementFactory.make("g3dradarprocess", None)
        if element is None:
            self.skipTest("g3dradarprocess element not available")

        pipeline = (
            f'multifilesrc location="{self.radar_data_pattern}" '
            f'start-index=559 stop-index=559 ! '
            f'application/octet-stream ! '
            f'g3dradarprocess radar-config="{self.radar_config_file}" ! '
            f'fakesink'
        )

        runner = TestGenericPipelineRunner()
        runner.set_pipeline(pipeline)
        runner.run_pipeline()

        if len(runner.exceptions) > 0:
            print(f"Pipeline errors: {runner.exceptions}")

        self.assertEqual(
            len(runner.exceptions), 0, "Pipeline should run without errors"
        )

    def test_g3dradarprocess_with_metadata_conversion(self):
        """Test g3dradarprocess with gvametaconvert and gvametapublish"""
        # Check if g3dradarprocess element is available
        element = Gst.ElementFactory.make("g3dradarprocess", None)
        if element is None:
            self.skipTest("g3dradarprocess element not available")

        pipeline = (
            f'multifilesrc location="{self.radar_data_pattern}" '
            f'start-index=559 stop-index=559 ! '
            f'application/octet-stream ! '
            f'g3dradarprocess radar-config="{self.radar_config_file}" ! '
            f'gvametaconvert format=json json-indent=2 ! '
            f'gvametapublish file-format=2 file-path="{self.output_json}" ! '
            f'fakesink'
        )

        runner = TestGenericPipelineRunner()
        runner.set_pipeline(pipeline)
        runner.run_pipeline()

        self.assertEqual(
            len(runner.exceptions), 0, "Pipeline should run without errors"
        )

        # Verify output JSON file was created
        self.assertTrue(
            os.path.exists(self.output_json),
            f"Output JSON file not created: {self.output_json}",
        )

        # Verify JSON file is valid and contains expected data
        with open(self.output_json, "r", encoding="utf-8") as f:
            content = f.read().strip()
            if not content:
                self.fail("Output JSON file is empty")

            try:
                data = json.loads(content)
                self.assertIsInstance(
                    data, dict, "JSON output should be a dictionary"
                )

                # Check for expected radar metadata structure
                # Verify it's not empty
                self.assertGreater(
                    len(data), 0, "JSON output should not be empty"
                )

                print(f"Radar processing output keys: {list(data.keys())}")

            except json.JSONDecodeError as e:
                self.fail(f"Output JSON file is not valid JSON: {e}")


if __name__ == "__main__":
    unittest.main()
