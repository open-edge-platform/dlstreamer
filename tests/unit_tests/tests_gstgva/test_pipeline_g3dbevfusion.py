# ==============================================================================
# Copyright (C) 2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================
"""g3dbevfusion pipeline tests on a nuScenes scene.

Needs the assets from g3dbevfusion_prepare_model.sh and g3dbevfusion_prepare_data.sh
in samples/gstreamer/gst_launch/g3dbevfusion/, and a GPU. Tests are skipped when
they are missing.

  G3DBEVFUSION_CONFIG  runtime config (default: the last <sample>/bevfusion_ov_model_*/g3dbevfusion.json)
  G3DBEVFUSION_SCENE   converted scene (default: <sample>/nuscenes_mini_seq_12Hz/scene-0103)

Detections are read back through the element's G3DBEVFUSION_DUMP_DIR debug
output: one boxes_<frame>.npy per inferred frame, float32 [N, 11].
"""

import glob
import json
import os
import shutil
import struct
import tempfile
import unittest

import gi

gi.require_version("Gst", "1.0")
from gi.repository import Gst  # pylint: disable=no-name-in-module, wrong-import-position

Gst.init(None)

SCRIPT_DIR = os.path.dirname(os.path.realpath(__file__))
DLSTREAMER_ROOT = os.path.abspath(os.path.join(SCRIPT_DIR, "..", "..", ".."))
SAMPLE_DIR = os.path.join(DLSTREAMER_ROOT, "samples", "gstreamer", "gst_launch", "g3dbevfusion")
CONFIG = os.environ.get("G3DBEVFUSION_CONFIG") or next(
    reversed(sorted(glob.glob(os.path.join(SAMPLE_DIR, "bevfusion_ov_model_*", "g3dbevfusion.json")))),
    os.path.join(SAMPLE_DIR, "bevfusion_ov_model_<openvino-version>", "g3dbevfusion.json"))
SCENE = os.environ.get("G3DBEVFUSION_SCENE", os.path.join(SAMPLE_DIR, "nuscenes_mini_seq_12Hz", "scene-0103"))
CAMERAS = ["CAM_FRONT", "CAM_FRONT_RIGHT", "CAM_FRONT_LEFT", "CAM_BACK", "CAM_BACK_LEFT", "CAM_BACK_RIGHT"]
SCORE_THRESHOLD = 0.3


def load_boxes(path):
    """Read a boxes_<frame>.npy written by the element (float32 [N, 11], C order)."""
    with open(path, "rb") as f:
        data = f.read()
    header_len = struct.unpack("<H", data[8:10])[0]
    payload = data[10 + header_len:]
    count = len(payload) // 4
    values = struct.unpack(f"<{count}f", payload)
    return [values[i:i + 11] for i in range(0, count, 11)]


class TestG3DBevFusion(unittest.TestCase):

    @classmethod
    def setUpClass(cls):
        if Gst.ElementFactory.find("g3dbevfusion") is None:
            raise unittest.SkipTest("g3dbevfusion element not available")
        if not os.path.isfile(CONFIG):
            raise unittest.SkipTest(f"BEVFusion runtime config not found: {CONFIG}")
        if not os.path.isfile(os.path.join(SCENE, "calibration.json")):
            raise unittest.SkipTest(f"Converted nuScenes scene not found: {SCENE}")

    def setUp(self):
        self.dump_dir = tempfile.mkdtemp(prefix="g3dbevfusion_")
        os.environ["G3DBEVFUSION_DUMP_DIR"] = self.dump_dir

    def tearDown(self):
        os.environ.pop("G3DBEVFUSION_DUMP_DIR", None)
        shutil.rmtree(self.dump_dir, ignore_errors=True)

    @staticmethod
    def _camera_branches(frames):
        branches = []
        for pad, name in enumerate(CAMERAS):
            branches.append(
                f"multifilesrc location={SCENE}/{name}/%06d.jpg stop-index={frames - 1} "
                "caps=image/jpeg,framerate=2/1 ! jpegparse ! jpegdec ! videoconvert ! "
                f"video/x-raw,format=BGR ! queue ! mux.sink_{pad}")
        return " ".join(branches)

    @staticmethod
    def _lidar_branch(frames):
        return (f"multifilesrc location={SCENE}/LIDAR_TOP/%06d.bin stop-index={frames - 1} "
                "caps=application/octet-stream ! g3dlidarparse frame-rate=2 ! queue ! mux.sink_6")

    @staticmethod
    def _bevfusion(calibration, config=CONFIG):
        props = f"calibration={calibration} score-threshold={SCORE_THRESHOLD}"
        if config:
            props += f" config={config}"
        return ("gvastreammux name=mux output-mode=container sync-mode=first-pts max-wait-time=5000000000 ! "
                f"g3dbevfusion {props} ! fakesink name=sink")

    def _calibration(self, edit):
        """A copy of the scene's calibration.json, changed by edit(dict)."""
        with open(os.path.join(SCENE, "calibration.json")) as f:
            calibration = json.load(f)
        edit(calibration)
        path = os.path.join(self.dump_dir, "calibration_edited.json")
        with open(path, "w") as f:
            json.dump(calibration, f)
        return path

    @staticmethod
    def _frames_table():
        with open(os.path.join(SCENE, "calibration.json")) as f:
            return json.load(f).get("frames", [])

    @staticmethod
    def _run(pipeline, timeout_s=180, pts=None):
        """Runs to EOS; with pts=[], collects the PTS of every buffer reaching the sink."""
        gst_pipeline = Gst.parse_launch(pipeline)
        if pts is not None:
            gst_pipeline.get_by_name("sink").get_static_pad("sink").add_probe(
                Gst.PadProbeType.BUFFER, lambda pad, info: pts.append(info.get_buffer().pts) or Gst.PadProbeReturn.OK)
        bus = gst_pipeline.get_bus()
        errors = []
        if gst_pipeline.set_state(Gst.State.PLAYING) == Gst.StateChangeReturn.FAILURE:
            errors.append("Pipeline failed to start")
        while True:
            msg = bus.timed_pop_filtered(timeout_s * Gst.SECOND, Gst.MessageType.ERROR | Gst.MessageType.EOS)
            if msg is None:
                errors.append("Timed out waiting for EOS")
                break
            if msg.type == Gst.MessageType.ERROR:
                err, debug = msg.parse_error()
                errors.append(f"{err.message} | {debug}")
                break
            if msg.type == Gst.MessageType.EOS:
                break
        gst_pipeline.set_state(Gst.State.NULL)
        return errors

    def _dumped_frames(self):
        return sorted(f for f in os.listdir(self.dump_dir) if f.startswith("boxes_"))

    def test_lidar_only(self):
        frames = 3
        errors = self._run(f"{self._lidar_branch(frames)} "
                           f"{self._bevfusion(os.path.join(SCENE, 'calibration_lidar_only.json'))}")
        self.assertEqual(errors, [])
        dumped = self._dumped_frames()
        self.assertEqual(len(dumped), frames)
        for name in dumped:
            boxes = load_boxes(os.path.join(self.dump_dir, name))
            self.assertGreater(len(boxes), 0, f"{name}: no detections")
            self.assertTrue(all(b[9] >= SCORE_THRESHOLD for b in boxes), f"{name}: box below score-threshold")

    def test_fusion(self):
        frames = 2
        errors = self._run(f"{self._camera_branches(frames)} {self._lidar_branch(frames)} "
                           f"{self._bevfusion(os.path.join(SCENE, 'calibration.json'))}")
        self.assertEqual(errors, [])
        dumped = self._dumped_frames()
        self.assertEqual(len(dumped), frames)
        fusion = [load_boxes(os.path.join(self.dump_dir, name)) for name in dumped]
        for name, boxes in zip(dumped, fusion):
            self.assertGreater(len(boxes), 0, f"{name}: no detections")

        # The camera branch has to change the result: rerun the same frames lidar-only.
        shutil.rmtree(self.dump_dir)
        os.makedirs(self.dump_dir)
        errors = self._run(f"{self._lidar_branch(frames)} "
                           f"{self._bevfusion(os.path.join(SCENE, 'calibration_lidar_only.json'))}")
        self.assertEqual(errors, [])
        lidar = [load_boxes(os.path.join(self.dump_dir, name)) for name in self._dumped_frames()]
        self.assertNotEqual(fusion, lidar, "fusion produced exactly the lidar-only detections")

    def test_fusion_repeats_a_frame(self):
        # The same frame twice in one pipeline must give the same detections: the
        # camera branch's BEV pool must not carry anything over from the frame before.
        repeat = "start-index=0 stop-index=0 loop=true num-buffers=2"
        # The second read of file 0 is lidar frame 1, which the frames table
        # describes differently; leave the table out.
        calibration = self._calibration(lambda c: c.pop("frames", None))
        cameras = " ".join(
            f"multifilesrc location={SCENE}/{name}/%06d.jpg {repeat} "
            "caps=image/jpeg,framerate=2/1 ! jpegparse ! jpegdec ! videoconvert ! "
            f"video/x-raw,format=BGR ! queue ! mux.sink_{pad}" for pad, name in enumerate(CAMERAS))
        lidar = (f"multifilesrc location={SCENE}/LIDAR_TOP/%06d.bin {repeat} "
                 "caps=application/octet-stream ! g3dlidarparse frame-rate=2 ! queue ! mux.sink_6")
        errors = self._run(f"{cameras} {lidar} {self._bevfusion(calibration)}")
        self.assertEqual(errors, [])
        dumped = self._dumped_frames()
        self.assertEqual(len(dumped), 2)
        first, second = (load_boxes(os.path.join(self.dump_dir, name)) for name in dumped)
        self.assertGreater(len(first), 0, "no detections")
        self.assertEqual(len(first), len(second), "the repeated frame gave a different number of boxes")
        for a, b in zip(sorted(first), sorted(second)):
            self.assertTrue(all(abs(x - y) < 1e-2 for x, y in zip(a, b)), f"repeated frame differs: {a} vs {b}")

    def test_frames_table_sets_recorded_timestamps(self):
        table = self._frames_table()
        if not table:
            self.skipTest("the scene's calibration.json has no frames table")
        frames, pts = 3, []
        errors = self._run(f"{self._camera_branches(frames)} {self._lidar_branch(frames)} "
                           f"{self._bevfusion(os.path.join(SCENE, 'calibration.json'))}", pts=pts)
        self.assertEqual(errors, [])
        self.assertEqual(pts, [table[i]["timestamp_us"] * 1000 for i in range(frames)])

    def test_frames_table_out_of_step_is_an_error(self):
        if not self._frames_table():
            self.skipTest("the scene's calibration.json has no frames table")

        def edit(calibration):
            calibration["frames"][0]["lidar"]["points"] += 1
            calibration["frames"][0]["lidar"].pop("sweeps", None)

        errors = self._run(f"{self._lidar_branch(1)} {self._bevfusion(self._calibration(edit))}")
        self.assertNotEqual(errors, [])
        self.assertIn("does not match the calibration's frames table", "\n".join(errors))

    def test_too_few_cameras_is_an_error(self):
        with open(os.path.join(SCENE, "calibration.json")) as f:
            calibration = json.load(f)
        calibration["cameras"] = {pad: cam for pad, cam in calibration["cameras"].items() if int(pad) < 3}
        path = os.path.join(self.dump_dir, "three_cameras.json")
        with open(path, "w") as f:
            json.dump(calibration, f)
        errors = self._run(f"{self._lidar_branch(1)} {self._bevfusion(path)}")
        self.assertNotEqual(errors, [])
        self.assertIn("slot 3 is missing", "\n".join(errors))

    def test_missing_config_is_an_error(self):
        errors = self._run(f"{self._lidar_branch(1)} "
                           f"{self._bevfusion(os.path.join(SCENE, 'calibration.json'), config=None)}")
        self.assertNotEqual(errors, [])
        self.assertIn("config and calibration properties are required", "\n".join(errors))

    def test_batch_without_lidar_passes_through(self):
        errors = self._run(f"{self._camera_branches(1)} "
                           f"{self._bevfusion(os.path.join(SCENE, 'calibration.json'))}")
        self.assertEqual(errors, [])
        self.assertEqual(self._dumped_frames(), [], "no inference should run without the lidar")


if __name__ == "__main__":
    unittest.main()
