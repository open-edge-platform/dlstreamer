"""Unit tests for the ONVIF sample's web dashboard HTTP Basic Auth
(samples/gstreamer/python/onvif_camera_analytics_validation/onvif_camera_analytics_validation.py).

Requires PyGObject + GStreamer typelibs to import the sample module (same
requirement as running the sample itself); skipped automatically if missing.

Run with: pytest test_dashboard_auth.py -v
"""
# ==============================================================================
# Copyright (C) 2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================

import base64
import os
import sys

import pytest

gst_mod = pytest.importorskip(
    "gi.repository.Gst",
    reason="Requires PyGObject + GStreamer typelibs (same as running the sample itself)",
)
pytest.importorskip("paho", reason="Requires paho-mqtt (sample's util.py imports it unconditionally)")

_SAMPLE_DIR = os.path.join(
    os.path.dirname(os.path.abspath(__file__)),
    "..", "..", "..", "samples", "gstreamer", "python", "onvif_camera_analytics_validation",
)
sys.path.insert(0, os.path.abspath(_SAMPLE_DIR))

from onvif_camera_analytics_validation import DashboardHandler  # noqa: E402  pylint: disable=wrong-import-position


class _FakeHandler:
    """Minimal stand-in for http.server.BaseHTTPRequestHandler's `self`."""

    def __init__(self, headers=None):
        self.headers = headers or {}
        self.responses = []

    def send_response(self, code):
        self.responses.append(("status", code))

    def send_header(self, key, value):
        self.responses.append(("header", key, value))

    def end_headers(self):
        self.responses.append(("end_headers",))


def _basic_auth_header(password, username="any"):
    token = base64.b64encode(f"{username}:{password}".encode()).decode()
    return f"Basic {token}"


def test_dashboard_rejects_missing_credentials():
    DashboardHandler.auth_token = "s3cret"
    fake = _FakeHandler(headers={})
    assert DashboardHandler._authenticated(fake) is False  # pylint: disable=protected-access
    assert ("status", 401) in fake.responses


def test_dashboard_rejects_wrong_password():
    DashboardHandler.auth_token = "s3cret"
    fake = _FakeHandler(headers={"Authorization": _basic_auth_header("wrong")})
    assert DashboardHandler._authenticated(fake) is False  # pylint: disable=protected-access
    assert ("status", 401) in fake.responses


def test_dashboard_accepts_correct_password():
    DashboardHandler.auth_token = "s3cret"
    fake = _FakeHandler(headers={"Authorization": _basic_auth_header("s3cret")})
    assert DashboardHandler._authenticated(fake) is True  # pylint: disable=protected-access
    assert ("status", 401) not in fake.responses


def test_dashboard_auth_disabled_when_token_empty():
    DashboardHandler.auth_token = None
    fake = _FakeHandler(headers={})
    assert DashboardHandler._authenticated(fake) is True  # pylint: disable=protected-access
