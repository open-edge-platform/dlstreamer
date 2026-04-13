# ==============================================================================
# Copyright (C) 2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================

import json
import os
import shutil
import subprocess
import sys
import tempfile
import unittest

import gi
gi.require_version("Gst", "1.0")
from gi.repository import Gst  # pylint: disable=no-name-in-module, wrong-import-position

from pipeline_runner import TestGenericPipelineRunner  # pylint: disable=wrong-import-position

SCRIPT_DIR = os.path.dirname(os.path.realpath(__file__))
DLSTREAMER_ROOT = os.path.abspath(os.path.join(SCRIPT_DIR, "..", "..", ".."))
OPENVINO_CONTRIB_URL = "https://github.com/openvinotoolkit/openvino_contrib.git"


def ensure_pointpillars_root():
    env_root = os.environ.get("POINTPILLARS_ROOT")
    if env_root:
        if not os.path.isdir(env_root):
            raise unittest.SkipTest(f"POINTPILLARS_ROOT does not exist: {env_root}")
        return env_root

    sibling_root = os.path.join(DLSTREAMER_ROOT, "..", "openvino_contrib", "modules", "3d", "pointPillars")
    if os.path.isdir(sibling_root):
        return sibling_root

    cache_root = os.environ.get(
        "POINTPILLARS_CACHE_DIR",
        os.path.join(tempfile.gettempdir(), "g3dinference_pointpillars_cache"),
    )
    sparse_root = os.path.join(cache_root, "openvino_contrib")
    checkout_root = os.path.join(sparse_root, "modules", "3d", "pointPillars")

    if os.path.isdir(checkout_root):
        return checkout_root

    git_bin = shutil.which("git")
    if git_bin is None:
        raise unittest.SkipTest("git is required to fetch openvino_contrib for PointPillars")

    os.makedirs(cache_root, exist_ok=True)
    shutil.rmtree(sparse_root, ignore_errors=True)

    try:
        subprocess.run(
            [git_bin, "clone", "--depth", "1", "--filter=blob:none", "--sparse", OPENVINO_CONTRIB_URL, sparse_root],
            check=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
        )
        subprocess.run(
            [git_bin, "-C", sparse_root, "sparse-checkout", "set", "modules/3d/pointPillars"],
            check=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
        )
    except subprocess.CalledProcessError as error:
        raise unittest.SkipTest(
            f"Failed to fetch openvino_contrib PointPillars sources: {error.stdout.strip()}"
        ) from error

    return checkout_root


def ensure_pointpillars_extension(pointpillars_root):
    ext_library = os.path.join(pointpillars_root, "ov_extensions", "build", "libov_pointpillars_extensions.so")
    if os.path.exists(ext_library):
        return ext_library

    build_script = os.path.join(pointpillars_root, "ov_extensions", "build.sh")
    if not os.path.exists(build_script):
        raise unittest.SkipTest(f"PointPillars build script not found: {build_script}")

    env = os.environ.copy()
    env["PATH"] = os.path.dirname(sys.executable) + os.pathsep + env.get("PATH", "")

    try:
        subprocess.run(
            ["bash", build_script],
            check=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            env=env,
        )
    except subprocess.CalledProcessError as error:
        raise unittest.SkipTest(
            f"Failed to build PointPillars extension: {error.stdout.strip()}"
        ) from error

    if not os.path.exists(ext_library):
        raise unittest.SkipTest(f"PointPillars extension library was not produced: {ext_library}")

    return ext_library


class TestG3DInference(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.pointpillars_root = ensure_pointpillars_root()
        cls.extension_lib = ensure_pointpillars_extension(cls.pointpillars_root)

    def setUp(self):
        self.test_dir = tempfile.mkdtemp(prefix="g3dinference_test_")
        self.test_output = os.path.join(self.test_dir, "g3dinference_output.json")
        self.pc_file = os.path.join(self.pointpillars_root, "pointpillars", "dataset", "demo_data", "test", "000002.bin")
        self.config_file = os.path.join(self.test_dir, "pointpillars_ov_config.json")
        self.extension_lib = self.__class__.extension_lib
        self.voxel_model = os.path.join(self.pointpillars_root, "pretrained", "pointpillars_ov_pillar_layer.xml")
        self.nn_model = os.path.join(self.pointpillars_root, "pretrained", "pointpillars_ov_nn.xml")
        self.postproc_model = os.path.join(self.pointpillars_root, "pretrained", "pointpillars_ov_postproc.xml")

    def tearDown(self):
        shutil.rmtree(self.test_dir, ignore_errors=True)

    def _write_runtime_config(self):
        config_payload = {
            "voxel_params": {
                "voxel_size": [0.16, 0.16, 4],
                "point_cloud_range": [0, -39.68, -3, 69.12, 39.68, 1],
                "max_num_points": 32,
                "max_voxels": 16000,
            },
            "extension_lib": self.extension_lib,
            "voxel_model": self.voxel_model,
            "nn_model": self.nn_model,
            "postproc_model": self.postproc_model,
        }

        with open(self.config_file, "w", encoding="utf-8") as handle:
            json.dump(config_payload, handle)

    def test_g3dinference_pointpillars_pipeline(self):
        element = Gst.ElementFactory.make("g3dinference", None)
        if element is None:
            self.skipTest("g3dinference element not available")

        if not os.path.exists(self.pc_file):
            self.skipTest(f"Point cloud file not found: {self.pc_file}")
        if not os.path.exists(self.extension_lib):
            self.skipTest(f"PointPillars extension library not found: {self.extension_lib}")
        if not os.path.exists(self.voxel_model):
            self.skipTest(f"PointPillars voxel model not found: {self.voxel_model}")
        if not os.path.exists(self.nn_model):
            self.skipTest(f"PointPillars NN model not found: {self.nn_model}")
        if not os.path.exists(self.postproc_model):
            self.skipTest(f"PointPillars postproc model not found: {self.postproc_model}")

        self._write_runtime_config()

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