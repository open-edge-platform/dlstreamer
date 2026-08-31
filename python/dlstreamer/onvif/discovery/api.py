# ==============================================================================
# Copyright (C) 2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================
"""
dlstreamer.onvif.discovery — ONVIF Camera Discovery Library.

Pure WS-Discovery layer with no ONVIF media-service, GStreamer or
configuration dependencies. Finds ONVIF-compliant cameras on the local
network and yields them as ``{"hostname": str, "port": int}`` dicts.

Media profile querying (video/audio/PTZ/RTSP URL) lives in the
companion library :mod:`dlstreamer.onvif.camera_profiles`, which
consumes the descriptors produced here.

Low-level WS-Discovery generators:
    discover_onvif_cameras()       — synchronous
    discover_onvif_cameras_async() — asynchronous

XML helpers:
    extract_xaddrs()
    parse_xaddrs_url()
"""

from .ws_discovery import (
    discover_onvif_cameras,
    discover_onvif_cameras_async,
    extract_xaddrs,
    parse_xaddrs_url,
)

__all__ = [
    "discover_onvif_cameras",
    "discover_onvif_cameras_async",
    "extract_xaddrs",
    "parse_xaddrs_url",
]
