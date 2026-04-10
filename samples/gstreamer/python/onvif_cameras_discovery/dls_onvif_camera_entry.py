# ==============================================================================
# Copyright (C) 2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================
"""Unified data model binding discovered ONVIF cameras to their pipelines."""
from __future__ import annotations

import threading
from dataclasses import dataclass, field
from datetime import datetime
from enum import Enum, auto
from typing import Any, Optional

from dls_onvif_data import ONVIFProfile
from dls_onvif_discovery_thread import DlsLaunchedPipeline


class CameraStatus(Enum):
    """Lifecycle status of a discovered camera."""

    DISCOVERED = auto()
    CONNECTING = auto()
    STREAMING = auto()
    ERROR = auto()
    REMOVED = auto()


@dataclass
class DlsOnvifCameraEntry:  # pylint: disable=too-many-instance-attributes
    """Single point of truth for a discovered camera and its runtime state.

    Binds together:
      - discovery data   (hostname, port)
      - ONVIF profiles   (video/audio/ptz configuration)
      - GStreamer pipelines running for each profile
      - lifecycle metadata (timestamps, status, errors)

    Keyed by ``camera_id`` which is ``hostname:port``.
    """

    # --- discovery info ---
    hostname: str
    port: int = 80
    username: str = ""
    password: str = ""

    # --- ONVIF profiles retrieved after connection ---
    profiles: list[ONVIFProfile] = field(default_factory=list)

    # --- running pipelines, one per profile ---
    pipelines: list[DlsLaunchedPipeline] = field(default_factory=list)

    # --- lifecycle metadata ---
    status: CameraStatus = CameraStatus.DISCOVERED
    discovered_at: datetime = field(default_factory=datetime.now)
    last_seen_at: Optional[datetime] = None
    error_message: str = ""

    # --- raw discovery dict (for backward compat) ---
    raw_discovery: dict[str, Any] = field(default_factory=dict)

    # ---- derived helpers ----

    @property
    def camera_id(self) -> str:
        """Unique key used for lookups and comparisons."""
        return f"{self.hostname}:{self.port}"

    @property
    def is_streaming(self) -> bool:
        """Return True when camera status is STREAMING."""
        return self.status == CameraStatus.STREAMING

    @property
    def pipeline_count(self) -> int:
        """Return the number of pipelines (running or not)."""
        return len(self.pipelines)

    @property
    def active_pipeline_count(self) -> int:
        """Pipelines whose worker thread is still alive."""
        return sum(
            1 for p in self.pipelines if p.thread is not None and p.thread.is_alive()
        )

    @property
    def profile_names(self) -> list[str]:
        """Return the list of ONVIF profile names."""
        return [p.name for p in self.profiles]

    # ---- pipeline management ----

    def add_pipeline(self, pipeline: DlsLaunchedPipeline) -> None:
        """Append a pipeline to this camera entry."""
        self.pipelines.append(pipeline)

    def stop_all_pipelines(self) -> list[str]:
        """Stop every pipeline; return list of error messages (empty = all ok)."""
        errors: list[str] = []
        for pipeline in self.pipelines:
            try:
                pipeline.stop()
            except Exception as exc:  # pylint: disable=broad-exception-caught
                errors.append(f"{pipeline.name}: {exc}")
        self.pipelines.clear()
        return errors

    # ---- status transitions ----

    def mark_streaming(self) -> None:
        """Transition status to STREAMING and clear any error."""
        self.status = CameraStatus.STREAMING
        self.error_message = ""

    def mark_error(self, message: str) -> None:
        """Transition status to ERROR with a descriptive message."""
        self.status = CameraStatus.ERROR
        self.error_message = message

    def mark_removed(self) -> None:
        """Transition status to REMOVED."""
        self.status = CameraStatus.REMOVED

    def touch(self) -> None:
        """Update *last_seen_at* to now (called on every discovery cycle)."""
        self.last_seen_at = datetime.now()

    # ---- serialisation helpers ----

    def to_dict(self) -> dict[str, Any]:
        """Flat summary suitable for logging / JSON export."""
        return {
            "camera_id": self.camera_id,
            "hostname": self.hostname,
            "port": self.port,
            "status": self.status.name,
            "profiles": self.profile_names,
            "pipeline_count": self.pipeline_count,
            "active_pipelines": self.active_pipeline_count,
            "discovered_at": self.discovered_at.isoformat(),
            "last_seen_at": (
                self.last_seen_at.isoformat() if self.last_seen_at else None
            ),
            "error": self.error_message or None,
        }

    def __repr__(self) -> str:
        return (
            f"DlsOnvifCameraEntry("
            f"{self.camera_id}, "
            f"status={self.status.name}, "
            f"profiles={len(self.profiles)}, "
            f"pipelines={self.pipeline_count}"
            f")"
        )

    # ---- factory ----

    @classmethod
    def from_discovery_dict(
        cls,
        camera: dict[str, Any],
        username: str = "",
        password: str = "",
    ) -> DlsOnvifCameraEntry:
        """Create an entry from the raw dict returned by WS-Discovery."""
        hostname = camera.get("hostname") or camera.get("ip_address", "")
        port = camera.get("port", 80)
        return cls(
            hostname=hostname,
            port=port,
            username=username,
            password=password,
            raw_discovery=dict(camera),
        )


class DlsOnvifCameraRegistry:
    """Thread-safe registry of all known cameras, keyed by ``camera_id``.

    Replaces the previous pattern of two separate lists
    (``active_cameras: list[dict]`` and ``__gst_processes: list[DlsLaunchedPipeline]``)
    with a single mapping that keeps camera data and pipelines together.
    """

    def __init__(self) -> None:
        self._cameras: dict[str, DlsOnvifCameraEntry] = {}
        self._lock = threading.Lock()

    # ---- CRUD ----

    def add(self, entry: DlsOnvifCameraEntry) -> None:
        """Register a camera entry (overwrites if camera_id exists)."""
        with self._lock:
            self._cameras[entry.camera_id] = entry

    def get(self, camera_id: str) -> Optional[DlsOnvifCameraEntry]:
        """Return the entry for *camera_id*, or ``None``."""
        with self._lock:
            return self._cameras.get(camera_id)

    def remove(self, camera_id: str) -> Optional[DlsOnvifCameraEntry]:
        """Remove and return the entry for *camera_id*, or ``None``."""
        with self._lock:
            return self._cameras.pop(camera_id, None)

    def __contains__(self, camera_id: str) -> bool:
        """Check if *camera_id* is registered."""
        with self._lock:
            return camera_id in self._cameras

    def __len__(self) -> int:
        """Return the number of registered cameras."""
        with self._lock:
            return len(self._cameras)

    # ---- bulk queries ----

    def all_entries(self) -> list[DlsOnvifCameraEntry]:
        """Return a snapshot list of all registered entries."""
        with self._lock:
            return list(self._cameras.values())

    def camera_ids(self) -> set[str]:
        """Return the set of all registered camera IDs."""
        with self._lock:
            return set(self._cameras.keys())

    def streaming_cameras(self) -> list[DlsOnvifCameraEntry]:
        """Return entries with STREAMING status."""
        with self._lock:
            return [c for c in self._cameras.values() if c.is_streaming]

    def all_pipelines(self) -> list[DlsLaunchedPipeline]:
        """Return a flat list of all pipelines across all cameras."""
        with self._lock:
            return [p for entry in self._cameras.values() for p in entry.pipelines]

    # ---- lifecycle ----

    def stop_all(self) -> list[str]:
        """Stop every pipeline in every camera; return aggregated errors."""
        errors: list[str] = []
        with self._lock:
            for entry in self._cameras.values():
                errors.extend(entry.stop_all_pipelines())
            self._cameras.clear()
        return errors

    def summary(self) -> list[dict[str, Any]]:
        """Return a list of dicts for logging / debugging."""
        with self._lock:
            return [entry.to_dict() for entry in self._cameras.values()]
