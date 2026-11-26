"""Unit tests for ONVIFClient's transport-scheme handling
(samples/gstreamer/python/onvif_camera_analytics_validation/util.py).

No GStreamer/PyGObject dependency - safe to run in any Python environment
with the sample's requirements.txt installed.

Run with: pytest test_onvif_client.py -v
"""
# ==============================================================================
# Copyright (C) 2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================

import os
import sys

_SAMPLE_DIR = os.path.join(
    os.path.dirname(os.path.abspath(__file__)),
    "..", "..", "..", "samples", "gstreamer", "python", "onvif_camera_analytics_validation",
)
sys.path.insert(0, os.path.abspath(_SAMPLE_DIR))

from util import ONVIFClient  # pylint: disable=wrong-import-position


def test_onvifclient_defaults_to_http():
    client = ONVIFClient("192.168.1.100", 80)
    assert client.device_url == "http://192.168.1.100:80/onvif/device_service"
    assert client.media_url == "http://192.168.1.100:80/onvif/media_service"


def test_onvifclient_accepts_https_scheme():
    client = ONVIFClient("192.168.1.100", 443, scheme="https")
    assert client.device_url == "https://192.168.1.100:443/onvif/device_service"
    assert client.media_url == "https://192.168.1.100:443/onvif/media_service"
