# ==============================================================================
# Copyright (C) 2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================
"""
Data model for :mod:`dlstreamer.onvif.ptz`.

The dataclasses here mirror the small subset of the ONVIF PTZ ver 2.0
WSDL that the controller and capability helpers need to expose to
callers.
"""
from __future__ import annotations

from dataclasses import dataclass, field
from typing import Optional


@dataclass
class PTZVector:
    """PanTilt (x, y) plus Zoom (z) vector.

    Interpretation depends on the PTZ configuration in use:

    - ``ContinuousMove``: values are velocities, typically normalized to
      the range ``[-1.0, 1.0]``.
    - ``AbsoluteMove`` / ``RelativeMove`` / ``GetStatus``: values are in
      the coordinate space declared by the profile's
      ``PTZConfigurationOptions``. For generic space this is again
      ``[-1.0, 1.0]``.
    """

    pan: float = 0.0
    tilt: float = 0.0
    zoom: float = 0.0

    def as_position_dict(self) -> dict:
        """Serialize as ONVIF ``PTZVector`` (Position / Translation)."""
        return {
            "PanTilt": {"x": float(self.pan), "y": float(self.tilt)},
            "Zoom": {"x": float(self.zoom)},
        }

    def as_velocity_dict(self) -> dict:
        """Serialize as ONVIF ``PTZSpeed`` / ``PTZVelocity``.

        ONVIF re-uses the same PanTilt/Zoom vector shape for velocities
        and speeds, so this is an alias for :meth:`as_position_dict`.
        """
        return self.as_position_dict()


@dataclass
class PTZStatus:
    """Snapshot returned by ``GetStatus``.

    Attributes:
        position: Current PanTilt + Zoom position.
        pan_tilt_move_status: ``"IDLE"`` / ``"MOVING"`` / ``"UNKNOWN"``,
            or ``None`` when the device does not report it.
        zoom_move_status: Same as above for the zoom axis.
        error: Free-form error string reported by the device (if any).
        utc_time: Device-reported UTC timestamp of the snapshot.
    """

    position: PTZVector = field(default_factory=PTZVector)
    pan_tilt_move_status: Optional[str] = None
    zoom_move_status: Optional[str] = None
    error: Optional[str] = None
    utc_time: Optional[str] = None


@dataclass
class PTZPreset:
    """A single ONVIF PTZ preset entry."""

    token: str
    name: str = ""
    position: Optional[PTZVector] = None


@dataclass
class PTZCapableProfile:
    """A discovered camera profile that supports PTZ.

    Emitted by :func:`~dlstreamer.onvif.ptz.find_ptz_capable_profiles`
    and consumed by :class:`~dlstreamer.onvif.ptz.controller.PTZController`
    (via :meth:`PTZController.from_capable_profile`).
    """

    hostname: str
    port: int
    profile_token: str
    profile_name: str = ""
    ptz_configuration_token: str = ""
    ptz_node_token: str = ""

    @property
    def camera_id(self) -> str:
        """``"hostname:port"`` identifier used by higher layers."""
        return f"{self.hostname}:{self.port}"
