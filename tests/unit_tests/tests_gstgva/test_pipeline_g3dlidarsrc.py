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
REPO_ROOT = os.path.abspath(os.path.join(SCRIPT_DIR, "../../.."))


def _to_gst_path(path):
    """Return a filesystem path safe to embed in a GStreamer pipeline string.

    GStreamer's pipeline parser treats backslash as an escape character, so a
    Windows path such as ``C:\\Program Files\\Intel`` loses its separators and
    becomes ``C:Program FilesIntel``. Windows also accepts forward slashes in
    file paths, so normalizing to forward slashes fixes parsing on Windows and
    is a no-op on Linux (whose paths contain no backslashes).
    """
    return path.replace("\\", "/")


class TestG3DLidarSrc(unittest.TestCase):
    """Optional live integration tests for g3dlidarsrc (NOT run in typical CI).

    These are hardware-in-the-loop integration tests, not self-contained unit
    tests: they exercise the full element against a real device and therefore
    require physical hardware. Because the requirements below are not met in a
    normal CI environment, the whole class skips itself automatically (see
    setUpClass) and provides no deterministic coverage there. To actually run
    them you need:
      1. a built vendor backend library,
      2. a valid LiDAR config file,
      3. a reachable physical LiDAR device streaming live UDP data.

    Optional overrides:
        export G3D_LIDARSRC_TEST_CONFIG=/path/to/lidar.json
        export G3D_LIDARSRC_TEST_TIMEOUT_USEC=10000000
    """

    @staticmethod
    def _find_backend_library(library_name):
        search_dirs = []
        if os.name == "nt":
            path_env = os.environ.get("PATH", "")
            if path_env:
                search_dirs.extend([path for path in path_env.split(os.pathsep) if path])

            search_dirs.extend([
                os.path.join(REPO_ROOT, "build"),
                os.path.join(REPO_ROOT, "build", "intel64"),
                os.path.join(REPO_ROOT, "build", "intel64", "Release", "bin"),
                os.path.join(REPO_ROOT, "build", "intel64", "Debug", "bin"),
            ])
        else:
            ld_library_path = os.environ.get("LD_LIBRARY_PATH", "")
            if ld_library_path:
                search_dirs.extend([path for path in ld_library_path.split(":") if path])

            search_dirs.extend([
                "/usr/lib",
                "/usr/local/lib",
                "/lib",
                "/lib64",
                "/usr/lib64",
                os.path.join(REPO_ROOT, "build"),
                os.path.join(REPO_ROOT, "build", "intel64"),
                os.path.join(REPO_ROOT, "build", "intel64", "Release", "lib"),
                os.path.join(REPO_ROOT, "build", "intel64", "Debug", "lib"),
            ])

        for directory in search_dirs:
            candidate = os.path.join(directory, library_name)
            if os.path.exists(candidate):
                return candidate

        return None

    @classmethod
    def setUpClass(cls):
        cls.default_config = os.path.join(
            REPO_ROOT,
            "src/monolithic/gst/3d_elements/g3dlidarsrc/configs/robosense_e1r_udp.json",
        )
        cls.lidar_config_file = os.environ.get(
            "G3D_LIDARSRC_TEST_CONFIG", cls.default_config
        )
        cls.timeout_usec = int(
            os.environ.get("G3D_LIDARSRC_TEST_TIMEOUT_USEC", "10000000")
        )

        if not os.path.exists(cls.lidar_config_file):
            raise unittest.SkipTest(
                f"g3dlidarsrc config file not found: {cls.lidar_config_file}"
            )

        with open(cls.lidar_config_file, "r", encoding="utf-8") as config_file:
            config = json.load(config_file)

        vendor = config.get("vendor")
        if not vendor:
            raise unittest.SkipTest(
                f"g3dlidarsrc config does not contain a vendor field: {cls.lidar_config_file}"
            )

        if os.name == "nt":
            backend_library_name = f"g3dlidar_{vendor}.dll"
            search_hint = "Checked PATH and common Windows build output directories."
        else:
            backend_library_name = f"libg3dlidar_{vendor}.so"
            search_hint = "Checked LD_LIBRARY_PATH and common Linux library directories."
        backend_library_path = cls._find_backend_library(backend_library_name)
        if backend_library_path is None:
            print("=" * 70)
            print("ERROR: g3dlidarsrc backend library not found!")
            print(f"Expected library: {backend_library_name}")
            print(search_hint)
            print("=" * 70)
            raise unittest.SkipTest(
                f"g3dlidarsrc backend library not found: {backend_library_name}"
            )

        cls.backend_library_path = backend_library_path

    def setUp(self):
        self.output_json = os.path.join(
            SCRIPT_DIR, "test_files", "g3dlidarsrc_test_output.json"
        )
        if os.path.exists(self.output_json):
            os.remove(self.output_json)

    def tearDown(self):
        if os.path.exists(self.output_json):
            os.remove(self.output_json)

    def test_g3dlidarsrc_basic(self):
        """Test basic g3dlidarsrc pipeline with a live device."""
        element = Gst.ElementFactory.make("g3dlidarsrc", None)
        if element is None:
            self.skipTest("g3dlidarsrc element not available")

        pipeline = (
            f'g3dlidarsrc config="{_to_gst_path(self.lidar_config_file)}" '
            f'timeout={self.timeout_usec} num-buffers=1 ! '
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

    def test_g3dlidarsrc_with_metadata_conversion(self):
        """Test g3dlidarsrc with gvametaconvert and gvametapublish."""
        element = Gst.ElementFactory.make("g3dlidarsrc", None)
        if element is None:
            self.skipTest("g3dlidarsrc element not available")

        pipeline = (
            f'g3dlidarsrc config="{_to_gst_path(self.lidar_config_file)}" '
            f'timeout={self.timeout_usec} num-buffers=1 ! '
            f'gvametaconvert format=json add-empty-results=true ! '
            f'gvametapublish method=file file-format=json-lines file-path="{_to_gst_path(self.output_json)}" ! '
            f'fakesink'
        )

        runner = TestGenericPipelineRunner()
        runner.set_pipeline(pipeline)
        runner.run_pipeline()

        self.assertEqual(
            len(runner.exceptions), 0, "Pipeline should run without errors"
        )

        self.assertTrue(
            os.path.exists(self.output_json),
            f"Output JSON file not created: {self.output_json}",
        )

        with open(self.output_json, "r", encoding="utf-8") as output_file:
            content = output_file.read().strip()
            if not content:
                self.fail("Output JSON file is empty")

            try:
                data = json.loads(content)
                self.assertIsInstance(
                    data, dict, "JSON output should be a dictionary"
                )
                self.assertGreater(
                    len(data), 0, "JSON output should not be empty"
                )

                print(f"LiDAR source output keys: {list(data.keys())}")

            except json.JSONDecodeError as exc:
                self.fail(f"Output JSON file is not valid JSON: {exc}")


if __name__ == "__main__":
    unittest.main()