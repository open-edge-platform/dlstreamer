# ==============================================================================
# Copyright (C) 2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================
"""Internal, process-wide state for the dlstreamer.onvif Public API.

The Public API uses only ``Gst.*`` types in its 13 operation
signatures, but a small amount of internal bookkeeping is still
required to satisfy the contracts of:

* operation 6 (``list_defined_pipelines``) — the library must remember
  which ``Gst.Pipeline`` objects it owns and which camera each belongs
  to;
* operations 9 / 10 (event watches) — the library must reference-count
  per-camera PullPoint workers so it can ``Unsubscribe`` when the last
  watch goes away.

State is intentionally kept in module-level singletons (the library is
loaded once per process), guarded by a single re-entrant lock.
"""
from __future__ import annotations

import threading
from dataclasses import dataclass, field
from typing import TYPE_CHECKING, Any

if TYPE_CHECKING:  # pragma: no cover - typing only
    from gi.repository import Gst  # type: ignore[import-untyped]


@dataclass
class PipelineEntry:
    pipeline: "Gst.Pipeline"
    camera_xaddr: str            # stable identity (`onvif.xaddr` property)


@dataclass
class EventWatchEntry:
    watch_id: int                # GLib source id returned to the caller
    camera_xaddr: str
    topic_filter: str
    bus: "Gst.Bus"


@dataclass
class CameraEventState:
    """Per-camera PullPoint worker + bus."""

    bus: "Gst.Bus"
    pull_thread: Any = None       # worker handle (opaque)
    refcount: int = 0             # active watches on this camera


@dataclass
class Registry:
    pipelines: dict[str, PipelineEntry] = field(default_factory=dict)
    event_watches: dict[int, EventWatchEntry] = field(default_factory=dict)
    camera_events: dict[str, CameraEventState] = field(default_factory=dict)


_lock = threading.RLock()
_registry = Registry()


def state_lock() -> threading.RLock:
    return _lock


def registry() -> Registry:
    return _registry


def camera_xaddr(camera: "Gst.Device") -> str:
    """Return the stable ONVIF identity of a discovered camera.

    The provider publishes the camera service URL on the device's
    ``properties`` (``Gst.Structure``) under the key ``onvif.xaddr``.
    Falls back to the display name when the property is missing so
    unit tests can use ``Gst.Device`` stubs.
    """
    props = camera.get_properties()
    if props is not None:
        xaddr = props.get_string("onvif.xaddr")
        if xaddr:
            return xaddr
    return camera.get_display_name() or ""
