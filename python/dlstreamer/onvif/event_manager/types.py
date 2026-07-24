# ==============================================================================
# Copyright (C) 2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================
"""
Data model for :mod:`dlstreamer.onvif.event_manager`.

Small dataclasses mirroring the subset of the ONVIF Events schema
exposed by the PullPoint engine. Keeps callers away from the raw zeep
objects unless they explicitly want them (:attr:`EventNotification.raw`).
"""
from __future__ import annotations

from dataclasses import dataclass, field
from typing import Any, Dict, Optional


_DEFAULT_TOPIC_DIALECT = (
    "http://www.onvif.org/ver10/tev/topicExpression/ConcreteSet"
)


@dataclass
class EventFilter:
    """Topic expression filter for ``CreatePullPointSubscription``.

    Attributes:
        expression: ONVIF topic expression, e.g. ``"tns1:VideoSource//."``
            (any topic under ``VideoSource``) or ``"tns1:RuleEngine//."``.
            Empty string means "no topic filter" (subscribe to all events).
        dialect: URI identifying the topic expression dialect.
            ONVIF requires ``ConcreteSet`` for wildcarded subtree
            expressions (the default).
    """

    expression: str = ""
    dialect: str = _DEFAULT_TOPIC_DIALECT


@dataclass
class EventNotification:
    """A single parsed ONVIF ``NotificationMessage``.

    Fields ``source`` and ``data`` flatten the corresponding ``SimpleItem``
    lists into ``{name: value}`` dictionaries because the ONVIF schema
    forbids repeated names inside those blocks. Complex items are ignored
    at parsing time — reach for :attr:`raw` if you need them.
    """

    topic: str = ""
    utc_time: str = ""
    property_operation: str = ""  # "Initialized" | "Changed" | "Deleted" | ""
    source: Dict[str, str] = field(default_factory=dict)
    data: Dict[str, str] = field(default_factory=dict)
    raw: Any = None

    def short(self) -> str:
        """Compact one-line rendering, useful for logs / test UIs."""
        src = ",".join(f"{k}={v}" for k, v in self.source.items()) or "-"
        dat = ",".join(f"{k}={v}" for k, v in self.data.items()) or "-"
        op = self.property_operation or "-"
        return (
            f"[{self.utc_time or '-'}] {self.topic or '-'} "
            f"op={op} source={src} data={dat}"
        )


@dataclass
class SupportedEventTopic:
    """One event type a camera advertises via ``GetEventProperties``.

    Beyond the topic path, this carries the ``MessageDescription`` schema so
    callers can see which fields a notification will contain (the answer to
    "what does ``MotionAlarm`` actually report?"). ``source`` and ``data``
    map each ``SimpleItemDescription`` name to its declared type, mirroring
    the ``{name: value}`` shape of :class:`EventNotification` (e.g.
    ``data={"IsMotion": "xsd:boolean"}``).

    Attributes:
        topic: Concrete ONVIF topic path, e.g.
            ``"tns1:RuleEngine/CellMotionDetector/Motion"``.
        is_property: ``MessageDescription/@IsProperty`` — True when the topic
            models a stateful property (Initialized/Changed/Deleted).
        source: ``{name: type}`` for the message ``Source`` items (the event
            key/identity fields).
        data: ``{name: type}`` for the message ``Data`` items (the payload,
            e.g. ``IsMotion``, ``State``).
    """

    topic: str = ""
    is_property: bool = False
    source: Dict[str, str] = field(default_factory=dict)
    data: Dict[str, str] = field(default_factory=dict)

    def short(self) -> str:
        """Compact one-line rendering, useful for logs / test UIs."""
        src = ",".join(f"{k}:{v}" for k, v in self.source.items()) or "-"
        dat = ",".join(f"{k}:{v}" for k, v in self.data.items()) or "-"
        kind = "property" if self.is_property else "event"
        return f"{self.topic} [{kind}] source={src} data={dat}"


@dataclass
class EventCapableCamera:
    """Camera descriptor confirmed to expose an ONVIF Events service.

    Emitted by :func:`~dlstreamer.onvif.event_manager.find_event_capable_cameras`
    and typically consumed by
    :class:`~dlstreamer.onvif.event_manager.engine.OnvifEventEngine`
    (see :meth:`OnvifEventEngine.from_capable_camera`).
    """

    hostname: str
    port: int
    error: Optional[str] = None

    @property
    def camera_id(self) -> str:
        """``"hostname:port"`` identifier used by higher layers."""
        return f"{self.hostname}:{self.port}"

    @property
    def ok(self) -> bool:
        """True when the events probe succeeded."""
        return self.error is None
