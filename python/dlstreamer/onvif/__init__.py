# ==============================================================================
# Copyright (C) 2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================
"""
dlstreamer.onvif — ONVIF camera discovery, media profiles and PTZ control.

Composed of three independent libraries:

- :mod:`dlstreamer.onvif.discovery` — **ONVIF Discovery Library**.
  Pure WS-Discovery layer. Finds ONVIF cameras on the local network and
  yields ``{"hostname", "port"}`` descriptors.

- :mod:`dlstreamer.onvif.camera_profiles` — **ONVIF Media Profiles Library**.
  Consumes discovery descriptors, connects to each camera and returns
  the media profiles (:class:`ONVIFProfile`) via
  :func:`read_camera_profiles`.

- :mod:`dlstreamer.onvif.ptz` — **ONVIF PTZ Library**.
  Filters profiles for PTZ support for cameras conforming to the ONVIF
  PTZ ver 2.0 WSDL.

The top-level module re-exports the public API of the three
sub-libraries so that ``from dlstreamer.onvif import ...`` keeps
working.

The planned :mod:`dlstreamer.onvif.video_engine` wrapper is also
available as a separate subpackage and can be re-exported here once its
API is finalized.
"""

# --- ONVIF Discovery Library ---
from .discovery import (
  discover_onvif_cameras,
  discover_onvif_cameras_async,
)

# --- ONVIF Media Profiles Library ---
from .camera_profiles import (
  CameraProfilesResult,
  ONVIFProfile,
  read_camera_profiles,
  read_camera_profiles_async,
)

# --- ONVIF PTZ Library ---
from .ptz import (
  PTZCapableProfile,
  PTZController,
  PTZPreset,
  PTZStatus,
  PTZVector,
  find_ptz_capable_profiles,
  find_ptz_capable_profiles_async,
  is_ptz_profile,
)


__all__ = [
    # discovery library
    "discover_onvif_cameras",
    "discover_onvif_cameras_async",
    # camera_profiles library
    "CameraProfilesResult",
    "ONVIFProfile",
    "read_camera_profiles",
    "read_camera_profiles_async",
    # ptz library
    "PTZCapableProfile",
    "PTZController",
    "PTZPreset",
    "PTZStatus",
    "PTZVector",
    "find_ptz_capable_profiles",
    "find_ptz_capable_profiles_async",
    "is_ptz_profile",
]
