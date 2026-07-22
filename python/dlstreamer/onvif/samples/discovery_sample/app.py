# ==============================================================================
# Copyright (C) 2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================
"""
Interactive text UI exercising every public entry point of
:mod:`dlstreamer.onvif.discovery`:

- :func:`discover_onvif_cameras`         — synchronous WS-Discovery
- :func:`discover_onvif_cameras_async`   — asynchronous WS-Discovery
- :func:`extract_xaddrs` / :func:`parse_xaddrs_url` helpers

Discovery is credential-free — media profile querying now lives in
:mod:`dlstreamer.onvif.camera_profiles` and has its own sample app
(``python -m dlstreamer.onvif.samples.camera_profiles_sample``).

The UI is a plain menu loop using ``input()`` so it runs on any TTY
(local console, SSH session, container) without extra dependencies.
"""
from __future__ import annotations

import asyncio
import time
from typing import Iterable, Optional

from dlstreamer.onvif.discovery import (
    discover_onvif_cameras,
    discover_onvif_cameras_async,
    extract_xaddrs,
    parse_xaddrs_url,
)


# ---------------------------------------------------------------------------
# State
# ---------------------------------------------------------------------------


class TestAppState:
    """In-memory state shared between menu actions."""

    def __init__(self) -> None:
        self.verbose: bool = False
        self.cameras: list[dict] = []


# ---------------------------------------------------------------------------
# Formatting helpers
# ---------------------------------------------------------------------------


def _print_table(title: str, headers: list[str], rows: list[list[str]]) -> None:
    """Print a small ASCII table."""
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
    for row in rows:print("| " + " | ".join(v.ljust(w) for v, w in zip(row, widths)) + " |")
        
    print(sep)


def _print_cameras(cameras: Iterable[dict], title: str = "Cameras") -> None:
    """Render a list of ``{"hostname", "port"}`` dicts."""
    rows = [
        [str(i), str(cam.get("hostname", "-")), str(cam.get("port", "-"))]
        for i, cam in enumerate(cameras, 1)
    ]
    _print_table(title, ["#", "hostname", "port"], rows)


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


# ---------------------------------------------------------------------------
# Menu actions
# ---------------------------------------------------------------------------


def action_configure(state: TestAppState) -> None:
    """Toggle the verbose flag used by the discovery engine."""
    print("\n-- Configure --")
    state.verbose = _prompt_bool("Verbose logging", state.verbose)


def action_probe_sync(state: TestAppState) -> None:
    """Run synchronous WS-Discovery via the low-level function."""
    print("\n-- discover_onvif_cameras (sync) --")
    start = time.monotonic()
    cameras = list(discover_onvif_cameras(state.verbose))
    elapsed = time.monotonic() - start
    state.cameras = cameras
    _print_cameras(cameras, f"Discovered {len(cameras)} camera(s) in {elapsed:.2f}s")


def action_probe_async(state: TestAppState) -> None:
    """Run asynchronous WS-Discovery via the low-level async generator."""
    print("\n-- discover_onvif_cameras_async (async, low-level) --")

    async def _run() -> list[dict]:
        collected: list[dict] = []
        async for cam in discover_onvif_cameras_async(state.verbose):
            collected.append(cam)
            print(f"  found: {cam}")
        return collected

    start = time.monotonic()
    cameras = asyncio.run(_run())
    elapsed = time.monotonic() - start
    state.cameras = cameras
    _print_cameras(cameras, f"Discovered {len(cameras)} camera(s) in {elapsed:.2f}s")


def action_xml_helpers(state: TestAppState) -> None:  # pylint: disable=unused-argument
    """Exercise ``extract_xaddrs`` / ``parse_xaddrs_url`` on user input."""
    print("\n-- XML helpers --")
    print("  1) Paste an XAddrs URL and parse it")
    print("  2) Paste a WS-Discovery ProbeMatch XML and extract XAddrs")
    choice = _prompt("Pick 1 or 2 (blank = cancel)")
    if choice == "1":
        url = _prompt("XAddrs URL (e.g. http://192.168.1.10/onvif/device_service)")
        if not url:
            return
        parts = parse_xaddrs_url(url)
        rows = [[k, str(v)] for k, v in parts.items()]
        _print_table("parse_xaddrs_url result", ["key", "value"], rows)
    elif choice == "2":
        print("Paste the XML, end with an empty line:")
        lines: list[str] = []
        while True:
            line = input()
            if not line:
                break
            lines.append(line)
        xml_string = "\n".join(lines)
        result = extract_xaddrs(xml_string)
        print(f"  extract_xaddrs result: {result!r}")


def action_show_state(state: TestAppState) -> None:
    """Dump current in-memory state."""
    print("\n-- Current state --")
    print(f"  verbose: {state.verbose}")
    _print_cameras(state.cameras, "Cached cameras")


# ---------------------------------------------------------------------------
# Main loop
# ---------------------------------------------------------------------------


_MENU = """
===  Sample Application: ONVIF Discovery Library ===

Settings: verbose={verbose}
State:    cached cameras={n_cams}

  1) Configure verbose flag
  2) discover_onvif_cameras       (sync, low-level)
  3) discover_onvif_cameras_async (async, low-level)
  4) Show current state
  0) Exit

  Other samples: 
    - Profile querying   : python -m dlstreamer.onvif.samples.camera_profiles_sample
    - PTZ control        : python -m dlstreamer.onvif.samples.ptz_sample
    - Event subscription : python -m dlstreamer.onvif.samples.event_manager_sample
    - Video engine       : python -m dlstreamer.onvif.samples.video_engine_sample

> """


def run() -> None:
    """Start the interactive sample application."""
    state = TestAppState()

    while True:
        prompt = _MENU.format(
            verbose=state.verbose,
            n_cams=len(state.cameras),
        )
        try:
            choice = input(prompt).strip()
        except (EOFError, KeyboardInterrupt):
            print("\nBye.")
            return

        try:
            if choice == "0":
                print("Bye.")
                return
            if choice == "1":
                action_configure(state)
            elif choice == "2":
                action_probe_sync(state)
            elif choice == "3":
                action_probe_async(state)
            elif choice == "4":
                action_show_state(state)
            else:
                print(f"  Unknown choice: {choice!r}")
        except KeyboardInterrupt:
            print("\n  Interrupted; back to menu.")
        except Exception as exc:  # pylint: disable=broad-exception-caught
            print(f"  [ERROR] {type(exc).__name__}: {exc}")


if __name__ == "__main__":
    run()
