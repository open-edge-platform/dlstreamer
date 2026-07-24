# ==============================================================================
# Copyright (C) 2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================
"""
Discover PTZ-capable ONVIF profiles.

Consumes camera descriptors produced by :mod:`dlstreamer.onvif.discovery`,
reads their media profiles through :mod:`dlstreamer.onvif.camera_profiles`
and filters out everything that does not carry a PTZ configuration.
"""
from __future__ import annotations

from typing import (
    AsyncIterable,
    AsyncIterator,
    Iterable,
    Iterator,
    List,
    Union,
)

from dlstreamer.onvif.camera_profiles import (
    CameraProfilesResult,
    ONVIFProfile,
    read_camera_profiles,
    read_camera_profiles_async,
)

from .types import PTZCapableProfile


CameraSource = Union[Iterable[dict], AsyncIterable[dict]]


def is_ptz_profile(profile: ONVIFProfile) -> bool:
    """Return True when the profile carries a non-empty PTZConfiguration."""
    return bool(getattr(profile, "ptz_token", ""))


def _to_capable(result: CameraProfilesResult) -> List[PTZCapableProfile]:
    """Extract every PTZ-capable profile from a :class:`CameraProfilesResult`."""
    if not result.ok:
        return []
    out: List[PTZCapableProfile] = []
    for profile in result.profiles:
        if not is_ptz_profile(profile):
            continue
        out.append(
            PTZCapableProfile(
                hostname=result.hostname,
                port=result.port,
                profile_token=profile.token,
                profile_name=profile.name,
                ptz_configuration_token=profile.ptz_token,
                ptz_node_token=profile.ptz_node_token,
            )
        )
    return out


def find_ptz_capable_profiles(
    cameras: Iterable[dict],
    username: str = "",
    password: str = "",
    verbose: bool = False,
) -> Iterator[PTZCapableProfile]:
    """Read profiles from ``cameras`` and yield only PTZ-capable ones (sync).

    Example::

        from dlstreamer.onvif.discovery import discover_onvif_cameras
        from dlstreamer.onvif.ptz import find_ptz_capable_profiles
        from dlstreamer.onvif.ptz.controller import PTZController

        for cap in find_ptz_capable_profiles(
            discover_onvif_cameras(), username="admin", password="…"
        ):
            ctrl = PTZController.from_capable_profile(cap, "admin", "…")
            print(cap.camera_id, cap.profile_name, ctrl.get_status())
    """
    for result in read_camera_profiles(
        cameras,
        username=username,
        password=password,
        verbose=verbose,
    ):
        yield from _to_capable(result)


async def find_ptz_capable_profiles_async(
    cameras: CameraSource,
    username: str = "",
    password: str = "",
    verbose: bool = False,
) -> AsyncIterator[PTZCapableProfile]:
    """Async counterpart of :func:`find_ptz_capable_profiles`.

    Accepts both a plain iterable and an async iterable of camera
    descriptors, so it can be piped directly from
    :func:`~dlstreamer.onvif.discovery.discover_onvif_cameras_async`.
    """
    async for result in read_camera_profiles_async(
        cameras,
        username=username,
        password=password,
        verbose=verbose,
    ):
        for cap in _to_capable(result):
            yield cap
