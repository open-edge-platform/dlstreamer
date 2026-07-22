# ==============================================================================
# Copyright (C) 2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================
"""
Read ONVIF media profiles from cameras produced by
:mod:`dlstreamer.onvif.discovery`.

The discovery layer yields plain camera descriptors
(``{"hostname": str, "port": int}``). This module turns each descriptor
into an ONVIF session, queries the media service and returns the
resulting :class:`ONVIFProfile` list wrapped in :class:`CameraProfilesResult`
so per-camera errors do not abort the whole sweep.
"""
from __future__ import annotations

import asyncio
from dataclasses import dataclass, field
from typing import (
    AsyncIterable,
    AsyncIterator,
    Iterable,
    Iterator,
    List,
    Optional,
    Union,
)

from onvif import ONVIFCamera  # pylint: disable=import-error

from .types import ONVIFProfile
from .media_profiles import _fetch_media_profiles


def _fetch_mac_address(client, verbose: bool = False) -> str:
    """Query the camera's Device Management service for its MAC address.

    Uses ``GetNetworkInterfaces()`` and returns the ``HwAddress`` of the
    first interface found.  Returns an empty string when the information
    is unavailable.
    """
    try:
        device_service = client.create_devicemgmt_service()
        interfaces = device_service.GetNetworkInterfaces()
        for iface in interfaces:
            if hasattr(iface, "Info") and iface.Info:
                hw = getattr(iface.Info, "HwAddress", None)
                if hw:
                    if verbose:
                        print(f"  MAC address: {hw}")
                    return str(hw)
    except Exception as exc:  # pylint: disable=broad-exception-caught
        if verbose:
            print(f"[WARN] Could not retrieve MAC address: {exc}")
    return ""


CameraSource = Union[Iterable[dict], AsyncIterable[dict]]


@dataclass
class CameraProfilesResult:
    """Profiles read from a single camera, plus any transport-level error.

    Attributes:
        hostname: Camera hostname/IP as reported by discovery.
        port: ONVIF service port as reported by discovery.
        profiles: Media profiles returned by the camera (empty on error).
        mac_address: MAC address of the camera (empty string if unavailable).
        error: ``None`` on success, otherwise a ``"ExcType: message"`` string.
    """

    hostname: str
    port: int
    profiles: List[ONVIFProfile] = field(default_factory=list)
    mac_address: str = ""
    error: Optional[str] = None

    @property
    def ok(self) -> bool:
        """True when the camera responded and profiles were parsed."""
        return self.error is None


def _camera_endpoint(cam: dict) -> tuple[str, int]:
    """Extract ``(hostname, port)`` from a discovery descriptor."""
    return str(cam["hostname"]), int(cam.get("port") or 80)


class CameraProfileReader:
    """Credentialed reader that turns discovered cameras into media profiles.

    Encapsulates the ONVIF username/password once so callers can iterate
    over an entire discovery stream without re-passing credentials.
    """

    def __init__(
        self,
        username: str = "",
        password: str = "",
        verbose: bool = False,
    ):
        self.username = username
        self.password = password
        self.verbose = verbose

    # ---- Single camera ----

    def read_one_sync(self, hostname: str, port: int) -> CameraProfilesResult:
        """Connect to one camera and return its profiles (blocking)."""
        result = CameraProfilesResult(hostname=hostname, port=int(port))
        try:
            client = ONVIFCamera(hostname, port, self.username, self.password)
            result.mac_address = _fetch_mac_address(client, verbose=self.verbose)
            profiles = _fetch_media_profiles(client, verbose=self.verbose)
            for profile in profiles:
                profile.ip = hostname
                profile.port = int(port)
                profile.username = self.username
                profile.password = self.password
                profile.mac_address = result.mac_address
            result.profiles = profiles
        except Exception as exc:  # pylint: disable=broad-exception-caught
            result.error = f"{type(exc).__name__}: {exc}"
            if self.verbose:
                print(
                    f"[WARN] Failed to read profiles from "
                    f"{hostname}:{port}: {result.error}"
                )
        return result

    async def read_one(self, hostname: str, port: int) -> CameraProfilesResult:
        """Async wrapper around :meth:`read_one_sync` (runs in a worker thread)."""
        return await asyncio.to_thread(self.read_one_sync, hostname, port)

    # ---- Multiple cameras ----

    def read_many_sync(
        self, cameras: Iterable[dict]
    ) -> Iterator[CameraProfilesResult]:
        """Iterate discovered cameras and yield a result per camera (blocking)."""
        for cam in cameras:
            hostname, port = _camera_endpoint(cam)
            yield self.read_one_sync(hostname, port)

    async def read_many(
        self, cameras: CameraSource
    ) -> AsyncIterator[CameraProfilesResult]:
        """Iterate cameras from a sync or async source and yield results.

        Reads each camera as soon as the source produces it, so pairing
        with :func:`~dlstreamer.onvif.discovery.discover_onvif_cameras_async`
        starts fetching profiles before the discovery window closes.
        """
        if hasattr(cameras, "__aiter__"):
            async for cam in cameras:  # type: ignore[union-attr]
                hostname, port = _camera_endpoint(cam)
                yield await self.read_one(hostname, port)
        else:
            for cam in cameras:  # type: ignore[union-attr]
                hostname, port = _camera_endpoint(cam)
                yield await self.read_one(hostname, port)


# ---------------------------------------------------------------------------
# Module-level convenience wrappers
# ---------------------------------------------------------------------------


def read_camera_profiles(
    cameras: Iterable[dict],
    username: str = "",
    password: str = "",
    verbose: bool = False,
) -> Iterator[CameraProfilesResult]:
    """Read profiles from a sync stream of discovered cameras.

    Example::

        from dlstreamer.onvif.discovery import discover_onvif_cameras
        from dlstreamer.onvif.camera_profiles import read_camera_profiles

        for result in read_camera_profiles(
            discover_onvif_cameras(), username="admin", password="…"
        ):
            if result.ok:
                for profile in result.profiles:
                    print(result.hostname, profile.name, profile.rtsp_url)
    """
    reader = CameraProfileReader(username, password, verbose)
    yield from reader.read_many_sync(cameras)


async def read_camera_profiles_async(
    cameras: CameraSource,
    username: str = "",
    password: str = "",
    verbose: bool = False,
) -> AsyncIterator[CameraProfilesResult]:
    """Async counterpart of :func:`read_camera_profiles`.

    Example::

        from dlstreamer.onvif.discovery import discover_onvif_cameras_async
        from dlstreamer.onvif.camera_profiles import read_camera_profiles_async

        async for result in read_camera_profiles_async(
            discover_onvif_cameras_async(), username="admin", password="…"
        ):
            ...
    """
    reader = CameraProfileReader(username, password, verbose)
    async for result in reader.read_many(cameras):
        yield result
