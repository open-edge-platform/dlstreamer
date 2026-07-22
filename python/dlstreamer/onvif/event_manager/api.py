# ==============================================================================
# Copyright (C) 2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================
"""
dlstreamer.onvif.event_manager — ONVIF Events (PullPoint) engine.

Implements a Python client for the ONVIF Events Service
(http://www.onvif.org/ver10/events/wsdl/event.wsdl) using the
**PullPoint** subscription pattern. Callers do not need to open an
inbound HTTP port — notifications are fetched with long-polling
``PullMessages`` calls, so the engine works behind NAT and firewalls.

Public API:

Data model:
    EventNotification      — parsed NotificationMessage (topic, source,
                             data, timestamp, PropertyOperation)
    EventFilter            — topic expression filter for subscribe()
    EventCapableCamera     — camera descriptor exposing an Events service
    SupportedEventTopic    — advertised event type + its Source/Data schema

Capability discovery (built on :mod:`~dlstreamer.onvif.camera_profiles`
and :mod:`~dlstreamer.onvif.discovery`):
    is_event_capable()
    find_event_capable_cameras()         — sync generator
    find_event_capable_cameras_async()   — async generator

Supported event introspection:
    get_supported_event_topics()         — list the event types a camera
                                           advertises via GetEventProperties,
                                           with their Source/Data field schema

Simple event reading:
    pull_events_once()                   — subscribe, pull one batch, close
    stream_events()                      — async event stream with auto-close

Advanced control:
    :mod:`dlstreamer.onvif.event_manager.engine` provides
    ``OnvifEventEngine`` for callers that need full PullPoint
    subscription lifecycle control.
"""

# pylint: disable=duplicate-code

from __future__ import annotations

import asyncio
from typing import AsyncIterator, Optional

from .types import (
    EventCapableCamera,
    EventFilter,
    EventNotification,
    SupportedEventTopic,
)
from .capabilities import (
    find_event_capable_cameras,
    find_event_capable_cameras_async,
    is_event_capable,
)
from .engine import EventCallback, OnvifEventEngine


def get_supported_event_topics(
    hostname: str,
    port: int,
    username: str = "",
    password: str = "",
    verbose: bool = False,
) -> list[SupportedEventTopic]:
    """List the event types a camera advertises via ``GetEventProperties``.

    Opens a short-lived engine, reads the camera's ONVIF ``TopicSet`` and
    returns a :class:`SupportedEventTopic` per event type. Each carries the
    topic path (e.g. ``"tns1:RuleEngine/CellMotionDetector/Motion"``) plus its
    ``MessageDescription`` schema — the Source/Data field names and types the
    notification will contain (e.g. ``data={"IsMotion": "xsd:boolean"}``). No
    subscription is created. Use ``t.topic`` as an :class:`EventFilter`
    expression. Returns an empty list when the camera exposes no topics.
    """
    engine = OnvifEventEngine(hostname, port, username, password, verbose)
    try:
        return engine.get_supported_event_topics()
    finally:
        engine.close()


def pull_events_once(
    hostname: str,
    port: int,
    username: str = "",
    password: str = "",
    verbose: bool = False,
    timeout: str = "PT30S",
    limit: int = 100,
    termination_time: str = "PT1H",
    topic_filter: Optional[EventFilter] = None,
) -> list[EventNotification]:
    """Read one event batch with automatic subscription lifecycle.

    This is the simplest blocking API for events:
    create subscription -> pull once -> close subscription.
    """
    engine = OnvifEventEngine(hostname, port, username, password, verbose)
    try:
        engine.subscribe(termination_time=termination_time, topic_filter=topic_filter)
        return engine.pull(timeout=timeout, limit=limit)
    finally:
        engine.close()


async def stream_events(
    hostname: str,
    port: int,
    username: str = "",
    password: str = "",
    verbose: bool = False,
    timeout: str = "PT30S",
    limit: int = 100,
    termination_time: str = "PT1H",
    topic_filter: Optional[EventFilter] = None,
    renew_every: Optional[float] = 300.0,
) -> AsyncIterator[EventNotification]:
    """Yield events continuously with automatic subscribe/unsubscribe.

    This function hides engine lifecycle details and is the preferred
    async API for long-running event consumption.
    """
    engine = OnvifEventEngine(hostname, port, username, password, verbose)
    try:
        await asyncio.to_thread(
            engine.subscribe,
            termination_time,
            topic_filter,
        )
        async for note in engine.stream(
            timeout=timeout,
            limit=limit,
            renew_every=renew_every,
        ):
            yield note
    finally:
        await asyncio.to_thread(engine.close)


def print_event_type(note: EventNotification) -> None:
    """Default event callback: print the event *type* (its ONVIF topic).

    The ONVIF ``topic`` fully identifies the event type, e.g.
    ``tns1:RuleEngine/CellMotionDetector/Motion`` or
    ``tns1:VideoSource/MotionAlarm``. Falls back to ``"<unknown>"`` when a
    camera omits the topic.
    """
    print(f"{{EVENT}} [event] type: {note.topic or '<unknown>'}")


async def watch_events(
    hostname: str,
    port: int,
    username: str = "",
    password: str = "",
    callback: EventCallback = print_event_type,
    verbose: bool = False,
    timeout: str = "PT30S",
    limit: int = 100,
    termination_time: str = "PT1H",
    topic_filter: Optional[EventFilter] = None,
    renew_every: Optional[float] = 300.0,
) -> None:
    """Subscribe and invoke ``callback`` for every new camera event.

    Handles the full subscription lifecycle (subscribe -> stream ->
    close) and calls ``callback`` once per :class:`EventNotification` as
    events arrive. Defaults to :func:`print_event_type`, which prints the
    event type (ONVIF topic) to stdout.

    ``callback`` may be a plain function or an ``async def`` coroutine
    function. Runs until the caller cancels the surrounding task (e.g.
    Ctrl+C / task cancellation) or an underlying SOAP call raises.
    """
    engine = OnvifEventEngine(hostname, port, username, password, verbose)
    try:
        await asyncio.to_thread(
            engine.subscribe,
            termination_time,
            topic_filter,
        )
        await engine.stream_to_callback(
            callback,
            timeout=timeout,
            limit=limit,
            renew_every=renew_every,
        )
    finally:
        await asyncio.to_thread(engine.close)


__all__ = [
    # data model
    "EventCapableCamera",
    "EventFilter",
    "EventNotification",
    "SupportedEventTopic",
    # capability discovery
    "is_event_capable",
    "find_event_capable_cameras",
    "find_event_capable_cameras_async",
    # supported event introspection
    "get_supported_event_topics",
    # simple event reading
    "pull_events_once",
    "stream_events",
    # callback-based event watching
    "print_event_type",
    "watch_events",
]
