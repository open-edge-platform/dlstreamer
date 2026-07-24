# ==============================================================================
# Copyright (C) 2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================
"""
dlstreamer.onvif.ptz — PTZ control for ONVIF cameras.

Implements support for cameras that expose profiles conforming to the
ONVIF PTZ Service ver 2.0 WSDL
(http://www.onvif.org/onvif/ver20/ptz/wsdl/ptz.wsdl).

Depends on:

- :mod:`dlstreamer.onvif.discovery` — locates ONVIF cameras on the
  network (used only via :mod:`~dlstreamer.onvif.camera_profiles`,
  which consumes discovery output).
- :mod:`dlstreamer.onvif.camera_profiles` — reads media profiles from
  discovered cameras; used to filter out profiles without a PTZ
  configuration.

Public API:

Data model:
    PTZVector           — PanTilt (x,y) + Zoom (z) vector
    PTZStatus           — snapshot returned by ``GetStatus``
    PTZPreset           — named PTZ preset
    PTZCapableProfile   — a discovered camera profile that supports PTZ

PTZ-capable profile discovery (built on :mod:`~dlstreamer.onvif.camera_profiles`):
    is_ptz_profile()
    find_ptz_capable_profiles()         — sync generator
    find_ptz_capable_profiles_async()   — async generator

Controller:
    PTZController — one instance per ``(camera, profile)`` pair; wraps
    the ONVIF PTZ service and offers movement, status, preset and home
    operations plus their ``*_async`` variants.
"""

from .types import (
    PTZCapableProfile,
    PTZPreset,
    PTZStatus,
    PTZVector,
)
from .capabilities import (
    find_ptz_capable_profiles,
    find_ptz_capable_profiles_async,
    is_ptz_profile,
)
from .controller import PTZController

__all__ = [
    # data model
    "PTZVector",
    "PTZStatus",
    "PTZPreset",
    "PTZCapableProfile",
    # capability discovery
    "is_ptz_profile",
    "find_ptz_capable_profiles",
    "find_ptz_capable_profiles_async",
    # controller
    "PTZController",
]
