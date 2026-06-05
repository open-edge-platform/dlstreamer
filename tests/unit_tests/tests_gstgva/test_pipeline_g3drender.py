# ==============================================================================
# Copyright (C) 2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================

import os
import struct
import unittest

import gi
gi.require_version("Gst", "1.0")
from gi.repository import Gst

Gst.init(None)

SCRIPT_DIR = os.path.dirname(os.path.realpath(__file__))
TEST_FILES_DIR = os.path.join(SCRIPT_DIR, "test_files")
BIN_LOCATION = os.path.join(TEST_FILES_DIR, "000559.bin")
PCD_LOCATION = os.path.join(TEST_FILES_DIR, "000001.pcd")


def _run_pipeline(pipeline_str, timeout_sec=10):
    pipeline = Gst.parse_launch(pipeline_str)
    bus = pipeline.get_bus()
    pipeline.set_state(Gst.State.PLAYING)

    exceptions = []
    timeout = timeout_sec * Gst.SECOND

    while True:
        msg = bus.timed_pop_filtered(timeout, Gst.MessageType.ERROR | Gst.MessageType.EOS)
        if msg is None:
            break
        if msg.type == Gst.MessageType.ERROR:
            exceptions.append(msg.parse_error())
        elif msg.type == Gst.MessageType.EOS:
            break

    pipeline.set_state(Gst.State.NULL)
    return exceptions


class TestG3DRenderBEV(unittest.TestCase):
    def _check_element_available(self):
        if Gst.ElementFactory.find("g3drender") is None:
            self.skipTest("g3drender element not available")
        if Gst.ElementFactory.find("g3dlidarparse") is None:
            self.skipTest("g3dlidarparse element not available")

    def test_bev_bin_pipeline(self):
        self._check_element_available()
        if not os.path.exists(BIN_LOCATION):
            self.skipTest(f"Test file not found: {BIN_LOCATION}")

        pipeline = (
            f'filesrc location="{BIN_LOCATION}" '
            f'! capsfilter caps=application/octet-stream '
            f'! g3dlidarparse '
            f'! g3drender '
            f'! fakesink'
        )
        exceptions = _run_pipeline(pipeline)
        self.assertEqual(len(exceptions), 0, f"Pipeline errors: {exceptions}")

    def test_bev_pcd_pipeline(self):
        self._check_element_available()
        if not os.path.exists(PCD_LOCATION):
            self.skipTest(f"Test file not found: {PCD_LOCATION}")

        pipeline = (
            f'filesrc location="{PCD_LOCATION}" '
            f'! capsfilter caps=application/octet-stream '
            f'! g3dlidarparse '
            f'! g3drender '
            f'! fakesink'
        )
        exceptions = _run_pipeline(pipeline)
        self.assertEqual(len(exceptions), 0, f"Pipeline errors: {exceptions}")

    def test_bev_custom_size(self):
        self._check_element_available()
        if not os.path.exists(BIN_LOCATION):
            self.skipTest(f"Test file not found: {BIN_LOCATION}")

        pipeline = (
            f'filesrc location="{BIN_LOCATION}" '
            f'! capsfilter caps=application/octet-stream '
            f'! g3dlidarparse '
            f'! g3drender width=640 height=480 '
            f'! fakesink'
        )
        exceptions = _run_pipeline(pipeline)
        self.assertEqual(len(exceptions), 0, f"Pipeline errors: {exceptions}")

    def test_bev_zoom(self):
        self._check_element_available()
        if not os.path.exists(BIN_LOCATION):
            self.skipTest(f"Test file not found: {BIN_LOCATION}")

        for zoom in (0.5, 1.0, 2.0):
            with self.subTest(zoom=zoom):
                pipeline = (
                    f'filesrc location="{BIN_LOCATION}" '
                    f'! capsfilter caps=application/octet-stream '
                    f'! g3dlidarparse '
                    f'! g3drender zoom={zoom} '
                    f'! fakesink'
                )
                exceptions = _run_pipeline(pipeline)
                self.assertEqual(len(exceptions), 0, f"Pipeline errors at zoom={zoom}: {exceptions}")

    def test_bev_point_stride(self):
        self._check_element_available()
        if not os.path.exists(BIN_LOCATION):
            self.skipTest(f"Test file not found: {BIN_LOCATION}")

        for stride in (1, 4, 8):
            with self.subTest(stride=stride):
                pipeline = (
                    f'filesrc location="{BIN_LOCATION}" '
                    f'! capsfilter caps=application/octet-stream '
                    f'! g3dlidarparse '
                    f'! g3drender point-stride={stride} '
                    f'! fakesink'
                )
                exceptions = _run_pipeline(pipeline)
                self.assertEqual(len(exceptions), 0, f"Pipeline errors at stride={stride}: {exceptions}")

    def test_bev_output_caps(self):
        self._check_element_available()
        if not os.path.exists(BIN_LOCATION):
            self.skipTest(f"Test file not found: {BIN_LOCATION}")

        received_caps = []

        pipeline = Gst.parse_launch(
            f'filesrc location="{BIN_LOCATION}" '
            f'! capsfilter caps=application/octet-stream '
            f'! g3dlidarparse '
            f'! g3drender width=800 height=800 '
            f'! fakesink name=sink'
        )

        sink = pipeline.get_by_name("sink")
        pad = sink.get_static_pad("sink")

        def on_caps(pad, info):
            event = info.get_event()
            if event.type == Gst.EventType.CAPS:
                _, caps = event.parse_caps()
                received_caps.append(caps.to_string())
            return Gst.PadProbeReturn.OK

        pad.add_probe(Gst.PadProbeType.EVENT_DOWNSTREAM, on_caps)

        bus = pipeline.get_bus()
        pipeline.set_state(Gst.State.PLAYING)

        msg = bus.timed_pop_filtered(10 * Gst.SECOND, Gst.MessageType.ERROR | Gst.MessageType.EOS)
        pipeline.set_state(Gst.State.NULL)

        self.assertTrue(len(received_caps) > 0, "No caps received on sink pad")
        caps_str = received_caps[0]
        self.assertIn("video/x-raw", caps_str)
        self.assertIn("BGR", caps_str)
        self.assertIn("width=(int)800", caps_str)
        self.assertIn("height=(int)800", caps_str)


class TestG3DRenderPerspective(unittest.TestCase):
    def _check_element_available(self):
        if Gst.ElementFactory.find("g3drender") is None:
            self.skipTest("g3drender element not available")
        if Gst.ElementFactory.find("g3dlidarparse") is None:
            self.skipTest("g3dlidarparse element not available")

    def test_perspective_pipeline(self):
        self._check_element_available()
        if not os.path.exists(BIN_LOCATION):
            self.skipTest(f"Test file not found: {BIN_LOCATION}")

        pipeline = (
            f'filesrc location="{BIN_LOCATION}" '
            f'! capsfilter caps=application/octet-stream '
            f'! g3dlidarparse '
            f'! g3drender view-mode=perspective point-stride=4 '
            f'  cam-distance=35 cam-elevation=30 cam-azimuth=225 '
            f'! fakesink'
        )
        exceptions = _run_pipeline(pipeline)
        self.assertEqual(len(exceptions), 0, f"Pipeline errors: {exceptions}")

    def test_perspective_rotation(self):
        self._check_element_available()
        if not os.path.exists(BIN_LOCATION):
            self.skipTest(f"Test file not found: {BIN_LOCATION}")

        pipeline = (
            f'filesrc location="{BIN_LOCATION}" '
            f'! capsfilter caps=application/octet-stream '
            f'! g3dlidarparse '
            f'! g3drender view-mode=perspective point-stride=4 cam-azimuth-step=1 '
            f'! fakesink'
        )
        exceptions = _run_pipeline(pipeline)
        self.assertEqual(len(exceptions), 0, f"Pipeline errors: {exceptions}")


if __name__ == "__main__":
    unittest.main()
