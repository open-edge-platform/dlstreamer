# ==============================================================================
# Copyright (C) 2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================
"""Data models for :mod:`dlstreamer.onvif.video_engine`."""

from __future__ import annotations

from dataclasses import dataclass, field
from datetime import datetime, timezone
from enum import Enum
from typing import Any, Dict, Optional, Sequence, Tuple


def _normalize_mac(mac: Optional[str]) -> Optional[str]:
    if mac is None:
        return None
    normalized = mac.strip().upper().replace("-", ":")
    return normalized or None


@dataclass(frozen=True)
class CameraIdentity:
    """Normalized camera key used for matching and state tracking."""

    hostname: str
    port: int
    mac: Optional[str] = None

    def key(self) -> str:
        """Return the stable string key identifying this camera."""
        normalized_mac = _normalize_mac(self.mac)
        if normalized_mac:
            return f"{self.hostname}:{self.port}:{normalized_mac}"
        return f"{self.hostname}:{self.port}"


@dataclass(frozen=True)
class PipelineBinding:
    """A configured pipeline associated with a camera identity."""

    camera: CameraIdentity
    pipeline: Sequence[str] | str
    binding_id: str
    events: Tuple[str, ...] = field(default_factory=tuple)
    profile_name: Optional[str] = None
    username: str = ""
    password: str = ""

    def to_dict(self) -> Dict[str, Any]:
        """Return a JSON-serializable representation of the binding."""
        return {
            "camera": {
                "hostname": self.camera.hostname,
                "port": self.camera.port,
                "mac": self.camera.mac,
            },
            "binding_id": self.binding_id,
            "pipeline": list(self.pipeline) if not isinstance(self.pipeline, str) else self.pipeline,
            "events": list(self.events),
            "profile_name": self.profile_name,
        }


@dataclass
class CameraRuntimeState:
    """Live state for a discovered camera."""

    camera: CameraIdentity
    status: str = "discovered"
    last_seen: datetime = field(default_factory=lambda: datetime.now(timezone.utc))
    pipeline_binding_ids: Tuple[str, ...] = field(default_factory=tuple)
    error: Optional[str] = None
    # ---- optional extensions used by auto-templates / auto-profile-fetch ----
    # Kept with defaults so existing constructors ``CameraRuntimeState(identity)``
    # continue to work unchanged.
    profiles_snapshot: Optional["CameraProfileSnapshot"] = None
    template_binding_ids: Tuple[str, ...] = field(default_factory=tuple)

    def touch(self) -> None:
        """Update :attr:`last_seen` to the current UTC time."""
        self.last_seen = datetime.now(timezone.utc)


@dataclass(frozen=True)
class VideoEngineEvent:
    """Callback payload emitted by the engine."""

    kind: str
    camera: CameraIdentity
    details: Dict[str, Any] = field(default_factory=dict)
    timestamp: datetime = field(default_factory=lambda: datetime.now(timezone.utc))


@dataclass(frozen=True)
class CameraEventDefinition:
    """ONVIF event definition attached to a camera."""

    name: str
    camera: CameraIdentity
    topic_filter: Optional[str] = None
    renew_every: Optional[float] = None
    metadata: Dict[str, Any] = field(default_factory=dict)


class PipelineAction(str, Enum):
    """Action applied to an analytics pipeline when an event rule matches."""

    ADD = "add"
    MODIFY = "modify"
    REMOVE = "remove"
    RESTART = "restart"


@dataclass(frozen=True)
class EventTrigger:
    """Condition matched against an incoming ONVIF notification.

    All populated fields must hold for the trigger to fire (logical AND).
    Unset fields (``None`` / empty) are ignored. Matching itself is the
    responsibility of the matcher, not of this pure data object.
    """

    topic_contains: Optional[str] = None
    property_operation: Optional[str] = None
    data_equals: Dict[str, str] = field(default_factory=dict)


@dataclass(frozen=True)
class EventRule:  # pylint: disable=too-many-instance-attributes
    """Bind an ONVIF event condition to a pipeline action.

    A rule ties a :class:`EventTrigger` on a given camera to a
    :class:`PipelineAction` targeting a specific pipeline binding.
    ``pipeline`` is required for :attr:`PipelineAction.ADD` and
    :attr:`PipelineAction.MODIFY`, and ignored otherwise.
    """

    name: str
    camera: CameraIdentity
    trigger: EventTrigger
    action: PipelineAction
    target_binding_id: str
    pipeline: Optional[Sequence[str] | str] = None
    profile_name: Optional[str] = None
    username: str = ""
    password: str = ""


def as_camera_identity(camera: Any) -> CameraIdentity:
    """Build a :class:`CameraIdentity` from a dict-like camera descriptor."""

    if isinstance(camera, CameraIdentity):
        return camera

    if not isinstance(camera, dict):
        raise TypeError("camera must be a mapping with hostname and port")

    hostname = camera.get("hostname") or camera.get("ip")
    port = camera.get("port")
    mac = camera.get("mac") or camera.get("mac_address")

    if not hostname or port is None:
        raise ValueError("camera descriptor requires hostname/ip and port")

    return CameraIdentity(str(hostname), int(port), _normalize_mac(mac))


def pipeline_list(value: Sequence[str] | str) -> Sequence[str] | str:
    """Return the pipeline as stored, after trivial normalization."""

    if isinstance(value, str):
        return value.strip()
    return tuple(str(item) for item in value)


# ---------------------------------------------------------------------------
# Auto-templates and auto-profile-fetch (opt-in additions).
#
# The types below are strictly additive; nothing in the existing runtime uses
# them unless the caller registers a :class:`PipelineTemplate` via the new
# public API or enables :meth:`VideoEngine.enable_auto_profile_fetch`.
# ---------------------------------------------------------------------------


def _norm_mac_str(mac: Optional[str]) -> str:
    normalized = _normalize_mac(mac)
    return normalized or ""


@dataclass(frozen=True)
class CameraMatcher:
    """Predicate describing which discovered cameras a template applies to.

    Every populated field must hold for the matcher to fire (logical AND).
    Unset fields (``None`` / empty) are ignored. Matching is pure and side-
    effect free.

    ``hostname`` / ``port`` / ``mac`` match a single identity.
    ``mac_prefix`` matches any camera whose (normalized) MAC starts with the
    given prefix (case-insensitive, separators collapsed to ``:``).
    ``subnet`` matches any hostname/IP that lies inside the given CIDR (e.g.
    ``"10.91.106.0/24"``); non-IP hostnames never match.
    """

    hostname: Optional[str] = None
    port: Optional[int] = None
    mac: Optional[str] = None
    mac_prefix: Optional[str] = None
    subnet: Optional[str] = None

    @staticmethod
    def _ip_in_subnet(hostname: str, subnet: str) -> bool:
        # Local import so the ipaddress dependency is only paid when subnets
        # are actually used.
        import ipaddress  # pylint: disable=import-outside-toplevel

        try:
            addr = ipaddress.ip_address(hostname)
        except ValueError:
            return False
        try:
            network = ipaddress.ip_network(subnet, strict=False)
        except ValueError:
            return False
        return addr in network

    def matches(self, identity: CameraIdentity) -> bool:
        """Return True when *identity* satisfies every populated field."""
        if self.hostname is not None and self.hostname != identity.hostname:
            return False
        if self.port is not None and int(self.port) != int(identity.port):
            return False

        cam_mac = _norm_mac_str(identity.mac)
        if self.mac is not None:
            if not cam_mac or _norm_mac_str(self.mac) != cam_mac:
                return False
        if self.mac_prefix is not None:
            wanted = _norm_mac_str(self.mac_prefix)
            if not wanted or not cam_mac.startswith(wanted):
                return False
        if self.subnet is not None and not self._ip_in_subnet(identity.hostname, self.subnet):
            return False
        return True


@dataclass(frozen=True)
class CameraProfileSnapshot:
    """Cached ONVIF media profiles for one camera.

    ``profiles`` uses ``Any`` typing to avoid importing the
    :mod:`camera_profiles` package here (keeps the type layer lightweight).
    """

    identity: CameraIdentity
    profiles: Tuple[Any, ...] = field(default_factory=tuple)
    fetched_at: datetime = field(default_factory=lambda: datetime.now(timezone.utc))
    mac_address: str = ""
    error: Optional[str] = None

    @property
    def ok(self) -> bool:
        """Return True when the profile fetch completed without error."""
        return self.error is None


@dataclass(frozen=True)
class PipelineTemplate:  # pylint: disable=too-many-instance-attributes
    """Recipe used to auto-instantiate a :class:`PipelineBinding`.

    A template is applied by the engine every time a discovered camera
    satisfies :attr:`matcher`. The rendered pipeline supports the following
    placeholders (all optional): ``{rtsp_url}``, ``{hostname}``, ``{port}``,
    ``{mac}``, ``{profile_name}``.

    :attr:`profile_selector` chooses which media profile to bind against:

    - ``"first"`` (default) — first profile reported by the camera.
    - ``"name=<value>"``    — profile whose ``name`` equals ``<value>``.
    """

    template_id: str
    pipeline: Sequence[str] | str
    matcher: CameraMatcher = field(default_factory=CameraMatcher)
    profile_selector: str = "first"
    events: Tuple[str, ...] = field(default_factory=tuple)
    username: str = ""
    password: str = ""
    auto_start: bool = True

    def to_dict(self) -> Dict[str, Any]:
        """Return a JSON-serializable representation of the template."""
        return {
            "template_id": self.template_id,
            "pipeline": (
                list(self.pipeline) if not isinstance(self.pipeline, str) else self.pipeline
            ),
            "matcher": {
                "hostname": self.matcher.hostname,
                "port": self.matcher.port,
                "mac": self.matcher.mac,
                "mac_prefix": self.matcher.mac_prefix,
                "subnet": self.matcher.subnet,
            },
            "profile_selector": self.profile_selector,
            "events": list(self.events),
            "auto_start": self.auto_start,
        }


def as_camera_matcher(payload: Any) -> CameraMatcher:
    """Build a :class:`CameraMatcher` from a dict payload."""

    if isinstance(payload, CameraMatcher):
        return payload
    if payload is None:
        return CameraMatcher()
    if not isinstance(payload, dict):
        raise TypeError("matcher payload must be a mapping")
    port = payload.get("port")
    return CameraMatcher(
        hostname=payload.get("hostname"),
        port=int(port) if port is not None else None,
        mac=payload.get("mac"),
        mac_prefix=payload.get("mac_prefix"),
        subnet=payload.get("subnet"),
    )


def as_pipeline_template(payload: Any) -> PipelineTemplate:
    """Build a :class:`PipelineTemplate` from a dict payload."""

    if isinstance(payload, PipelineTemplate):
        return payload
    if not isinstance(payload, dict):
        raise TypeError("template payload must be a mapping")

    template_id = payload.get("template_id") or payload.get("id")
    if not template_id:
        raise ValueError("pipeline template requires 'template_id'")
    pipeline = payload.get("pipeline")
    if pipeline is None:
        raise ValueError(f"pipeline template '{template_id}' requires 'pipeline'")

    return PipelineTemplate(
        template_id=str(template_id),
        pipeline=pipeline_list(pipeline),
        matcher=as_camera_matcher(payload.get("matcher")),
        profile_selector=str(payload.get("profile_selector", "first")),
        events=tuple(str(item) for item in (payload.get("events") or ())),
        username=str(payload.get("username", "")),
        password=str(payload.get("password", "")),
        auto_start=bool(payload.get("auto_start", True)),
    )


def select_profile(
    profiles: Sequence[Any],
    selector: str,
) -> Optional[Any]:
    """Pick a profile from a list according to a selector string.

    Supports ``"first"`` and ``"name=<value>"``. Unknown selectors fall back
    to the first profile so the runtime stays forgiving of typos.
    """

    if not profiles:
        return None
    sel = (selector or "first").strip()
    if sel.startswith("name="):
        wanted = sel[len("name="):]
        for profile in profiles:
            if str(getattr(profile, "name", "")) == wanted:
                return profile
        return None
    return profiles[0]
