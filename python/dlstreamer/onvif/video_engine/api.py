# ==============================================================================
# Copyright (C) 2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================
"""Public API for :mod:`dlstreamer.onvif.video_engine`."""

# pylint: disable=duplicate-code

from __future__ import annotations

from pathlib import Path
from typing import Any, Callable, Iterable, Sequence

from .engine import VideoEngine
from .types import (
    CameraEventDefinition,
    CameraIdentity,
    CameraMatcher,
    CameraProfileSnapshot,
    CameraRuntimeState,
    EventRule,
    EventTrigger,
    PipelineAction,
    PipelineBinding,
    PipelineTemplate,
    VideoEngineEvent,
    as_pipeline_template,
)
from .dynamic import (
    CameraEventWorker,
    DynamicPipelineController,
    EventRuleMatcher,
    EventRuleParser,
    PipelineActionExecutor,
)
from ..camera_profiles import (
    ONVIFProfile,
    CameraProfilesResult,
    read_camera_profiles,
    read_camera_profiles_async,
)
from ..ptz import (
    PTZVector,
    PTZStatus,
    PTZPreset,
    PTZCapableProfile,
    is_ptz_profile,
    find_ptz_capable_profiles,
    find_ptz_capable_profiles_async,
    PTZController,
)
from ..event_manager import (
    EventCapableCamera,
    EventFilter,
    EventNotification,
    SupportedEventTopic,
    is_event_capable,
    find_event_capable_cameras,
    find_event_capable_cameras_async,
    get_supported_event_topics,
    pull_events_once,
    stream_events,
)

_DEFAULT_ENGINE = VideoEngine()


def create_video_engine(
    config_path: str | Path | None = None,
    *,
    discovery_time: int = 60,
    timeout: int = 120,
    verbose: bool = False,
) -> VideoEngine:
    """Create a new, isolated :class:`VideoEngine` instance."""
    return VideoEngine(
        config_path=config_path,
        discovery_time=discovery_time,
        timeout=timeout,
        verbose=verbose,
    )


def get_video_engine() -> VideoEngine:
    """Return the shared default :class:`VideoEngine` instance."""
    return _DEFAULT_ENGINE


def start_video_engine() -> None:
    """Start the shared engine's discovery loop and reaper."""
    _DEFAULT_ENGINE.start()


def stop_video_engine() -> None:
    """Stop the shared engine and all its running pipelines."""
    _DEFAULT_ENGINE.stop()


def destroy_video_engine() -> None:
    """Stop the shared engine and clear all of its state."""
    _DEFAULT_ENGINE.destroy()


def discovery_start() -> None:
    """Start ONVIF discovery on the shared engine."""
    _DEFAULT_ENGINE.discovery_start()


def discovery_stop() -> None:
    """Stop ONVIF discovery on the shared engine."""
    _DEFAULT_ENGINE.discovery_stop()


# pylint: disable=invalid-name
# camelCase kept for backward-compatible public API (see PUBLIC_API.md).
def setTimeout(sec: int) -> None:
    """Set the camera-liveness timeout on the shared engine."""
    _DEFAULT_ENGINE.setTimeout(sec)


def getTimeout() -> int:
    """Return the camera-liveness timeout of the shared engine."""
    return _DEFAULT_ENGINE.getTimeout()


def setDiscoveryTime(sec: int) -> None:
    """Set the discovery interval on the shared engine."""
    _DEFAULT_ENGINE.setDiscoveryTime(sec)


def getDiscoveryTime() -> int:
    """Return the discovery interval of the shared engine."""
    return _DEFAULT_ENGINE.getDiscoveryTime()
# pylint: enable=invalid-name


def load_config(config_path: str | Path, *, pipeline_library: dict[str, Any] | None = None) -> None:
    """Load pipeline bindings/templates into the shared engine."""
    _DEFAULT_ENGINE.load_config(config_path, pipeline_library=pipeline_library)


def save_config() -> None:
    """Persist the shared engine's bindings to its configured path."""
    _DEFAULT_ENGINE.save_config()


def list_camera_pipeline_pairs() -> list[dict[str, Any]]:
    """Return all camera/pipeline bindings from the shared engine."""
    return _DEFAULT_ENGINE.list_camera_pipeline_pairs()


def get_pipeline_for_camera(hostname: str, port: int, mac: str | None = None) -> list[dict[str, Any]]:
    """Return the shared engine's bindings matching a camera identity."""
    return _DEFAULT_ENGINE.get_pipeline_for_camera(hostname, port, mac)


def set_camera_pipeline(  # pylint: disable=too-many-arguments
    hostname: str,
    port: int,
    pipeline: Sequence[str] | str,
    *,
    mac: str | None = None,
    binding_id: str | None = None,
    events: Iterable[str] | None = None,
    persist: bool = False,
) -> PipelineBinding:
    """Register a pipeline binding for a camera on the shared engine."""
    return _DEFAULT_ENGINE.set_camera_pipeline(
        hostname,
        port,
        pipeline,
        mac=mac,
        binding_id=binding_id,
        events=events,
        persist=persist,
    )


def set_camera_event(  # pylint: disable=too-many-arguments
    hostname: str,
    port: int,
    event_name: str,
    *,
    mac: str | None = None,
    topic_filter: str | None = None,
    renew_every: float | None = None,
    **metadata: Any,
) -> CameraEventDefinition:
    """Attach an ONVIF event definition to a camera on the shared engine."""
    return _DEFAULT_ENGINE.set_camera_event(
        hostname,
        port,
        event_name,
        mac=mac,
        topic_filter=topic_filter,
        renew_every=renew_every,
        **metadata,
    )


def list_camera_events(hostname: str, port: int, mac: str | None = None) -> list[dict[str, Any]]:
    """Return the event definitions registered for a camera."""
    return _DEFAULT_ENGINE.list_camera_events(hostname, port, mac)


def register_callback(callback: Callable[[VideoEngineEvent], None]) -> None:
    """Register an engine event callback on the shared engine."""
    _DEFAULT_ENGINE.register_callback(callback)


def unregister_callback(callback: Callable[[VideoEngineEvent], None]) -> None:
    """Remove a previously registered engine event callback."""
    _DEFAULT_ENGINE.unregister_callback(callback)


def get_active_cameras() -> list[dict[str, Any]]:
    """Return runtime state for cameras discovered by the shared engine."""
    return _DEFAULT_ENGINE.get_active_cameras()


def get_active_pipelines() -> list[dict[str, Any]]:
    """Return the running pipelines managed by the shared engine."""
    return _DEFAULT_ENGINE.get_active_pipelines()


# ---------------------------------------------------------------------------
# Auto-templates and auto-profile-fetch (opt-in public API)
# ---------------------------------------------------------------------------


def enable_auto_profile_fetch(
    enabled: bool = True,
    *,
    ttl_seconds: float = 300.0,
) -> None:
    """Toggle automatic ONVIF profile fetch on the shared engine."""
    _DEFAULT_ENGINE.enable_auto_profile_fetch(enabled, ttl_seconds=ttl_seconds)


def add_pipeline_template(template: PipelineTemplate | dict[str, Any]) -> PipelineTemplate:
    """Register a pipeline template on the shared engine.

    ``template`` may be a :class:`PipelineTemplate` instance or a plain
    mapping (parsed via :func:`as_pipeline_template`).
    """
    if isinstance(template, dict):
        template = as_pipeline_template(template)
    return _DEFAULT_ENGINE.add_pipeline_template(template)


def remove_pipeline_template(template_id: str) -> None:
    """Unregister a pipeline template from the shared engine."""
    _DEFAULT_ENGINE.remove_pipeline_template(template_id)


def list_pipeline_templates() -> list[dict[str, Any]]:
    """Return the pipeline templates registered on the shared engine."""
    return _DEFAULT_ENGINE.list_pipeline_templates()


def create_dynamic_controller(
    engine: VideoEngine | None = None,
    *,
    verbose: bool = False,
) -> DynamicPipelineController:
    """Create a controller wiring ONVIF events to pipeline changes.

    Defaults to the shared engine instance when ``engine`` is omitted.
    """
    return DynamicPipelineController(engine or _DEFAULT_ENGINE, verbose=verbose)


__all__ = [
    # ===== Model classes =====
    "CameraEventDefinition",
    "CameraIdentity",
    "CameraMatcher",
    "CameraProfileSnapshot",
    "CameraRuntimeState",
    "EventRule",
    "EventTrigger",
    "PipelineAction",
    "PipelineBinding",
    "PipelineTemplate",
    "VideoEngine",
    "VideoEngineEvent",

    # ===== Factory and lifecycle =====
    "create_video_engine",
    "get_video_engine",
    "start_video_engine",
    "stop_video_engine",
    "destroy_video_engine",

    # ===== Discovery =====
    "discovery_start",
    "discovery_stop",

    # ===== Configuration =====
    "setTimeout",
    "getTimeout",
    "setDiscoveryTime",
    "getDiscoveryTime",
    "load_config",
    "save_config",

    # ===== Camera pipeline management =====
    "list_camera_pipeline_pairs",
    "get_pipeline_for_camera",
    "set_camera_pipeline",

    # ===== Camera events =====
    "set_camera_event",
    "list_camera_events",

    # ===== Callbacks =====
    "register_callback",
    "unregister_callback",

    # ===== State query =====
    "get_active_cameras",
    "get_active_pipelines",

    # ===== Auto-templates / auto-profile-fetch =====
    "enable_auto_profile_fetch",
    "add_pipeline_template",
    "remove_pipeline_template",
    "list_pipeline_templates",
    "as_pipeline_template",

    # ===== Dynamic (event-driven) pipeline control =====
    "DynamicPipelineController",
    "EventRuleMatcher",
    "EventRuleParser",
    "PipelineActionExecutor",
    "CameraEventWorker",
    "create_dynamic_controller",

    # ===== Camera profiles library =====
    "ONVIFProfile",
    "CameraProfilesResult",
    "read_camera_profiles",
    "read_camera_profiles_async",

    # ===== PTZ library =====
    "PTZVector",
    "PTZStatus",
    "PTZPreset",
    "PTZCapableProfile",
    "is_ptz_profile",
    "find_ptz_capable_profiles",
    "find_ptz_capable_profiles_async",
    "PTZController",

    # ===== Event manager library =====
    "EventCapableCamera",
    "EventFilter",
    "EventNotification",
    "SupportedEventTopic",
    "is_event_capable",
    "find_event_capable_cameras",
    "find_event_capable_cameras_async",
    "get_supported_event_topics",
    "pull_events_once",
    "stream_events",
]
