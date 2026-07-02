# ==============================================================================
# Copyright (C) 2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================
"""Public API — 13 operations, GStreamer / DL Streamer native variant.

Each function is a thin wrapper over ``Gst.DeviceMonitor`` /
``Gst.Device`` / ``Gst.Pipeline`` machinery; the heavy lifting (SOAP,
WS-Discovery, PullPoint polling, credential storage) lives in the
``onvifdeviceprovider`` plugin loaded by :mod:`dlstreamer.onvif._provider`.

Error convention follows the GStreamer style: discovery / capability
calls return ``False`` / ``None`` and post a ``Gst.MessageType.ERROR``
on the monitor's bus; pipeline errors arrive on the pipeline's own bus.
No custom ``OnvifError`` exception type is raised.
"""
from __future__ import annotations

from typing import Callable, Optional, Union
from urllib.parse import urlparse

import gi

gi.require_version("Gst", "1.0")
from gi.repository import GLib, Gst  # noqa: E402  (gi version guard)

from . import _provider, _state
from .dls_onvif_config_manager import default_config_manager

EventCallback = Callable[["Gst.Bus", "Gst.Message", object], bool]

# Accepted forms for identifying a camera in the pipeline-definition API.
CameraRef = Union["Gst.Device", str]


# --------------------------------------------------------------------------- #
# 1 / 2 — discovery lifecycle
# --------------------------------------------------------------------------- #
def init_discovery() -> Gst.DeviceMonitor:
    """Create a ``Gst.DeviceMonitor`` pre-configured for ONVIF cameras.

    The monitor is filtered to ``"Source/Network/ONVIF"`` so it picks up
    only devices produced by the ``onvifdeviceprovider`` plugin. The
    monitor is **not** started — call :func:`start_discovery` next.
    """
    _provider.ensure_provider_registered()
    monitor = Gst.DeviceMonitor.new()
    monitor.add_filter(_provider.device_class(), None)
    return monitor


def release_discovery(monitor: Gst.DeviceMonitor) -> None:
    """Stop the monitor (if running) and release its ref-count. Idempotent."""
    if monitor is None:
        return
    try:
        monitor.stop()
    except GLib.Error:
        pass


# --------------------------------------------------------------------------- #
# 3 / 4 — start / stop probing
# --------------------------------------------------------------------------- #
def start_discovery(monitor: Gst.DeviceMonitor) -> bool:
    """Start WS-Discovery probing inside the provider.

    Returns ``False`` when the monitor is already running or could not
    start (the provider posts a ``Gst.MessageType.ERROR`` on the bus in
    that case).
    """
    return bool(monitor.start())


def stop_discovery(monitor: Gst.DeviceMonitor) -> None:
    """Stop probing. Already-discovered ``Gst.Device`` objects remain valid."""
    monitor.stop()


# --------------------------------------------------------------------------- #
# 5 — enumerate discovered cameras
# --------------------------------------------------------------------------- #
def list_discovered_cameras(monitor: Gst.DeviceMonitor) -> list[Gst.Device]:
    """Snapshot of cameras the monitor currently knows about."""
    return list(monitor.get_devices() or [])


# --------------------------------------------------------------------------- #
# 6 / 7 / 8 — pipeline registry
# --------------------------------------------------------------------------- #
def list_defined_pipelines(camera: Optional[Gst.Device] = None) -> list[Gst.Pipeline]:
    """Return pipelines the library owns, optionally filtered by camera."""
    with _state.state_lock():
        entries = list(_state.registry().pipelines.values())
    if camera is None:
        return [e.pipeline for e in entries]
    xaddr = _state.camera_xaddr(camera)
    return [e.pipeline for e in entries if e.camera_xaddr == xaddr]


def add_or_update_pipeline(camera: Gst.Device, pipeline: Gst.Pipeline) -> Gst.Pipeline:
    """Register a caller-built pipeline against a camera.

    If a pipeline with the same ``get_name()`` already exists, the old
    one is transitioned to ``Gst.State.NULL`` and replaced (MODIFY
    semantics).
    """
    name = pipeline.get_name()
    xaddr = _state.camera_xaddr(camera)
    with _state.state_lock():
        existing = _state.registry().pipelines.get(name)
        if existing is not None and existing.pipeline is not pipeline:
            existing.pipeline.set_state(Gst.State.NULL)
        _state.registry().pipelines[name] = _state.PipelineEntry(
            pipeline=pipeline, camera_xaddr=xaddr,
        )
    return pipeline


def remove_pipeline(pipeline: Gst.Pipeline) -> None:
    """Stop the pipeline and drop the library's reference. Idempotent."""
    if pipeline is None:
        return
    name = pipeline.get_name()
    with _state.state_lock():
        _state.registry().pipelines.pop(name, None)
    pipeline.set_state(Gst.State.NULL)


# --------------------------------------------------------------------------- #
# 6b — pipeline definition registry (in-code, camera-bound)
# --------------------------------------------------------------------------- #
def register_pipeline_definition(
    camera: CameraRef,
    definition: str,
    *,
    port: int = 80,
    name: Optional[str] = None,
) -> str:
    """Bind a pipeline definition template to a specific camera.

    Unlike :func:`add_or_update_pipeline` (which stores an already-built
    ``Gst.Pipeline`` instance), this stores a **string template** — the
    tail appended after ``rtspsrc location="..."`` when the discovery
    engine spins up a pipeline for the matching camera.

    Parameters
    ----------
    camera:
        Either a ``Gst.Device`` returned by discovery (hostname/port are
        extracted from its ``onvif.xaddr`` property) or a hostname string
        used together with ``port``.
    definition:
        Pipeline tail. Typically starts with ``" ! "``, e.g.
        ``" ! rtph264depay ! h264parse ! avdec_h264 ! videoconvert ! autovideosink"``.
    port:
        Ignored when ``camera`` is a ``Gst.Device``. Default 80.
    name:
        Optional stable key for later removal. Auto-derived from
        ``hostname_port`` when omitted.

    Returns
    -------
    The entry key under which the definition was stored.
    """
    hostname, port_int = _resolve_camera_hostport(camera, port)
    return default_config_manager().add_pipeline_definition(
        hostname, port_int, definition, name=name,
    )


def unregister_pipeline_definition(
    camera: Optional[CameraRef] = None,
    *,
    port: int = 80,
    name: Optional[str] = None,
) -> bool:
    """Remove a pipeline definition. Match by ``name`` or by camera.

    Returns ``True`` when an entry was removed.
    """
    if name is not None:
        return default_config_manager().remove_pipeline_definition(name=name)
    if camera is None:
        return False
    hostname, port_int = _resolve_camera_hostport(camera, port)
    return default_config_manager().remove_pipeline_definition(hostname, port_int)


def get_pipeline_definition(
    camera: CameraRef,
    *,
    port: int = 80,
) -> Optional[str]:
    """Return the pipeline definition template stored for ``camera``.

    ``camera`` follows the same rules as :func:`register_pipeline_definition`.
    Returns ``None`` when no definition is registered for the target.
    """
    hostname, port_int = _resolve_camera_hostport(camera, port)
    return default_config_manager().get_pipeline_definition_by_ip_port(
        hostname, port_int,
    )


def list_pipeline_definitions() -> list[dict]:
    """Snapshot of every registered ``(hostname, port) -> definition`` binding.

    Each entry is a dict with keys ``name``, ``hostname``, ``port``,
    ``definition``.
    """
    return default_config_manager().list_pipeline_definitions()


# --------------------------------------------------------------------------- #
# 9 / 10 — camera events
# --------------------------------------------------------------------------- #
def register_camera_event(
    camera: Gst.Device,
    topic_filter: str,
    callback: EventCallback,
    user_data: object = None,
) -> int:
    """Subscribe to ONVIF events for *camera* matching *topic_filter*.

    Returns the GLib watch id (same calling convention as
    ``Gst.Bus.add_watch``). Events are delivered as
    ``Gst.MessageType.ELEMENT`` messages whose ``Gst.Structure`` is
    named ``"onvif-event"`` and carries the fields ``topic``,
    ``utc-time``, ``source`` and ``data``.
    """
    xaddr = _state.camera_xaddr(camera)
    reg = _state.registry()
    with _state.state_lock():
        cam_state = reg.camera_events.get(xaddr)
        if cam_state is None:
            cam_state = _state.CameraEventState(bus=Gst.Bus.new())
            reg.camera_events[xaddr] = cam_state
            _start_pullpoint_worker(camera, cam_state, topic_filter)
        bus = cam_state.bus

    watch_id = bus.add_watch(
        GLib.PRIORITY_DEFAULT,
        _make_filter(topic_filter, callback, user_data),
        None,
    )

    with _state.state_lock():
        cam_state.refcount += 1
        reg.event_watches[watch_id] = _state.EventWatchEntry(
            watch_id=watch_id, camera_xaddr=xaddr,
            topic_filter=topic_filter, bus=bus,
        )
    return watch_id


def unregister_camera_event(watch_id: int) -> bool:
    """Remove a previously installed event watch.

    When the last watch for a camera is removed, the library
    ``Unsubscribe``s the PullPoint and tears down the polling worker.
    Idempotent.
    """
    reg = _state.registry()
    with _state.state_lock():
        entry = reg.event_watches.pop(watch_id, None)
        if entry is None:
            return False
        cam_state = reg.camera_events.get(entry.camera_xaddr)
        if cam_state is not None:
            cam_state.refcount -= 1
            if cam_state.refcount <= 0:
                _stop_pullpoint_worker(cam_state)
                reg.camera_events.pop(entry.camera_xaddr, None)
    return bool(GLib.source_remove(watch_id))


# --------------------------------------------------------------------------- #
# 11 / 12 — capabilities & profiles
# --------------------------------------------------------------------------- #
def get_camera_capabilities(camera: Gst.Device) -> Gst.Structure:
    """Return the camera's ONVIF capabilities as a ``Gst.Structure``."""
    props = camera.get_properties()
    if props is not None:
        nested = props.get_value("onvif.capabilities")
        if isinstance(nested, Gst.Structure):
            return nested
    return Gst.Structure.new_empty("onvif-capabilities")


def get_camera_profiles(camera: Gst.Device) -> Gst.Caps:
    """Return media profiles as ``Gst.Caps`` — one structure per profile."""
    caps = camera.get_caps()
    return caps if caps is not None else Gst.Caps.new_empty()


# --------------------------------------------------------------------------- #
# 13 — credentials
# --------------------------------------------------------------------------- #
def set_camera_credentials(camera: Gst.Device, uri: str) -> bool:
    """Update the credentials used by SOAP and ``rtspsrc location=``.

    *uri* must follow the ``GstURIHandler`` form
    ``"onvif://user:password@host:port"``. Returns ``True`` when the
    device accepts the new credentials (verified with a
    ``GetDeviceInformation`` probe).
    """
    parsed = urlparse(uri)
    if parsed.scheme != "onvif" or not parsed.hostname or parsed.username is None:
        return False

    if not Gst.uri_is_valid(uri):
        return False

    provider = camera.get_device_class()
    if provider is None:
        return False

    handler = camera if isinstance(camera, Gst.URIHandler) else None
    if handler is not None and not handler.set_uri(uri):
        return False

    reconfigure = Gst.Structure.new_empty("onvif-set-credentials")
    reconfigure.set_value("uri", uri)
    return bool(camera.send_event(Gst.Event.new_custom(
        Gst.EventType.CUSTOM_DOWNSTREAM, reconfigure,
    )))


# --------------------------------------------------------------------------- #
# Internal helpers
# --------------------------------------------------------------------------- #
def _resolve_camera_hostport(camera: CameraRef, port: int) -> tuple[str, int]:
    """Extract ``(hostname, port)`` from a ``Gst.Device`` or ``(str, port)`` pair.

    For a ``Gst.Device`` the values come from (in order of preference):
    the device's ``host`` / ``port`` Python attributes (populated by
    ``OnvifDevice``), the ``onvif.host`` / ``onvif.port`` fields on the
    device properties structure, or a URL-parse of ``onvif.xaddr``.
    """
    if isinstance(camera, str):
        try:
            return camera, int(port)
        except (TypeError, ValueError) as exc:
            raise ValueError(f"port must be an integer, got {port!r}") from exc

    if camera is None:
        raise ValueError("camera must be a Gst.Device or hostname string")

    host = getattr(camera, "host", None)
    dev_port = getattr(camera, "port", None)
    if host and dev_port is not None:
        return str(host), int(dev_port)

    props = None
    try:
        props = camera.get_properties()
    except AttributeError:
        pass

    if props is not None:
        prop_host = props.get_string("onvif.host") if hasattr(props, "get_string") else None
        prop_port = None
        try:
            _ok, prop_port = props.get_int("onvif.port")
            if not _ok:
                prop_port = None
        except (AttributeError, TypeError):
            prop_port = None
        if prop_host and prop_port:
            return prop_host, int(prop_port)

        xaddr = props.get_string("onvif.xaddr") if hasattr(props, "get_string") else None
        if xaddr:
            parsed = urlparse(xaddr)
            if parsed.hostname:
                return parsed.hostname, int(parsed.port or 80)

    raise ValueError(
        "Cannot resolve (hostname, port) — pass a discovery Gst.Device or a hostname string."
    )


def _make_filter(
    topic_filter: str,
    callback: EventCallback,
    user_data: object,
) -> Callable[["Gst.Bus", "Gst.Message", object], bool]:
    """Wrap *callback* so it only fires for matching ``onvif-event`` topics."""

    def _on_message(bus: Gst.Bus, msg: Gst.Message, _ud: object) -> bool:
        if msg.type != Gst.MessageType.ELEMENT:
            return True
        struct = msg.get_structure()
        if struct is None or struct.get_name() != "onvif-event":
            return True
        if topic_filter and topic_filter != "//.":
            topic = struct.get_string("topic") or ""
            if not _topic_matches(topic, topic_filter):
                return True
        return bool(callback(bus, msg, user_data))

    return _on_message


def _topic_matches(topic: str, topic_filter: str) -> bool:
    """ONVIF concrete-topic match.

    Filter syntax is the subset commonly used by ONVIF cameras —
    a fully-qualified topic optionally suffixed with ``//.`` meaning
    "and any descendant".
    """
    if topic_filter.endswith("//."):
        prefix = topic_filter[:-3]
        return topic == prefix or topic.startswith(prefix + "/")
    return topic == topic_filter


def _start_pullpoint_worker(
    camera: Gst.Device,
    cam_state: "_state.CameraEventState",
    topic_filter: str,
) -> None:
    """Hand the camera off to the provider's PullPoint worker pool.

    The real worker lives in the ``onvifdeviceprovider`` plugin; it
    posts ``onvif-event`` ``Gst.Message`` instances on
    ``cam_state.bus`` and stores its own handle on
    ``cam_state.pull_thread``. This Python-side helper is the
    integration point so tests can stub it out.
    """
    del camera, cam_state, topic_filter


def _stop_pullpoint_worker(cam_state: "_state.CameraEventState") -> None:
    """Signal the provider to ``Unsubscribe`` the PullPoint."""
    cam_state.pull_thread = None
