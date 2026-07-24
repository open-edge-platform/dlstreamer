# ==============================================================================
# Copyright (C) 2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================
"""
Discover ONVIF cameras that expose an Events service.

An ONVIF camera is considered "event-capable" when it responds to
``GetServiceCapabilities`` on its Events service. Most Profile-S cameras
qualify, but the check gives a fast, cheap way to filter a discovery
sweep before opening subscriptions.

The helpers here mirror the sync/async surface of the other libraries
in :mod:`dlstreamer.onvif`.
"""
from __future__ import annotations

import asyncio
from typing import (
    AsyncIterable,
    AsyncIterator,
    Iterable,
    Iterator,
    Union,
)

from onvif import ONVIFCamera  # pylint: disable=import-error

from .types import EventCapableCamera


CameraSource = Union[Iterable[dict], AsyncIterable[dict]]


def _endpoint(cam: dict) -> tuple[str, int]:
    """Extract ``(hostname, port)`` from a discovery descriptor."""
    return str(cam["hostname"]), int(cam.get("port") or 80)


def is_event_capable(
    hostname: str,
    port: int,
    username: str = "",
    password: str = "",
    verbose: bool = False,
) -> bool:
    """Return True if the camera exposes an ONVIF Events service.

    Probes ``GetServiceCapabilities`` on the Events endpoint. Any error
    (network, auth, missing service) yields ``False`` — the intent is a
    quick capability check, not a fault report.
    """
    return _probe(hostname, int(port), username, password, verbose).ok


def _probe(
    hostname: str,
    port: int,
    username: str,
    password: str,
    verbose: bool,
) -> EventCapableCamera:
    """Return an :class:`EventCapableCamera` reflecting the probe outcome."""
    result = EventCapableCamera(hostname=hostname, port=int(port))
    try:
        client = ONVIFCamera(hostname, int(port), username, password)
        client.create_events_service().GetServiceCapabilities()
    except Exception as exc:  # pylint: disable=broad-exception-caught
        result.error = f"{type(exc).__name__}: {exc}"
        if verbose:
            print(f"[WARN] {hostname}:{port} events probe failed: {result.error}")
    return result


def find_event_capable_cameras(
    cameras: Iterable[dict],
    username: str = "",
    password: str = "",
    verbose: bool = False,
) -> Iterator[EventCapableCamera]:
    """Yield only cameras from ``cameras`` that expose an Events service.

    Example::

        from dlstreamer.onvif.discovery import discover_onvif_cameras
        from dlstreamer.onvif.event_manager import find_event_capable_cameras
        from dlstreamer.onvif.event_manager.engine import OnvifEventEngine

        for cap in find_event_capable_cameras(
            discover_onvif_cameras(), username="admin", password="…"
        ):
            with OnvifEventEngine.from_capable_camera(cap, "admin", "…") as eng:
                eng.subscribe()
                for note in eng.pull(timeout="PT5S", limit=10):
                    print(note.short())
    """
    for cam in cameras:
        hostname, port = _endpoint(cam)
        result = _probe(hostname, port, username, password, verbose)
        if result.ok:
            yield result


async def find_event_capable_cameras_async(
    cameras: CameraSource,
    username: str = "",
    password: str = "",
    verbose: bool = False,
) -> AsyncIterator[EventCapableCamera]:
    """Async counterpart of :func:`find_event_capable_cameras`.

    Accepts both a plain iterable and an async iterable of camera
    descriptors, so it can be piped directly from
    :func:`~dlstreamer.onvif.discovery.discover_onvif_cameras_async`.
    """
    async def _probe_async(cam):
        hostname, port = _endpoint(cam)
        return await asyncio.to_thread(
            _probe, hostname, port, username, password, verbose
        )

    if hasattr(cameras, "__aiter__"):
        async for cam in cameras:  # type: ignore[union-attr]
            result = await _probe_async(cam)
            if result.ok:
                yield result
    else:
        for cam in cameras:  # type: ignore[union-attr]
            result = await _probe_async(cam)
            if result.ok:
                yield result
