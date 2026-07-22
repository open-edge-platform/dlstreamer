# ==============================================================================
# Copyright (C) 2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================
"""
Interactive text UI exercising :mod:`dlstreamer.onvif.camera_profiles`.

Flow:

1. Use :mod:`dlstreamer.onvif.discovery` to collect ONVIF cameras on the
   local network (WS-Discovery, sync or async).
2. Feed the resulting camera descriptors to
   :mod:`dlstreamer.onvif.camera_profiles` to read the supported media
   profiles from each camera.

Every public entry point of ``camera_profiles`` is reachable from the
menu:

- :func:`read_camera_profiles`         — sync wrapper (single/all cameras)
- :func:`read_camera_profiles_async`   — async wrapper (single/all cameras)

The UI is a plain menu loop using ``input()`` so it runs on any TTY
(local console, SSH session, container) without extra dependencies.
"""
from __future__ import annotations

import asyncio
import getpass
import os
import shutil
import subprocess
import time
from dataclasses import fields
from typing import Iterable, Optional
from urllib.parse import quote, urlparse, urlunparse

from dlstreamer.onvif.discovery import (
    discover_onvif_cameras,
    discover_onvif_cameras_async,
)
from dlstreamer.onvif.camera_profiles import (
    CameraProfilesResult,
    ONVIFProfile,
    read_camera_profiles,
    read_camera_profiles_async,
)


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
        self.results: dict[str, CameraProfilesResult] = {}
        # Background RTSP players spawned via option 10.
        self.players: list[tuple[subprocess.Popen, str]] = []

    def prune_players(self) -> list[tuple[subprocess.Popen, str]]:
        """Drop players that have already exited; return the removed ones."""
        alive: list[tuple[subprocess.Popen, str]] = []
        finished: list[tuple[subprocess.Popen, str]] = []
        for proc, label in self.players:
            if proc.poll() is None:
                alive.append((proc, label))
            else:
                finished.append((proc, label))
        self.players = alive
        return finished

    def stop_players(self) -> int:
        """Terminate every running player; return how many were stopped."""
        self.prune_players()
        stopped = 0
        for proc, _ in self.players:
            if proc.poll() is None:
                proc.terminate()
                stopped += 1
        for proc, _ in self.players:
            try:
                proc.wait(timeout=2)
            except subprocess.TimeoutExpired:
                proc.kill()
        self.players = []
        return stopped


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
    for row in rows:
        print("| " + " | ".join(v.ljust(w) for v, w in zip(row, widths)) + " |")
    print(sep)


def _print_cameras(cameras: Iterable[dict], title: str = "Cameras") -> None:
    """Render a list of ``{"hostname", "port"}`` dicts."""
    rows = [
        [str(i), str(cam.get("hostname", "-")), str(cam.get("port", "-"))]
        for i, cam in enumerate(cameras, 1)
    ]
    _print_table(title, ["#", "hostname", "port"], rows)


def _profile_summary_row(index: int, p: ONVIFProfile) -> list[str]:
    resolution = "-"
    if p.vec_resolution:
        resolution = (
            f"{p.vec_resolution.get('width', '?')}x{p.vec_resolution.get('height', '?')}"
        )
    return [
        str(index),
        p.name or "-",
        p.token or "-",
        p.vec_encoding or "-",
        resolution,
        str(p.vec_framerate_limit or "-"),
        str(p.vec_bitrate_limit or "-"),
        p.ptz_name or "-",
        (p.rtsp_url or "-")[:60],
    ]


def _print_profiles(profiles: list[ONVIFProfile], camera_id: str) -> None:
    rows = [_profile_summary_row(i, p) for i, p in enumerate(profiles, 1)]
    _print_table(
        f"Profiles of {camera_id}",
        ["#", "name", "token", "codec", "resolution",
         "fps", "bitrate", "ptz", "rtsp_url (trimmed)"],
        rows,
    )


def _print_result(result: CameraProfilesResult) -> None:
    """Render a :class:`CameraProfilesResult`."""
    camera_id = f"{result.hostname}:{result.port}"
    if not result.ok:
        print(f"  [FAIL] {camera_id}: {result.error}")
        return
    mac_info = f"  MAC={result.mac_address}" if result.mac_address else ""
    print(f"  [ OK ] {camera_id}: {len(result.profiles)} profile(s){mac_info}")
    _print_profiles(result.profiles, camera_id)


# Grouped layout for the full profile dump. Any field not listed here is still
# printed under "Other", so new ONVIFProfile fields are never silently dropped.
_PROFILE_DETAIL_GROUPS = [
    ("Connection", ["ip_address", "port", "username", "password", "mac_address"]),
    ("Profile", ["name", "token", "fixed", "rtsp_url",
                 "video_source_configuration", "video_encoder_configuration"]),
    ("Video Source Configuration (VSC)",
     ["vsc_name", "vsc_token", "vsc_source_token", "vsc_bounds"]),
    ("Video Encoder Configuration (VEC)",
     ["vec_name", "vec_token", "vec_encoding", "vec_resolution", "vec_quality",
      "vec_rate_control", "vec_multicast", "vec_framerate_limit", "vec_bitrate_limit",
      "vec_encoding_interval", "vec_h264_profile", "vec_h264_gop_length",
      "vec_mpeg4_profile", "vec_mpeg4_gop_length"]),
    ("Audio Source Configuration (ASC)",
     ["asc_name", "asc_token", "asc_source_token"]),
    ("Audio Encoder Configuration (AEC)",
     ["aec_name", "aec_token", "aec_encoding", "aec_bitrate", "aec_sample_rate"]),
    ("PTZ Configuration", ["ptz_name", "ptz_token", "ptz_node_token"]),
]


def _print_profile_details(index: int, p: ONVIFProfile) -> None:
    """Print every field of a single :class:`ONVIFProfile`, grouped and complete."""
    print(f"\n  --- Profile #{index}: {p.name or '-'} (token={p.token or '-'}) ---")
    shown: set[str] = set()

    def _fmt(field_name: str) -> None:
        value = getattr(p, field_name)
        if field_name == "password":
            value = ("*" * len(str(value))) if value else ""
        print(f"    {field_name:26} = {value!r}")
        shown.add(field_name)

    for title, names in _PROFILE_DETAIL_GROUPS:
        print(f"    [{title}]")
        for name in names:
            _fmt(name)
    remaining = [f.name for f in fields(p) if f.name not in shown]
    if remaining:
        print("    [Other]")
        for name in remaining:
            _fmt(name)


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


def _pick_camera(state: TestAppState) -> Optional[tuple[str, int]]:
    """Ask the user which camera to target.

    Offers the list of previously discovered cameras plus manual
    ``host:port`` entry. Returns ``None`` when the user cancels.
    """
    if state.cameras:
        _print_cameras(state.cameras, "Cached cameras")
        choice = _prompt(
            "Pick a camera by number, or type 'host:port' (blank = cancel)"
        )
    else:
        print("No cached cameras. Run discovery first (option 2 or 3), "
              "or enter host:port manually.")
        choice = _prompt("host:port (blank = cancel)")

    if not choice:
        return None

    if choice.isdigit() and state.cameras:
        idx = int(choice) - 1
        if not 0 <= idx < len(state.cameras):
            print(f"  Index {choice} out of range.")
            return None
        cam = state.cameras[idx]
        return str(cam["hostname"]), int(cam["port"])

    if ":" in choice:
        host, port_s = choice.rsplit(":", 1)
        try:
            return host, int(port_s)
        except ValueError:
            print(f"  '{port_s}' is not a valid port.")
            return None

    print("  Could not parse camera specifier.")
    return None


def _ensure_credentials(state: TestAppState) -> bool:
    if not state.username or not state.password:
        print("  Credentials are empty; use option 1 to set them first.")
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


def action_discover_sync(state: TestAppState) -> None:
    """Run synchronous WS-Discovery via the discovery library."""
    print("\n-- discover_onvif_cameras (sync) --")
    start = time.monotonic()
    cameras = list(discover_onvif_cameras(state.verbose))
    elapsed = time.monotonic() - start
    state.cameras = cameras
    _print_cameras(cameras, f"Discovered {len(cameras)} camera(s) in {elapsed:.2f}s")


def action_discover_async(state: TestAppState) -> None:
    """Run asynchronous WS-Discovery via the discovery library."""
    print("\n-- discover_onvif_cameras_async (async) --")

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


def action_read_one(state: TestAppState, use_async: bool) -> None:
    """Read profiles for one chosen camera via function wrappers."""
    label = "async" if use_async else "sync"
    print(f"\n-- read_camera_profiles ({label}, single camera) --")

    if not _ensure_credentials(state):
        return

    target = _pick_camera(state)
    if target is None:
        return
    host, port = target
    one_camera = [{"hostname": host, "port": port}]

    start = time.monotonic()
    if use_async:
        async def _run_one() -> CameraProfilesResult:
            async for item in read_camera_profiles_async(
                one_camera,
                username=state.username,
                password=state.password,
                verbose=state.verbose,
            ):
                return item
            return CameraProfilesResult(hostname=host, port=port, error="No result")

        result = asyncio.run(_run_one())
    else:
        result = next(
            read_camera_profiles(
                one_camera,
                username=state.username,
                password=state.password,
                verbose=state.verbose,
            ),
            CameraProfilesResult(hostname=host, port=port, error="No result"),
        )
    elapsed = time.monotonic() - start

    state.results[f"{host}:{port}"] = result
    print(f"  finished in {elapsed:.2f}s")
    _print_result(result)


def action_read_many_sync(state: TestAppState) -> None:
    """Read profiles for every cached camera using :func:`read_camera_profiles`."""
    print("\n-- read_camera_profiles (sync) over cached cameras --")

    if not _ensure_credentials(state):
        return
    if not state.cameras:
        print("  No cached cameras. Run discovery first (option 2 or 3).")
        return

    start = time.monotonic()
    ok = fail = 0
    for result in read_camera_profiles(
        state.cameras,
        username=state.username,
        password=state.password,
        verbose=state.verbose,
    ):
        state.results[f"{result.hostname}:{result.port}"] = result
        _print_result(result)
        ok += int(result.ok)
        fail += int(not result.ok)
    elapsed = time.monotonic() - start
    print(f"\n  Summary: {ok} ok, {fail} failed in {elapsed:.2f}s")


def action_read_many_async(state: TestAppState) -> None:
    """Read profiles for every cached camera via :func:`read_camera_profiles_async`."""
    print("\n-- read_camera_profiles_async (async) over cached cameras --")

    if not _ensure_credentials(state):
        return
    if not state.cameras:
        print("  No cached cameras. Run discovery first (option 2 or 3).")
        return

    async def _run() -> tuple[int, int]:
        ok = fail = 0
        async for result in read_camera_profiles_async(
            state.cameras,
            username=state.username,
            password=state.password,
            verbose=state.verbose,
        ):
            state.results[f"{result.hostname}:{result.port}"] = result
            _print_result(result)
            ok += int(result.ok)
            fail += int(not result.ok)
        return ok, fail

    start = time.monotonic()
    ok, fail = asyncio.run(_run())
    elapsed = time.monotonic() - start
    print(f"\n  Summary: {ok} ok, {fail} failed in {elapsed:.2f}s")


def action_full_flow(state: TestAppState) -> None:
    """End-to-end: async discovery piped straight into async profile reading.

    Demonstrates that :func:`read_camera_profiles_async` accepts the async
    generator from :func:`discover_onvif_cameras_async` directly, so
    profiles start being fetched as soon as the first camera is found.
    """
    print("\n-- Full flow: discover_onvif_cameras_async → read_camera_profiles_async --")

    if not _ensure_credentials(state):
        return

    discovered: list[dict] = []

    async def _forward():
        """Wrap discovery so we can also cache each camera as it arrives."""
        async for cam in discover_onvif_cameras_async(state.verbose):
            discovered.append(cam)
            print(f"  discovered: {cam}")
            yield cam

    async def _run() -> tuple[int, int]:
        ok = fail = 0
        async for result in read_camera_profiles_async(
            _forward(),
            username=state.username,
            password=state.password,
            verbose=state.verbose,
        ):
            state.results[f"{result.hostname}:{result.port}"] = result
            _print_result(result)
            ok += int(result.ok)
            fail += int(not result.ok)
        return ok, fail

    start = time.monotonic()
    ok, fail = asyncio.run(_run())
    elapsed = time.monotonic() - start

    state.cameras = discovered
    _print_cameras(discovered, f"Discovered {len(discovered)} camera(s)")
    print(f"\n  Summary: {ok} ok, {fail} failed in {elapsed:.2f}s")


# ---------------------------------------------------------------------------
# RTSP playback helpers
# ---------------------------------------------------------------------------


_PLAYERS = ("ffplay", "mpv", "gst-launch-1.0", "cvlc", "vlc")


def _inject_credentials(url: str, user: str, password: str) -> str:
    """Return ``url`` with ``user``/``password`` added if missing.

    Many ONVIF cameras report an RTSP URL without user info even though
    the stream itself requires HTTP Basic / Digest auth. If the URL
    already carries credentials we leave it alone.
    """
    if not user or not password:
        return url
    parsed = urlparse(url)
    if parsed.username or parsed.password:
        return url
    userinfo = f"{quote(user, safe='')}:{quote(password, safe='')}"
    host = parsed.hostname or ""
    netloc = f"{userinfo}@{host}"
    if parsed.port:
        netloc += f":{parsed.port}"
    return urlunparse(parsed._replace(netloc=netloc))


def _build_player_argv(url: str) -> Optional[list[str]]:
    """Return argv for the first RTSP player available in ``PATH``."""
    if shutil.which("ffplay"):
        return [
            "ffplay", "-loglevel", "warning", "-fflags", "nobuffer",
            "-rtsp_transport", "tcp", url,
        ]
    if shutil.which("mpv"):
        return ["mpv", "--rtsp-transport=tcp", url]
    if shutil.which("gst-launch-1.0"):
        return [
            "gst-launch-1.0", "-q",
            "rtspsrc", f"location={url}", "latency=100", "protocols=tcp",
            "!", "decodebin", "!", "videoconvert", "!", "autovideosink",
        ]
    if shutil.which("cvlc"):
        return ["cvlc", "--play-and-exit", "--rtsp-tcp", url]
    if shutil.which("vlc"):
        return ["vlc", "--play-and-exit", "--rtsp-tcp", url]
    return None


def _collect_streams(
    state: TestAppState,
) -> list[tuple[str, int, ONVIFProfile]]:
    """Return ``(camera_id, profile_index, profile)`` for every cached RTSP URL."""
    streams: list[tuple[str, int, ONVIFProfile]] = []
    for camera_id, result in state.results.items():
        if not result.ok:
            continue
        for i, profile in enumerate(result.profiles, 1):
            if profile.rtsp_url:
                streams.append((camera_id, i, profile))
    return streams


def _pick_stream(
    streams: list[tuple[str, int, ONVIFProfile]],
) -> Optional[tuple[str, int, ONVIFProfile]]:
    """Prompt the user to pick one stream when several are available."""
    if len(streams) == 1:
        camera_id, idx, profile = streams[0]
        print(f"  Only one stream cached: {camera_id} profile #{idx}")
        return streams[0]

    rows = []
    for i, (camera_id, idx, profile) in enumerate(streams, 1):
        resolution = "-"
        if profile.vec_resolution:
            resolution = (
                f"{profile.vec_resolution.get('width', '?')}"
                f"x{profile.vec_resolution.get('height', '?')}"
            )
        rows.append([
            str(i),
            camera_id,
            str(idx),
            profile.name or "-",
            profile.vec_encoding or "-",
            resolution,
            (profile.rtsp_url or "-")[:60],
        ])
    _print_table(
        "Available RTSP streams",
        ["#", "camera", "profile#", "name", "codec", "resolution",
         "rtsp_url (trimmed)"],
        rows,
    )
    raw = _prompt("Pick stream by number (blank = cancel)")
    if not raw:
        return None
    try:
        n = int(raw)
    except ValueError:
        print(f"  '{raw}' is not a valid number.")
        return None
    if not 1 <= n <= len(streams):
        print(f"  Index {n} out of range.")
        return None
    return streams[n - 1]


def action_play_rtsp(state: TestAppState) -> None:
    """Play the RTSP stream of a cached profile in an external player.

    Non-blocking: the player is launched as a background process so the
    menu remains interactive. Use option 11 to stop running players
    (they are also terminated automatically on exit).
    """
    print("\n-- Play RTSP stream --")

    finished = state.prune_players()
    for proc, label in finished:
        print(f"  [info] previous player exited: {label} (rc={proc.returncode})")

    streams = _collect_streams(state)
    if not streams:
        print("  No cached profiles with RTSP URL. Run option 6, 7 or 8 first.")
        return

    chosen = _pick_stream(streams)
    if chosen is None:
        return
    camera_id, prof_idx, profile = chosen

    url = _inject_credentials(profile.rtsp_url, state.username, state.password)

    argv = _build_player_argv(url)
    if argv is None:
        print(
            "  [ERROR] No RTSP player found in PATH. Install one of: "
            + ", ".join(_PLAYERS)
            + "."
        )
        print(f"  URL was: {url}")
        return

    if not os.environ.get("DISPLAY") and not os.environ.get("WAYLAND_DISPLAY"):
        print("  [WARN] No DISPLAY / WAYLAND_DISPLAY set; playback may fail on a headless host.")

    print(f"  Camera:  {camera_id}")
    print(f"  Profile: #{prof_idx} '{profile.name}' ({profile.vec_encoding or '?'})")
    print(f"  URL:     {url}")
    print(f"  Player:  {argv[0]}")

    try:
        proc = subprocess.Popen(  # pylint: disable=consider-using-with
            argv,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
            stdin=subprocess.DEVNULL,
            start_new_session=True,
        )
    except FileNotFoundError as exc:
        print(f"  [ERROR] Failed to launch player: {exc}")
        return

    label = f"{camera_id} #{prof_idx} '{profile.name}' ({argv[0]})"
    state.players.append((proc, label))
    print(f"  Launched in background (pid={proc.pid}). Returning to menu.")
    print("  Use option 11 to stop background players.")


def action_stop_players(state: TestAppState) -> None:
    """Terminate every background RTSP player launched via option 10."""
    print("\n-- Stop background RTSP players --")
    finished = state.prune_players()
    for proc, label in finished:
        print(f"  [info] already exited: {label} (rc={proc.returncode})")
    if not state.players:
        print("  No running players.")
        return
    running_labels = [label for _, label in state.players]
    stopped = state.stop_players()
    print(f"  Stopped {stopped} player(s):")
    for label in running_labels:
        print(f"    - {label}")


# ---------------------------------------------------------------------------
# State inspection
# ---------------------------------------------------------------------------


def action_show_state(state: TestAppState) -> None:
    """Dump current in-memory state."""
    print("\n-- Current state --")
    print(f"  username: '{state.username}'")
    print(f"  password: {'*' * len(state.password)} ({len(state.password)} chars)")
    print(f"  verbose:  {state.verbose}")
    _print_cameras(state.cameras, "Cached cameras")

    if not state.results:
        print("\n  No cached profile results.")
        return

    rows = [
        [
            camera_id,
            "ok" if r.ok else "fail",
            str(len(r.profiles)),
            r.mac_address or "-",
            (r.error or "-")[:60],
        ]
        for camera_id, r in state.results.items()
    ]
    _print_table(
        "Cached profile results",
        ["camera", "status", "profiles", "MAC", "error (trimmed)"],
        rows,
    )
    for camera_id, r in state.results.items():
        if r.ok:
            _print_profiles(r.profiles, camera_id)


def action_show_all_details(state: TestAppState) -> None:
    """Print the full set of fields for every cached profile."""
    print("\n-- Full profile details --")
    if not state.results:
        print("  No cached profile results. Read profiles first (options 4-8).")
        return
    for camera_id, r in state.results.items():
        if not r.ok:
            print(f"\n  [FAIL] {camera_id}: {r.error}")
            continue
        mac_info = f"  MAC={r.mac_address}" if r.mac_address else ""
        print(f"\n=== {camera_id} — {len(r.profiles)} profile(s){mac_info} ===")
        for i, profile in enumerate(r.profiles, 1):
            _print_profile_details(i, profile)


# ---------------------------------------------------------------------------
# Main loop
# ---------------------------------------------------------------------------


_MENU = """
===  Sample Application: ONVIF Camera Profiles Library ===

Credentials: user='{user}'  password={pw_len} chars  verbose={verbose}
State:       cached cameras={n_cams}  profile results={n_results}  players={n_players}

  1) Configure credentials / verbose flag
  2) Discover cameras (sync, discovery lib)
  3) Discover cameras (async, discovery lib)
  4) read_camera_profiles       (sync, one camera)
  5) read_camera_profiles_async (async, one camera)
  6) read_camera_profiles       (sync, all cached cameras)
  7) read_camera_profiles_async (async, all cached cameras)
  8) Full flow: async discovery → async profile read (streaming)
  9) Show current state
 10) Show ALL profile details (full field dump)
 11) Play RTSP stream from a cached profile (non-blocking)
 12) Stop all background RTSP players
  0) Exit


  Other samples: 
    - Discovery          : python -m dlstreamer.onvif.samples.discovery_sample
    - PTZ control        : python -m dlstreamer.onvif.samples.ptz_sample
    - Event subscription : python -m dlstreamer.onvif.samples.event_manager_sample
    - Video engine       : python -m dlstreamer.onvif.samples.video_engine_sample

> """


def run() -> None:
    """Start the interactive sample application."""
    state = TestAppState()
    print("Welcome to the ONVIF Camera Profiles Library sample app.")
    print(
        "Tip: set ONVIF_USER / ONVIF_PASSWORD env vars to pre-fill credentials."
    )

    while True:
        state.prune_players()
        prompt = _MENU.format(
            user=state.username or "-",
            pw_len=len(state.password),
            verbose=state.verbose,
            n_cams=len(state.cameras),
            n_results=len(state.results),
            n_players=len(state.players),
        )
        try:
            choice = input(prompt).strip()
        except (EOFError, KeyboardInterrupt):
            print("\nBye.")
            state.stop_players()
            return

        try:
            if choice == "0":
                print("Bye.")
                state.stop_players()
                return
            if choice == "1":
                action_configure(state)
            elif choice == "2":
                action_discover_sync(state)
            elif choice == "3":
                action_discover_async(state)
            elif choice == "4":
                action_read_one(state, use_async=False)
            elif choice == "5":
                action_read_one(state, use_async=True)
            elif choice == "6":
                action_read_many_sync(state)
            elif choice == "7":
                action_read_many_async(state)
            elif choice == "8":
                action_full_flow(state)
            elif choice == "9":
                action_show_state(state)
            elif choice == "10":
                action_show_all_details(state)
            elif choice == "11":
                action_play_rtsp(state)
            elif choice == "12":
                action_stop_players(state)
            else:
                print(f"  Unknown choice: {choice!r}")
        except KeyboardInterrupt:
            print("\n  Interrupted; back to menu.")
        except Exception as exc:  # pylint: disable=broad-exception-caught
            print(f"  [ERROR] {type(exc).__name__}: {exc}")


if __name__ == "__main__":
    run()
