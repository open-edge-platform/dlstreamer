# ==============================================================================
# Copyright (C) 2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================
"""
Interactive text UI exercising :mod:`dlstreamer.onvif.event_manager`.

Flow:

1. Use :mod:`dlstreamer.onvif.discovery` to collect ONVIF cameras.
2. Filter with :func:`~dlstreamer.onvif.event_manager.find_event_capable_cameras`
   to keep only cameras exposing an Events service.
3. Pick a camera, then exercise the full PullPoint lifecycle
   (:meth:`~OnvifEventEngine.subscribe`,
    :meth:`~OnvifEventEngine.pull`,
    :meth:`~OnvifEventEngine.renew`,
    :meth:`~OnvifEventEngine.unsubscribe`) or run a continuous
   :meth:`~OnvifEventEngine.stream` loop.

The UI is a plain menu loop using ``input()`` so it runs on any TTY
(local console, SSH session, container) without extra dependencies.
"""
from __future__ import annotations

import asyncio
import getpass
import os
import time
from typing import Iterable, Optional

from dlstreamer.onvif.discovery import (
    discover_onvif_cameras,
    discover_onvif_cameras_async,
)
from dlstreamer.onvif.event_manager import (
    EventCapableCamera,
    EventFilter,
    EventNotification,
    find_event_capable_cameras,
    print_event_type,
)
from dlstreamer.onvif.event_manager.engine import OnvifEventEngine


# ---------------------------------------------------------------------------
# State
# ---------------------------------------------------------------------------


class TestAppState:
    """In-memory state shared between menu actions."""

    def __init__(self) -> None:
        self.username: str = os.environ.get("ONVIF_USER", "")
        self.password: str = os.environ.get("ONVIF_PASSWORD", "")
        self.verbose: bool = False
        self.cameras: list[dict] = []
        self.event_cameras: list[EventCapableCamera] = []
        self.selected: Optional[EventCapableCamera] = None
        self.engine: Optional[OnvifEventEngine] = None
        self.notifications: list[EventNotification] = []

    def close_engine(self) -> None:
        """Close the current engine (if any) and clear the selection."""
        if self.engine is not None:
            try:
                self.engine.close()
            except Exception:  # pylint: disable=broad-exception-caught
                pass
            self.engine = None


# ---------------------------------------------------------------------------
# Formatting helpers
# ---------------------------------------------------------------------------


def _print_table(title: str, headers: list[str], rows: list[list[str]]) -> None:
    print(f"\n{title}")
    if not rows:
        print("  (no rows)")
        return
    widths = [
        max(len(h), *(len(row[i]) for row in rows)) for i, h in enumerate(headers)
    ]
    sep = "+-" + "-+-".join("-" * w for w in widths) + "-+"
    print(sep)
    print("| " + " | ".join(h.ljust(w) for h, w in zip(headers, widths)) + " |")
    print(sep)
    for row in rows:
        print("| " + " | ".join(v.ljust(w) for v, w in zip(row, widths)) + " |")
    print(sep)


def _print_cameras(cameras: Iterable[dict], title: str = "Cameras") -> None:
    rows = [
        [str(i), str(cam.get("hostname", "-")), str(cam.get("port", "-"))]
        for i, cam in enumerate(cameras, 1)
    ]
    _print_table(title, ["#", "hostname", "port"], rows)


def _print_event_cameras(cams: Iterable[EventCapableCamera]) -> None:
    rows = [[str(i), c.camera_id, "ok" if c.ok else (c.error or "-")]
            for i, c in enumerate(cams, 1)]
    _print_table(
        "Event-capable cameras",
        ["#", "camera", "status"],
        rows,
    )


def _print_notifications(items: list[EventNotification], title: str) -> None:
    if not items:
        print(f"  {title}: (empty)")
        return
    rows = []
    for i, n in enumerate(items, 1):
        source = ",".join(f"{k}={v}" for k, v in n.source.items()) or "-"
        data = ",".join(f"{k}={v}" for k, v in n.data.items()) or "-"
        rows.append([
            str(i),
            (n.utc_time or "-")[:19],
            (n.topic or "-")[:60],
            n.property_operation or "-",
            source[:40],
            data[:40],
        ])
    _print_table(
        f"{title} ({len(items)})",
        ["#", "utc_time", "topic (trimmed)", "op", "source (trimmed)", "data (trimmed)"],
        rows,
    )


# ---------------------------------------------------------------------------
# Input helpers
# ---------------------------------------------------------------------------


def _prompt(prompt: str, default: Optional[str] = None) -> str:
    suffix = f" [{default}]" if default is not None else ""
    value = input(f"{prompt}{suffix}: ").strip()
    return value or (default or "")


def _prompt_bool(prompt: str, default: bool = False) -> bool:
    default_str = "y" if default else "n"
    value = _prompt(f"{prompt} (y/n)", default_str).lower()
    return value in ("y", "yes", "true", "1")


def _prompt_int(prompt: str, default: int) -> int:
    while True:
        raw = _prompt(prompt, str(default))
        try:
            return int(raw)
        except ValueError:
            print(f"  '{raw}' is not a valid integer, try again.")


def _ensure_credentials(state: TestAppState) -> bool:
    if not state.username or not state.password:
        print("  Credentials are empty; use option 1 to set them first.")
        return False
    return True


def _ensure_engine(state: TestAppState) -> bool:
    if state.engine is None:
        print("  No camera selected. Use option 4 to pick an event-capable camera.")
        return False
    return True


def _ensure_subscribed(state: TestAppState) -> bool:
    if not _ensure_engine(state):
        return False
    if not state.engine.subscribed:
        print("  Not subscribed. Use option 5 to subscribe first.")
        return False
    return True


# ---------------------------------------------------------------------------
# Menu actions
# ---------------------------------------------------------------------------


def action_configure(state: TestAppState) -> None:
    """Update credentials and verbose flag."""
    print("\n-- Configure --")
    state.username = _prompt("ONVIF username", state.username)
    new_password = getpass.getpass(
        f"ONVIF password [{'*' * len(state.password) if state.password else ''}]: "
    )
    if new_password:
        state.password = new_password
    state.verbose = _prompt_bool("Verbose logging", state.verbose)


def _scan_event_cameras_sync(state: TestAppState, cameras: list[dict]) -> None:
    """Filter ``cameras`` down to event-capable ones."""
    start = time.monotonic()
    caps = list(
        find_event_capable_cameras(
            cameras,
            username=state.username,
            password=state.password,
            verbose=state.verbose,
        )
    )
    elapsed = time.monotonic() - start
    state.event_cameras = caps
    print(f"  event-capable: {len(caps)} of {len(cameras)} in {elapsed:.1f}s")
    _print_event_cameras(caps)


def action_scan_sync(state: TestAppState) -> None:
    """Discover cameras (sync) and filter for event-capable ones."""
    print("\n-- Discover (sync) + filter event-capable --")
    if not _ensure_credentials(state):
        return

    start = time.monotonic()
    cameras = list(discover_onvif_cameras(state.verbose))
    elapsed = time.monotonic() - start
    state.cameras = cameras
    _print_cameras(cameras, f"Discovered {len(cameras)} camera(s) in {elapsed:.1f}s")
    if not cameras:
        return
    _scan_event_cameras_sync(state, cameras)


def action_scan_async(state: TestAppState) -> None:
    """Discover cameras (async) and filter for event-capable ones."""
    print("\n-- Discover (async) + filter event-capable --")
    if not _ensure_credentials(state):
        return

    discovered: list[dict] = []

    async def _run() -> None:
        async for cam in discover_onvif_cameras_async(state.verbose):
            discovered.append(cam)
            print(f"  discovered: {cam}")

    start = time.monotonic()
    asyncio.run(_run())
    elapsed = time.monotonic() - start
    state.cameras = discovered
    _print_cameras(discovered, f"Discovered {len(discovered)} camera(s) in {elapsed:.1f}s")
    if not discovered:
        return
    _scan_event_cameras_sync(state, discovered)


def action_pick_camera(state: TestAppState) -> None:
    """Pick an event-capable camera and create an :class:`OnvifEventEngine`."""
    print("\n-- Pick event-capable camera --")
    if not _ensure_credentials(state):
        return
    if not state.event_cameras:
        # allow direct entry if discovery hasn't been run
        print("  No cached event-capable cameras. Enter host:port manually "
              "or run option 2/3 first.")
        print("  Hint: the port is the camera's configured ONVIF service port, "
              "not necessarily 80 (e.g. 2020 for this camera).")
        raw = _prompt("host:port (blank = cancel)")
        if not raw or ":" not in raw:
            return
        host, port_s = raw.rsplit(":", 1)
        try:
            port = int(port_s)
        except ValueError:
            print(f"  '{port_s}' is not a valid port.")
            return
        cam = EventCapableCamera(hostname=host, port=port)
    else:
        _print_event_cameras(state.event_cameras)
        raw = _prompt("Pick camera by number (blank = cancel)")
        if not raw:
            return
        try:
            n = int(raw)
        except ValueError:
            print(f"  '{raw}' is not a valid number.")
            return
        if not 1 <= n <= len(state.event_cameras):
            print(f"  Index {n} out of range.")
            return
        cam = state.event_cameras[n - 1]

    state.close_engine()
    state.selected = cam
    state.engine = OnvifEventEngine(
        cam.hostname, cam.port,
        username=state.username,
        password=state.password,
        verbose=state.verbose,
    )
    print(f"  selected: {cam.camera_id}")


def action_subscribe(state: TestAppState) -> None:
    """Create a PullPoint subscription with an optional topic filter."""
    print("\n-- Subscribe --")
    if not _ensure_engine(state):
        return
    if state.engine.subscribed:
        print(f"  Already subscribed to {state.selected.camera_id}. "
              "Use option 8 to unsubscribe first.")
        return

    termination = _prompt("Initial termination time (ISO 8601 duration)", "PT1H")
    use_filter = _prompt_bool("Add topic filter?", False)
    topic_filter: Optional[EventFilter] = None
    if use_filter:
        expr = _prompt(
            "Topic expression (e.g. tns1:VideoSource//. )",
            "tns1:VideoSource//.",
        )
        dialect = _prompt(
            "Dialect URI",
            "http://www.onvif.org/ver10/tev/topicExpression/ConcreteSet",
        )
        topic_filter = EventFilter(expression=expr, dialect=dialect)

    try:
        state.engine.subscribe(termination_time=termination, topic_filter=topic_filter)
    except Exception as exc:  # pylint: disable=broad-exception-caught
        print(f"  [ERROR] subscribe failed: {type(exc).__name__}: {exc}")
        return

    print(f"  ref:  {state.engine.subscription_reference}")
    print(f"  term: {state.engine.termination_time}")


def action_pull(state: TestAppState) -> None:
    """Fetch one batch of notifications (long-polling)."""
    print("\n-- PullMessages --")
    if not _ensure_subscribed(state):
        return
    timeout = _prompt("Pull timeout (ISO 8601)", "PT30S")
    limit = _prompt_int("Message limit", 100)

    start = time.monotonic()
    try:
        batch = state.engine.pull(timeout=timeout, limit=limit)
    except Exception as exc:  # pylint: disable=broad-exception-caught
        print(f"  [ERROR] pull failed: {type(exc).__name__}: {exc}")
        return
    elapsed = time.monotonic() - start

    print(f"  Received {len(batch)} note(s) in {elapsed:.2f}s")
    if batch:
        state.notifications.extend(batch)
    _print_notifications(batch, "This batch")


def action_renew(state: TestAppState) -> None:
    """Extend the current subscription."""
    print("\n-- Renew subscription --")
    if not _ensure_subscribed(state):
        return
    termination = _prompt("New termination time", "PT1H")
    try:
        state.engine.renew(termination_time=termination)
    except Exception as exc:  # pylint: disable=broad-exception-caught
        print(f"  [ERROR] renew failed: {type(exc).__name__}: {exc}")
        return
    print(f"  new termination: {state.engine.termination_time}")


def action_unsubscribe(state: TestAppState) -> None:
    """Cancel the current subscription."""
    print("\n-- Unsubscribe --")
    if not _ensure_engine(state):
        return
    if not state.engine.subscribed:
        print("  Nothing to unsubscribe.")
        return
    try:
        state.engine.unsubscribe()
    except Exception as exc:  # pylint: disable=broad-exception-caught
        print(f"  [ERROR] unsubscribe failed: {type(exc).__name__}: {exc}")
        return
    print("  unsubscribed.")


def action_stream(state: TestAppState) -> None:
    """Continuous streaming via async :meth:`OnvifEventEngine.stream`.

    Runs until the user hits Ctrl+C or a chosen stop condition
    (max messages / max seconds) is met. Auto-renews the subscription
    every 5 minutes.
    """
    print("\n-- Stream notifications (Ctrl+C to stop) --")
    if not _ensure_subscribed(state):
        return

    poll_timeout = _prompt("Per-pull timeout (ISO 8601)", "PT30S")
    limit = _prompt_int("Per-pull message limit", 100)
    max_messages = _prompt_int("Stop after N messages (0 = no limit)", 0)
    max_seconds = _prompt_int("Stop after N seconds (0 = no limit)", 0)

    engine = state.engine
    received: list[EventNotification] = []
    start = time.monotonic()

    async def _run() -> None:
        try:
            async for note in engine.stream(timeout=poll_timeout, limit=limit):
                received.append(note)
                print(f"  {note.short()}")
                if max_messages and len(received) >= max_messages:
                    print(f"  reached max_messages={max_messages}, stopping.")
                    return
                if max_seconds and (time.monotonic() - start) >= max_seconds:
                    print(f"  reached max_seconds={max_seconds}, stopping.")
                    return
        except asyncio.CancelledError:
            pass

    try:
        asyncio.run(_run())
    except KeyboardInterrupt:
        print("\n  Ctrl+C — stopping stream.")

    elapsed = time.monotonic() - start
    state.notifications.extend(received)
    print(f"  streamed {len(received)} note(s) in {elapsed:.1f}s "
          f"(total cached: {len(state.notifications)})")


def action_watch_event_types(state: TestAppState) -> None:
    """Stream events through a callback that prints each event's type.

    Demonstrates :meth:`OnvifEventEngine.stream_to_callback`: every new
    notification triggers ``print_event_type``, which prints the ONVIF
    topic (the event type). Runs until Ctrl+C or the time limit.
    """
    print("\n-- Watch event types (callback, Ctrl+C to stop) --")
    if not _ensure_subscribed(state):
        return

    poll_timeout = _prompt("Per-pull timeout (ISO 8601)", "PT30S")
    limit = _prompt_int("Per-pull message limit", 100)
    max_seconds = _prompt_int("Stop after N seconds (0 = no limit)", 0)

    engine = state.engine
    count = 0
    start = time.monotonic()

    def _on_event(note: EventNotification) -> None:
        nonlocal count
        count += 1
        state.notifications.append(note)
        print_event_type(note)
        if max_seconds and (time.monotonic() - start) >= max_seconds:
            raise _StopWatch

    async def _run() -> None:
        try:
            await engine.stream_to_callback(
                _on_event,
                timeout=poll_timeout,
                limit=limit,
            )
        except _StopWatch:
            print(f"  reached max_seconds={max_seconds}, stopping.")
        except asyncio.CancelledError:
            pass

    try:
        asyncio.run(_run())
    except KeyboardInterrupt:
        print("\n  Ctrl+C — stopping watch.")

    elapsed = time.monotonic() - start
    print(f"  handled {count} event(s) in {elapsed:.1f}s "
          f"(total cached: {len(state.notifications)})")


class _StopWatch(Exception):
    """Internal sentinel to break out of a callback-driven stream."""


def action_service_capabilities(state: TestAppState) -> None:
    """Print ``EventServiceCapabilities`` for the selected camera."""
    print("\n-- GetServiceCapabilities --")
    if not _ensure_engine(state):
        return
    try:
        caps = state.engine.get_service_capabilities()
    except Exception as exc:  # pylint: disable=broad-exception-caught
        print(f"  [ERROR] {type(exc).__name__}: {exc}")
        return
    for k, v in caps.items():
        print(f"  {k}: {v}")


def action_event_properties(state: TestAppState) -> None:
    """Print topic dialects and top-level topic set (best-effort)."""
    print("\n-- GetEventProperties --")
    if not _ensure_engine(state):
        return
    try:
        props = state.engine.get_event_properties()
    except Exception as exc:  # pylint: disable=broad-exception-caught
        print(f"  [ERROR] {type(exc).__name__}: {exc}")
        return

    def _as_list(node) -> list:
        return list(node or [])

    print("  TopicExpressionDialect:")
    for d in _as_list(getattr(props, "TopicExpressionDialect", None)):
        print(f"    - {d}")
    print("  MessageContentFilterDialect:")
    for d in _as_list(getattr(props, "MessageContentFilterDialect", None)):
        print(f"    - {d}")
    print("  TopicNamespaceLocation:")
    for d in _as_list(getattr(props, "TopicNamespaceLocation", None)):
        print(f"    - {d}")

    topic_set = getattr(props, "TopicSet", None)
    if topic_set is not None:
        print("  TopicSet (top-level element names):")
        # Zeep xsd:any container — enumerate first-level children if present.
        for attr in ("_value_1", "_value_2"):
            children = getattr(topic_set, attr, None)
            if children is None:
                continue
            if not isinstance(children, list):
                children = [children]
            for ch in children:
                tag = getattr(ch, "tag", None) or ch
                print(f"    - {tag}")
            break


def action_supported_events(state: TestAppState) -> None:
    """List the event types the selected camera advertises (GetEventProperties).

    Shows the full detail for each topic: its Source/Data field names and
    types (e.g. ``IsMotion:xsd:boolean``) and whether it is a property.
    """
    print("\n-- Supported event topics --")
    if not _ensure_engine(state):
        return
    try:
        topics = state.engine.get_supported_event_topics()
    except Exception as exc:  # pylint: disable=broad-exception-caught
        print(f"  [ERROR] {type(exc).__name__}: {exc}")
        return
    if not topics:
        print("  (camera advertised no event topics)")
        return
    rows = []
    for i, t in enumerate(topics, 1):
        source = ",".join(f"{k}:{v}" for k, v in t.source.items()) or "-"
        data = ",".join(f"{k}:{v}" for k, v in t.data.items()) or "-"
        # Indent property topics by 4 spaces so they read as a tree hanging
        # off their parent event topic.
        indent = "    " if t.is_property else ""
        rows.append([
            str(i),
            f"{indent}{t.topic}"[:64],
            "property" if t.is_property else "event",
            source[:40],
            data[:40],
        ])
    _print_table(
        f"Supported event topics ({len(topics)})",
        ["#", "topic", "kind", "source (name:type)", "data (name:type)"],
        rows,
    )


def action_show_state(state: TestAppState) -> None:
    """Dump current in-memory state."""
    print("\n-- Current state --")
    print(f"  username: '{state.username}'")
    print(f"  password: {'*' * len(state.password)} ({len(state.password)} chars)")
    print(f"  verbose:  {state.verbose}")
    _print_cameras(state.cameras, "Cached cameras")
    _print_event_cameras(state.event_cameras)

    if state.selected is None:
        print("\n  selected camera: -")
    else:
        eng = state.engine
        status = (
            f"subscribed=True ref={eng.subscription_reference} "
            f"term={eng.termination_time}"
            if eng and eng.subscribed
            else "subscribed=False"
        )
        print(f"\n  selected camera: {state.selected.camera_id}  ({status})")

    _print_notifications(state.notifications[-20:], "Last 20 cached notifications")


def action_clear_notifications(state: TestAppState) -> None:
    """Drop the in-memory notification cache."""
    n = len(state.notifications)
    state.notifications.clear()
    print(f"\n  cleared {n} cached notification(s).")


# ---------------------------------------------------------------------------
# Main loop
# ---------------------------------------------------------------------------


_MENU = """
===  Sample Application: ONVIF Event Manager ===

Credentials: user='{user}'  password={pw_len} chars  verbose={verbose}
State:       cams={n_cams}  event_capable={n_evc}  selected={sel}  subscribed={sub}  cached_notes={n_notes}

  Discovery
    1) Configure credentials / verbose flag
    2) Discover cameras (sync)  + filter event-capable
    3) Discover cameras (async) + filter event-capable
    4) Pick event-capable camera

  Subscription lifecycle
    5) Subscribe (optional topic filter)
    6) Pull once (long-polling PullMessages)
    7) Renew subscription
    8) Unsubscribe

  Continuous
    9) Stream notifications (Ctrl+C to stop)
   10) Watch event types via callback (Ctrl+C to stop)

  Introspection
   11) GetServiceCapabilities
   12) GetEventProperties (topic dialects / TopicSet)
   13) Supported event topics (parsed list)

  General
   14) Show current state (+ last 20 cached notifications)
   15) Clear cached notifications
    0) Exit

Other samples: 
    - Discovery          : python -m dlstreamer.onvif.samples.discovery_sample
    - Profile querying   : python -m dlstreamer.onvif.samples.camera_profiles_sample
    - PTZ control        : python -m dlstreamer.onvif.samples.ptz_sample
    - Video engine       : python -m dlstreamer.onvif.samples.video_engine_sample



> """


def run() -> None:
    """Start the interactive sample application."""
    state = TestAppState()
    print("Welcome to the ONVIF Event Manager sample app.")
    print("Tip: set ONVIF_USER / ONVIF_PASSWORD env vars to pre-fill credentials.")

    dispatcher = {
        "1": action_configure,
        "2": action_scan_sync,
        "3": action_scan_async,
        "4": action_pick_camera,
        "5": action_subscribe,
        "6": action_pull,
        "7": action_renew,
        "8": action_unsubscribe,
        "9": action_stream,
        "10": action_watch_event_types,
        "11": action_service_capabilities,
        "12": action_event_properties,
        "13": action_supported_events,
        "14": action_show_state,
        "15": action_clear_notifications,
    }

    while True:
        prompt = _MENU.format(
            user=state.username or "-",
            pw_len=len(state.password),
            verbose=state.verbose,
            n_cams=len(state.cameras),
            n_evc=len(state.event_cameras),
            sel=state.selected.camera_id if state.selected else "-",
            sub=(state.engine.subscribed if state.engine else False),
            n_notes=len(state.notifications),
        )
        try:
            choice = input(prompt).strip()
        except (EOFError, KeyboardInterrupt):
            print("\nBye.")
            state.close_engine()
            return

        if choice == "0":
            print("Bye.")
            state.close_engine()
            return

        action = dispatcher.get(choice)
        if action is None:
            print(f"  Unknown choice: {choice!r}")
            continue

        try:
            action(state)
        except KeyboardInterrupt:
            print("\n  Interrupted; back to menu.")
        except Exception as exc:  # pylint: disable=broad-exception-caught
            print(f"  [ERROR] {type(exc).__name__}: {exc}")


if __name__ == "__main__":
    run()
