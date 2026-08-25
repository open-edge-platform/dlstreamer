# ==============================================================================
# Copyright (C) 2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================
"""
dlstreamer.onvif.camera_profiles — ONVIF media profile library.

Owns the ONVIF media profile data model and reads profiles from cameras
found by :mod:`dlstreamer.onvif.discovery`. The two libraries are
complementary:

- :mod:`dlstreamer.onvif.discovery` finds ONVIF cameras on the network
  and yields ``{"hostname": str, "port": int}`` descriptors.
- :mod:`dlstreamer.onvif.camera_profiles` (this module) consumes those
  descriptors and returns the media profiles reported by each camera.

Public API:

Data model:
    ONVIFProfile           — per-camera media profile (video/audio/PTZ, RTSP URL)
    CameraProfilesResult   — profiles + error status for a single camera

Convenience functions:
    read_camera_profiles()        — sync generator over discovered cameras
    read_camera_profiles_async()  — async generator over discovered cameras
"""

from .types import ONVIFProfile
from .reader import (
    CameraProfilesResult,
    read_camera_profiles,
    read_camera_profiles_async,
)

__all__ = [
    "ONVIFProfile",
    "CameraProfilesResult",
    "read_camera_profiles",
    "read_camera_profiles_async",
]
