# ==============================================================================
# Copyright (C) 2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================

import json
import os
import unittest

import gi
gi.require_version("Gst", "1.0")
from gi.repository import Gst  # pylint: disable=no-name-in-module, wrong-import-position

from pipeline_runner import TestGenericPipelineRunner  # pylint: disable=wrong-import-position

SCRIPT_DIR = os.path.dirname(os.path.realpath(__file__))
DLSTREAMER_ROOT = os.path.abspath(os.path.join(SCRIPT_DIR, "..", "..", ".."))
POINTPILLARS_ROOT = os.path.join(DLSTREAMER_ROOT, "..", "openvino_contrib", "modules", "3d", "pointPillars")


class TestG3DInference(unittest.TestCase):
    def setUp(self):
        self.test_output = os.path.join(SCRIPT_DIR, "test_files", "g3dinference_output.json")
        self.pc_file = os.path.join(POINTPILLARS_ROOT, "pointpillars", "dataset", "demo_data", "test", "000002.bin")
        self.config_file = os.path.join(POINTPILLARS_ROOT, "pretrained", "pointpillars_ov_config.json")

        if os.path.exists(self.test_output):
            os.remove(self.test_output)

    def tearDown(self):
        if os.path.exists(self.test_output):
            os.remove(self.test_output)

    def test_g3dinference_pointpillars_pipeline(self):
        element = Gst.ElementFactory.make("g3dinference", None)
        if element is None:
            self.skipTest("g3dinference element not available")

        if not os.path.exists(self.pc_file):
            self.skipTest(f"Point cloud file not found: {self.pc_file}")
        if not os.path.exists(self.config_file):
            self.skipTest(f"PointPillars config not found: {self.config_file}")

        pipeline = (
            f'filesrc location="{self.pc_file}" ! '
            f'application/octet-stream ! '
            f'g3dlidarparse ! '
            f'g3dinference config="{self.config_file}" device=CPU ! '
            f'gvametaconvert add-tensor-data=true format=json json-indent=2 ! '
            f'gvametapublish file-format=2 file-path="{self.test_output}" ! '
            f'fakesink'
        )

        runner = TestGenericPipelineRunner()
        runner.set_pipeline(pipeline)
        runner.run_pipeline()

        self.assertEqual(len(runner.exceptions), 0, "Pipeline should run without errors")
        self.assertTrue(os.path.exists(self.test_output), "Output JSON file was not created")

        with open(self.test_output, "r", encoding="utf-8") as handle:
            content = handle.read().strip()
            self.assertTrue(content, "Output JSON file is empty")
            payload = json.loads(content)

        serialized = json.dumps(payload)
        self.assertIn("pointpillars_3d_detection", serialized)
        self.assertIn("pointpillars_3d", serialized)
        self.assertIn("data", serialized)


if __name__ == "__main__":
    unittest.main()