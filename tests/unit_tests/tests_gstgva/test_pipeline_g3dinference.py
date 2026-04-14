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
EXPECTED_DETECTIONS = [
    {"label_id": 2, "confidence": 0.954, "bbox_3d": {"x": 10.403315544128418, "y": -4.837177753448486, "z": -1.532669186592102, "w": 1.692104697227478, "l": 4.5549397468566895, "h": 1.5184109210968018, "theta": -0.02542771026492119}},
    {"label_id": 2, "confidence": 0.936, "bbox_3d": {"x": 18.695310592651367, "y": 5.6205010414123535, "z": -2.036820650100708, "w": 1.5274709463119507, "l": 3.4749011993408203, "h": 1.4211682081222534, "theta": 1.534871220588684}},
    {"label_id": 2, "confidence": 0.918, "bbox_3d": {"x": 28.957921981811523, "y": 5.406683444976807, "z": -2.12335205078125, "w": 1.531412124633789, "l": 3.507960796356201, "h": 1.56046462059021, "theta": 1.5513761043548584}},
    {"label_id": 2, "confidence": 0.914, "bbox_3d": {"x": 47.77565002441406, "y": -4.002121925354004, "z": -1.5385185480117798, "w": 1.5788333415985107, "l": 3.7427563667297363, "h": 1.4949634075164795, "theta": 0.5784904360771179}},
    {"label_id": 2, "confidence": 0.900, "bbox_3d": {"x": 23.92894744873047, "y": 5.494050979614258, "z": -2.152738571166992, "w": 1.5495686531066895, "l": 3.7228798866271973, "h": 1.4438520669937134, "theta": 1.5458637475967407}},
    {"label_id": 2, "confidence": 0.820, "bbox_3d": {"x": 5.6566572189331055, "y": -4.869320392608643, "z": -1.508419394493103, "w": 1.5985325574874878, "l": 4.069395065307617, "h": 1.4478180408477783, "theta": -0.1482767015695572}},
    {"label_id": 2, "confidence": 0.552, "bbox_3d": {"x": 56.1164436340332, "y": 0.08022300899028778, "z": -1.8877365589141846, "w": 1.688123345375061, "l": 3.9101309776306152, "h": 1.4751232862472534, "theta": 1.5299361944198608}},
    {"label_id": 2, "confidence": 0.549, "bbox_3d": {"x": 34.539920806884766, "y": 5.468486309051514, "z": -2.2403616905212402, "w": 1.6812094449996948, "l": 4.142858982086182, "h": 1.4964956045150757, "theta": 1.6419190168380737}},
    {"label_id": 2, "confidence": 0.530, "bbox_3d": {"x": 14.937199592590332, "y": 6.906136989593506, "z": -2.152796745300293, "w": 1.5562783479690552, "l": 3.8965888023376465, "h": 1.4418131113052368, "theta": -3.06831693649292}},
    {"label_id": 2, "confidence": 0.450, "bbox_3d": {"x": 66.13455963134766, "y": 4.945899486541748, "z": -2.087339401245117, "w": 1.5780829191207886, "l": 4.042502403259277, "h": 1.5297237634658813, "theta": 1.4576448202133179}},
]


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
    BOX_DELTA = 1e-2
    CONFIDENCE_DELTA = 2e-2

    @classmethod
    def setUpClass(cls):
        cls.pointpillars_root = ensure_pointpillars_root()
        cls.extension_lib = ensure_pointpillars_extension(cls.pointpillars_root)

    def setUp(self):
        self.test_dir = tempfile.mkdtemp(prefix="g3dinference_test_")
        self.test_output = os.path.join(self.test_dir, "g3dinference_output.json")
        self.pc_file = os.path.join(self.pointpillars_root, "pointpillars", "dataset", "demo_data", "test", "000002.bin")
        self.pc_index = int(os.path.splitext(os.path.basename(self.pc_file))[0])
        self.pc_pattern = os.path.join(os.path.dirname(self.pc_file), "%06d.bin")
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

    def _extract_detected_objects(self, payload):
        objects = payload.get("objects")
        self.assertIsInstance(objects, list, "JSON output must contain an 'objects' array")

        parsed_objects = []
        for obj in objects:
            bbox_3d = obj.get("bbox_3d", {})
            parsed_objects.append(
                {
                    "label_id": obj.get("label_id"),
                    "confidence": obj.get("confidence"),
                    "bbox_3d": {
                        "x": bbox_3d.get("x"),
                        "y": bbox_3d.get("y"),
                        "z": bbox_3d.get("z"),
                        "w": bbox_3d.get("w"),
                        "l": bbox_3d.get("l"),
                        "h": bbox_3d.get("h"),
                        "theta": bbox_3d.get("theta"),
                    },
                }
            )

        return sorted(parsed_objects, key=lambda item: item["confidence"], reverse=True)

    def _print_detection_debug(self, actual, expected, index):
        print(f"[g3dinference][debug] object {index}")
        print(
            "  label_id: "
            f"actual={actual['label_id']} expected={expected['label_id']}"
        )
        print(
            "  confidence: "
            f"actual={actual['confidence']:.6f} expected={expected['confidence']:.6f} "
            f"delta={actual['confidence'] - expected['confidence']:+.6f}"
        )

        for field_name in ("x", "y", "z", "w", "l", "h", "theta"):
            actual_value = actual["bbox_3d"][field_name]
            expected_value = expected["bbox_3d"][field_name]
            print(
                f"  bbox_3d.{field_name}: "
                f"actual={actual_value:.6f} expected={expected_value:.6f} "
                f"delta={actual_value - expected_value:+.6f}"
            )

    def _print_detection_count_debug(self, actual_detections):
        print(
            "[g3dinference][debug] detection count mismatch: "
            f"actual={len(actual_detections)} expected={len(EXPECTED_DETECTIONS)}"
        )
        print("[g3dinference][debug] actual detections:")
        print(json.dumps(actual_detections, indent=2, ensure_ascii=True))
        print("[g3dinference][debug] expected detections:")
        print(json.dumps(EXPECTED_DETECTIONS, indent=2, ensure_ascii=True))

    def _print_all_detections_debug(self, actual_detections):
        print(
            "[g3dinference][debug] full detection dump: "
            f"actual={len(actual_detections)} expected={len(EXPECTED_DETECTIONS)}"
        )

        pair_count = min(len(actual_detections), len(EXPECTED_DETECTIONS))
        for index in range(pair_count):
            self._print_detection_debug(actual_detections[index], EXPECTED_DETECTIONS[index], index)

        if len(actual_detections) > pair_count:
            print("[g3dinference][debug] extra actual detections:")
            print(json.dumps(actual_detections[pair_count:], indent=2, ensure_ascii=True))

        if len(EXPECTED_DETECTIONS) > pair_count:
            print("[g3dinference][debug] missing expected detections:")
            print(json.dumps(EXPECTED_DETECTIONS[pair_count:], indent=2, ensure_ascii=True))

    def _assert_detection_matches(self, actual, expected, index):
        label_mismatch = actual["label_id"] != expected["label_id"]
        confidence_delta = abs(actual["confidence"] - expected["confidence"])
        bbox_mismatch = any(
            abs(actual["bbox_3d"][field_name] - expected["bbox_3d"][field_name]) > self.BOX_DELTA
            for field_name in ("x", "y", "z", "w", "l", "h", "theta")
        )

        if label_mismatch or confidence_delta > self.CONFIDENCE_DELTA or bbox_mismatch:
            self._print_detection_debug(actual, expected, index)

        self.assertEqual(actual["label_id"], expected["label_id"], f"label_id mismatch for object {index}")
        self.assertAlmostEqual(
            actual["confidence"], expected["confidence"], delta=self.CONFIDENCE_DELTA,
            msg=f"confidence mismatch for object {index}"
        )

        for field_name in ("x", "y", "z", "w", "l", "h", "theta"):
            self.assertAlmostEqual(
                actual["bbox_3d"][field_name], expected["bbox_3d"][field_name], delta=self.BOX_DELTA,
                msg=f"bbox_3d.{field_name} mismatch for object {index}"
            )

    def _assert_detections_match(self, payload):
        actual_detections = self._extract_detected_objects(payload)
        self._print_all_detections_debug(actual_detections)
        if len(actual_detections) != len(EXPECTED_DETECTIONS):
            self._print_detection_count_debug(actual_detections)
        self.assertEqual(
            len(actual_detections), len(EXPECTED_DETECTIONS),
            f"Expected {len(EXPECTED_DETECTIONS)} objects, got {len(actual_detections)}"
        )

        for index, (actual, expected) in enumerate(zip(actual_detections, EXPECTED_DETECTIONS)):
            self._assert_detection_matches(actual, expected, index)

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
            f'multifilesrc location="{self.pc_pattern}" start-index={self.pc_index} stop-index={self.pc_index} '
            f'caps=application/octet-stream ! '
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

        self._assert_detections_match(payload)

        serialized = json.dumps(payload)
        self.assertIn("pointpillars_3d_detection", serialized)
        self.assertIn("pointpillars_3d", serialized)
        self.assertIn("FP16", serialized)
        self.assertIn("data", serialized)


if __name__ == "__main__":
    unittest.main()