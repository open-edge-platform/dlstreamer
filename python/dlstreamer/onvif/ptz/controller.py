# ==============================================================================
# Copyright (C) 2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================
"""
PTZ controller — one instance per ``(camera, profile_token)`` pair.

Wraps an :class:`onvif.ONVIFCamera` client plus its PTZ service and
exposes the operations defined by the ONVIF PTZ ver 2.0 WSDL
(http://www.onvif.org/onvif/ver20/ptz/wsdl/ptz.wsdl) that are most
useful to callers: movement (continuous / relative / absolute / stop),
presets, home position, plus read-only queries (status, configurations,
nodes).

Every synchronous method has an ``*_async`` variant that offloads the
blocking SOAP call to a worker thread.
"""
from __future__ import annotations

import asyncio
import threading
from typing import List, Optional

from onvif import ONVIFCamera  # pylint: disable=import-error

from .types import PTZCapableProfile, PTZPreset, PTZStatus, PTZVector


# ---------------------------------------------------------------------------
# Zeep → dataclass helpers
# ---------------------------------------------------------------------------


def _to_ptz_vector(node) -> PTZVector:
    """Convert a zeep ``PTZVector`` object into :class:`PTZVector`."""
    if node is None:
        return PTZVector()
    pan = tilt = zoom = 0.0
    pan_tilt = getattr(node, "PanTilt", None)
    if pan_tilt is not None:
        pan = float(getattr(pan_tilt, "x", 0.0) or 0.0)
        tilt = float(getattr(pan_tilt, "y", 0.0) or 0.0)
    zoom_node = getattr(node, "Zoom", None)
    if zoom_node is not None:
        zoom = float(getattr(zoom_node, "x", 0.0) or 0.0)
    return PTZVector(pan=pan, tilt=tilt, zoom=zoom)


def _move_status(move_status_node, axis: str) -> Optional[str]:
    """Return the string form of ``MoveStatus.<axis>`` or ``None``."""
    if move_status_node is None:
        return None
    value = getattr(move_status_node, axis, None)
    if value is None:
        return None
    return str(value)


# ---------------------------------------------------------------------------
# Controller
# ---------------------------------------------------------------------------


class PTZController:
    """PTZ operations for a single ``(camera, profile_token)`` pair.

    The ONVIF client and PTZ service are created lazily on first use and
    reused across calls. Instances are not thread-safe but are safe to
    use from a single asyncio event loop (each async method offloads the
    blocking SOAP call to :func:`asyncio.to_thread`).

    Example::

        ctrl = PTZController("192.168.1.10", 80, "profile_1", "admin", "…")
        ctrl.continuous_move(PTZVector(pan=0.3), timeout=1.5)
        time.sleep(1.6)
        print(ctrl.get_status())
    """

    def __init__(
        self,
        hostname: str,
        port: int,
        profile_token: str,
        username: str = "",
        password: str = "",
    ):
        """Create a PTZ controller for one camera/profile pair."""
        self.hostname = hostname
        self.port = int(port)
        self.profile_token = profile_token
        self.username = username
        self.password = password
        self._client: Optional[ONVIFCamera] = None
        self._ptz = None
        # Client-side auto-stop for continuous_move(timeout=...).
        self._auto_stop_timer: Optional[threading.Timer] = None
        self._auto_stop_seq: int = 0

    # ---- construction ----

    @classmethod
    def from_capable_profile(
        cls,
        profile: PTZCapableProfile,
        username: str = "",
        password: str = "",
    ) -> "PTZController":
        """Build a controller directly from a :class:`PTZCapableProfile`."""
        return cls(
            hostname=profile.hostname,
            port=profile.port,
            profile_token=profile.profile_token,
            username=username,
            password=password,
        )

    # ---- session lifecycle ----

    def _service(self):
        """Lazily create the ONVIF client and PTZ service."""
        if self._ptz is None:
            self._client = ONVIFCamera(
                self.hostname, self.port, self.username, self.password
            )
            self._ptz = self._client.create_ptz_service()
        return self._ptz

    def close(self) -> None:
        """Drop the cached ONVIF client + PTZ service."""
        self._cancel_auto_stop_timer()
        self._ptz = None
        self._client = None

    def __enter__(self) -> "PTZController":
        """Initialize the cached ONVIF service and return the controller."""
        self._service()
        return self

    def __exit__(self, exc_type, exc, tb) -> None:
        """Release the cached ONVIF client and PTZ service."""
        self.close()

    # ---- read operations ----

    def get_status(self) -> PTZStatus:
        """Return the current PTZ status for the configured profile."""
        raw = self._service().GetStatus({"ProfileToken": self.profile_token})
        move_status = getattr(raw, "MoveStatus", None)
        return PTZStatus(
            position=_to_ptz_vector(getattr(raw, "Position", None)),
            pan_tilt_move_status=_move_status(move_status, "PanTilt"),
            zoom_move_status=_move_status(move_status, "Zoom"),
            error=str(getattr(raw, "Error", "") or "") or None,
            utc_time=str(getattr(raw, "UtcTime", "") or "") or None,
        )

    def get_presets(self) -> List[PTZPreset]:
        """Return the list of saved PTZ presets for this profile."""
        raw = self._service().GetPresets({"ProfileToken": self.profile_token}) or []
        presets: List[PTZPreset] = []
        for entry in raw:
            token = str(getattr(entry, "token", "") or "")
            name = str(getattr(entry, "Name", "") or "")
            position_node = getattr(entry, "PTZPosition", None)
            position = _to_ptz_vector(position_node) if position_node is not None else None
            presets.append(PTZPreset(token=token, name=name, position=position))
        return presets

    def get_configurations(self) -> list:
        """Return the raw list of ONVIF PTZ configurations exposed by the device."""
        return self._service().GetConfigurations() or []

    def get_nodes(self) -> list:
        """Return the raw list of ONVIF PTZ nodes exposed by the device."""
        return self._service().GetNodes() or []

    # ---- movement ----

    def continuous_move(
        self,
        velocity: PTZVector,
        timeout: Optional[float] = None,
    ) -> None:
        """Issue ``ContinuousMove``; optionally auto-stop after ``timeout`` seconds.

        The auto-stop is implemented **client-side** via a background
        :class:`threading.Timer` that issues :meth:`stop` after the
        specified duration. The SOAP ``Timeout`` field is intentionally
        not sent: several ONVIF cameras reject fractional ``xs:duration``
        values (e.g. ``PT0.500S``) with a generic SOAP fault. Handling
        the auto-stop in the client keeps behavior identical across
        vendors and preserves sub-second precision.

        Any pending auto-stop from a previous call is cancelled before
        the new move is issued, so back-to-back calls behave as expected.

        Args:
            velocity: Movement velocity vector (pan/tilt/zoom).
            timeout: Optional client-side auto-stop delay in seconds.
                ``None`` means “move until :meth:`stop` is called”.
        """
        request: dict = {
            "ProfileToken": self.profile_token,
            "Velocity": velocity.as_velocity_dict(),
        }
        self._cancel_auto_stop_timer()
        self._service().ContinuousMove(request)
        if timeout is not None and float(timeout) > 0.0:
            self._start_auto_stop_timer(float(timeout))

    def relative_move(
        self,
        translation: PTZVector,
        speed: Optional[PTZVector] = None,
    ) -> None:
        """Issue ``RelativeMove`` (offset from current position)."""
        request: dict = {
            "ProfileToken": self.profile_token,
            "Translation": translation.as_position_dict(),
        }
        if speed is not None:
            request["Speed"] = speed.as_velocity_dict()
        self._service().RelativeMove(request)

    def absolute_move(
        self,
        position: PTZVector,
        speed: Optional[PTZVector] = None,
    ) -> None:
        """Issue ``AbsoluteMove`` (target position in profile coordinate space)."""
        request: dict = {
            "ProfileToken": self.profile_token,
            "Position": position.as_position_dict(),
        }
        if speed is not None:
            request["Speed"] = speed.as_velocity_dict()
        self._service().AbsoluteMove(request)

    def stop(self, pan_tilt: bool = True, zoom: bool = True) -> None:
        """Issue ``Stop`` for the pan/tilt and/or zoom axis.

        Also cancels any pending client-side auto-stop scheduled by
        :meth:`continuous_move`.
        """
        self._cancel_auto_stop_timer()
        self._service().Stop(
            {
                "ProfileToken": self.profile_token,
                "PanTilt": bool(pan_tilt),
                "Zoom": bool(zoom),
            }
        )

    # ---- auto-stop timer plumbing ----

    def _start_auto_stop_timer(self, delay: float) -> None:
        """Schedule a background :class:`threading.Timer` to call ``Stop``."""
        self._auto_stop_seq += 1
        seq = self._auto_stop_seq
        service = self._service()
        profile_token = self.profile_token

        def _cb() -> None:
            # Skip if a newer move/stop has invalidated us in the meantime.
            if seq != self._auto_stop_seq:
                return
            try:
                service.Stop(
                    {
                        "ProfileToken": profile_token,
                        "PanTilt": True,
                        "Zoom": True,
                    }
                )
            except Exception:  # pylint: disable=broad-exception-caught
                # Best-effort background auto-stop; transient errors are
                # not the caller's problem here.
                pass

        timer = threading.Timer(delay, _cb)
        timer.daemon = True
        timer.start()
        self._auto_stop_timer = timer

    def _cancel_auto_stop_timer(self) -> None:
        """Cancel a pending auto-stop timer and invalidate its callback."""
        # Bump the sequence so any in-flight timer callback becomes a no-op.
        self._auto_stop_seq += 1
        timer = self._auto_stop_timer
        if timer is not None:
            timer.cancel()
            self._auto_stop_timer = None

    # ---- presets ----

    def set_preset(
        self,
        name: str,
        preset_token: Optional[str] = None,
    ) -> str:
        """Create or overwrite a preset at the current position; return its token."""
        request: dict = {
            "ProfileToken": self.profile_token,
            "PresetName": name,
        }
        if preset_token:
            request["PresetToken"] = preset_token
        response = self._service().SetPreset(request)
        token = getattr(response, "PresetToken", None)
        if token is None:
            token = response
        return str(token)

    def goto_preset(
        self,
        preset_token: str,
        speed: Optional[PTZVector] = None,
    ) -> None:
        """Move the camera to a saved preset."""
        request: dict = {
            "ProfileToken": self.profile_token,
            "PresetToken": preset_token,
        }
        if speed is not None:
            request["Speed"] = speed.as_velocity_dict()
        self._service().GotoPreset(request)

    def remove_preset(self, preset_token: str) -> None:
        """Delete a saved preset."""
        self._service().RemovePreset(
            {
                "ProfileToken": self.profile_token,
                "PresetToken": preset_token,
            }
        )

    # ---- home position ----

    def home_supported(self) -> bool:
        """Return True when the camera advertises Home Position support.

        Reads the PTZ node capabilities (``GetNodes``) and checks the
        ``HomeSupported`` flag. :meth:`goto_home` / :meth:`set_home` are
        optional ONVIF operations and only valid when this is true; calling
        them on a camera that reports ``HomeSupported=False`` raises a SOAP
        fault.
        """
        try:
            nodes = self.get_nodes()
        except Exception:  # pylint: disable=broad-exception-caught
            return False
        return any(bool(getattr(node, "HomeSupported", False)) for node in nodes)

    def goto_home(self, speed: Optional[PTZVector] = None) -> None:
        """Move the camera to its home position."""
        request: dict = {"ProfileToken": self.profile_token}
        if speed is not None:
            request["Speed"] = speed.as_velocity_dict()
        self._service().GotoHomePosition(request)

    def set_home(self) -> None:
        """Persist the current position as the new home position."""
        self._service().SetHomePosition({"ProfileToken": self.profile_token})

    # ---- async wrappers ----

    async def get_status_async(self) -> PTZStatus:
        """Async wrapper for :meth:`get_status`."""
        return await asyncio.to_thread(self.get_status)

    async def get_presets_async(self) -> List[PTZPreset]:
        """Async wrapper for :meth:`get_presets`."""
        return await asyncio.to_thread(self.get_presets)

    async def continuous_move_async(
        self,
        velocity: PTZVector,
        timeout: Optional[float] = None,
    ) -> None:
        """Async wrapper for :meth:`continuous_move`."""
        await asyncio.to_thread(self.continuous_move, velocity, timeout)

    async def relative_move_async(
        self,
        translation: PTZVector,
        speed: Optional[PTZVector] = None,
    ) -> None:
        """Async wrapper for :meth:`relative_move`."""
        await asyncio.to_thread(self.relative_move, translation, speed)

    async def absolute_move_async(
        self,
        position: PTZVector,
        speed: Optional[PTZVector] = None,
    ) -> None:
        """Async wrapper for :meth:`absolute_move`."""
        await asyncio.to_thread(self.absolute_move, position, speed)

    async def stop_async(self, pan_tilt: bool = True, zoom: bool = True) -> None:
        """Async wrapper for :meth:`stop`."""
        await asyncio.to_thread(self.stop, pan_tilt, zoom)

    async def goto_preset_async(
        self,
        preset_token: str,
        speed: Optional[PTZVector] = None,
    ) -> None:
        """Async wrapper for :meth:`goto_preset`."""
        await asyncio.to_thread(self.goto_preset, preset_token, speed)

    async def set_preset_async(
        self,
        name: str,
        preset_token: Optional[str] = None,
    ) -> str:
        """Async wrapper for :meth:`set_preset`."""
        return await asyncio.to_thread(self.set_preset, name, preset_token)

    async def remove_preset_async(self, preset_token: str) -> None:
        """Async wrapper for :meth:`remove_preset`."""
        await asyncio.to_thread(self.remove_preset, preset_token)

    async def goto_home_async(self, speed: Optional[PTZVector] = None) -> None:
        """Async wrapper for :meth:`goto_home`."""
        await asyncio.to_thread(self.goto_home, speed)

    async def set_home_async(self) -> None:
        """Async wrapper for :meth:`set_home`."""
        await asyncio.to_thread(self.set_home)
