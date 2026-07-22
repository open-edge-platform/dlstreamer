# ==============================================================================
# Copyright (C) 2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================
"""
Interactive text UI exercising :mod:`dlstreamer.onvif.ptz`.

Flow:

1. Use :mod:`dlstreamer.onvif.discovery` to collect ONVIF cameras on
   the local network.
2. Use :mod:`dlstreamer.onvif.camera_profiles` to read media profiles
   from each camera and keep only profiles carrying a PTZ configuration
   (per the ONVIF PTZ Service ver 2.0 WSDL,
   http://www.onvif.org/onvif/ver20/ptz/wsdl/ptz.wsdl).
3. Pick one PTZ-capable profile and enter interactive control mode.
   Arrow keys pan/tilt the camera, ``+``/``-`` zoom, ``p`` starts a
   background RTSP player, ``space`` stops motion, ``q``/``ESC`` returns
   to the menu.

The UI is a plain menu loop using ``input()`` for menu screens and
``termios``/``tty`` for the arrow-key control screen (Unix TTY only).
"""
from __future__ import annotations

import asyncio
import getpass
import os
import select
import shutil
import subprocess
import sys
import termios
import time
import tty
from typing import Iterable, Optional
from urllib.parse import quote, urlparse, urlunparse

from dlstreamer.onvif.discovery import (
    discover_onvif_cameras,
    discover_onvif_cameras_async,
)
from dlstreamer.onvif.camera_profiles import read_camera_profiles
from dlstreamer.onvif.ptz import (
    PTZCapableProfile,
    PTZController,
    PTZVector,
    is_ptz_profile,
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
        self.ptz_profiles: list[PTZCapableProfile] = []
        # Map "camera_id|profile_token" -> rtsp_url, populated during scan.
        self.rtsp_by_key: dict[str, str] = {}
        # Background RTSP players. Each entry is (Popen, label).
        self.players: list[tuple[subprocess.Popen, str]] = []

    def prune_players(self) -> list[tuple[subprocess.Popen, str]]:
        """Drop finished players; return the ones removed."""
        alive: list[tuple[subprocess.Popen, str]] = []
        finished: list[tuple[subprocess.Popen, str]] = []
        for proc, label in self.players:
            if proc.poll() is None:
                alive.append((proc, label))
            else:
                finished.append((proc, label))
        self.players = alive
        return finished

    def stop_players(self, key_filter: Optional[str] = None) -> int:
        """Terminate players. If ``key_filter`` is given, only stop matching ones."""
        self.prune_players()
        if key_filter is None:
            targets = list(self.players)
            self.players = []
        else:
            targets = [(p, l) for p, l in self.players if key_filter in l]
            self.players = [(p, l) for p, l in self.players if key_filter not in l]

        for proc, _ in targets:
            if proc.poll() is None:
                proc.terminate()
        for proc, _ in targets:
            try:
                proc.wait(timeout=2)
            except subprocess.TimeoutExpired:
                proc.kill()
        return sum(1 for p, _ in targets)


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


def _print_ptz_profiles(profiles: list[PTZCapableProfile]) -> None:
    rows = [
        [
            str(i),
            p.camera_id,
            p.profile_token,
            p.profile_name or "-",
            p.ptz_configuration_token or "-",
            p.ptz_node_token or "-",
        ]
        for i, p in enumerate(profiles, 1)
    ]
    _print_table(
        "PTZ-capable profiles",
        ["#", "camera", "profile_token", "profile_name", "ptz_conf", "ptz_node"],
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


def _ensure_credentials(state: TestAppState) -> bool:
    if not state.username or not state.password:
        print("  Credentials are empty; use option 1 to set them first.")
        return False
    return True


# ---------------------------------------------------------------------------
# RTSP playback helpers (mirrors camera_profiles_sample)
# ---------------------------------------------------------------------------


_PLAYERS = ("ffplay", "mpv", "gst-launch-1.0", "cvlc", "vlc")


def _inject_credentials(url: str, user: str, password: str) -> str:
    if not user or not password or not url:
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


def _spawn_player(state: TestAppState, cap: PTZCapableProfile, rtsp_url: str) -> None:
    """Start an RTSP player for ``cap`` in the background."""
    if not rtsp_url:
        print("  [WARN] no cached RTSP URL for this profile")
        return
    url = _inject_credentials(rtsp_url, state.username, state.password)
    argv = _build_player_argv(url)
    if argv is None:
        print(
            "  [ERROR] No RTSP player found in PATH. Install one of: "
            + ", ".join(_PLAYERS)
        )
        print(f"  URL was: {url}")
        return
    if not os.environ.get("DISPLAY") and not os.environ.get("WAYLAND_DISPLAY"):
        print("  [WARN] No DISPLAY / WAYLAND_DISPLAY set; playback may fail on a headless host.")
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
    label = f"{cap.camera_id}|{cap.profile_token} ({argv[0]})"
    state.players.append((proc, label))
    print(f"  Player started (pid={proc.pid}) for {label}")


# ---------------------------------------------------------------------------
# Raw keypress reader (Unix TTY only)
# ---------------------------------------------------------------------------


class _RawKeys:
    """Read single keypresses from stdin in cbreak mode.

    Returns one of ``"UP"``, ``"DOWN"``, ``"LEFT"``, ``"RIGHT"`` for
    arrow keys (ANSI ``ESC [ A/B/C/D``). Bare ESC is returned as
    ``"\\x1b"`` after a short timeout so ESC-to-exit still works.
    Any other key is returned as a single character.
    """

    _ARROWS = {"A": "UP", "B": "DOWN", "C": "RIGHT", "D": "LEFT"}
    _ESC_TIMEOUT = 0.15

    def __init__(self) -> None:
        self._fd = sys.stdin.fileno()
        self._old: Optional[list] = None

    def __enter__(self) -> "_RawKeys":
        if not sys.stdin.isatty():
            raise RuntimeError(
                "Interactive PTZ control requires a TTY (stdin is not a terminal)."
            )
        self._old = termios.tcgetattr(self._fd)
        tty.setcbreak(self._fd)
        return self

    def __exit__(self, exc_type, exc, tb) -> None:
        if self._old is not None:
            termios.tcsetattr(self._fd, termios.TCSADRAIN, self._old)
            self._old = None

    def _readable(self, timeout: float) -> bool:
        rlist, _, _ = select.select([sys.stdin], [], [], timeout)
        return bool(rlist)

    def read(self) -> str:
        ch = sys.stdin.read(1)
        if ch != "\x1b":
            return ch
        # ESC — could be bare ESC or start of an escape sequence
        if not self._readable(self._ESC_TIMEOUT):
            return "\x1b"
        ch2 = sys.stdin.read(1)
        if ch2 != "[":
            return "\x1b"
        if not self._readable(self._ESC_TIMEOUT):
            return "\x1b"
        ch3 = sys.stdin.read(1)
        return self._ARROWS.get(ch3, "\x1b")


# ---------------------------------------------------------------------------
# Interactive control screen
# ---------------------------------------------------------------------------


_CONTROL_HELP = """
Interactive PTZ control (WASD, game-style):

  w / a / s / d ... tilt up / pan left / tilt down / pan right
  + / = ......... zoom in
  - / _ ......... zoom out
  space ......... stop all motion
  h ............. goto home
  H ............. set home (current position)
  i ............. print status snapshot
  p ............. start RTSP player in background
  x ............. stop background players for this profile
  < / > ......... decrease / increase movement magnitude (0.05..1.0)
  1..9 .......... set magnitude to 0.1 .. 0.9
  ? ............. show this help
  q | ESC ....... back to menu
"""


def _run_control_screen(state: TestAppState, cap: PTZCapableProfile) -> None:
    """Interactive WASD control screen for one PTZ profile."""
    print(_CONTROL_HELP)
    profile_key = f"{cap.camera_id}|{cap.profile_token}"
    rtsp_url = state.rtsp_by_key.get(profile_key, "")
    magnitude = 0.5
    duration = 0.5  # ContinuousMove auto-stop timeout, in seconds

    print(
        f"Controlling: {cap.camera_id}  profile='{cap.profile_name}' "
        f"(token={cap.profile_token})"
    )
    if not rtsp_url:
        print("  (no RTSP URL cached for this profile; 'p' will be a no-op)")

    ctrl = PTZController.from_capable_profile(cap, state.username, state.password)

    def _do(name: str, fn, *args, **kwargs) -> None:
        try:
            fn(*args, **kwargs)
            print(f"  {name}: ok")
        except Exception as exc:  # pylint: disable=broad-exception-caught
            print(f"  {name}: [ERROR] {type(exc).__name__}: {exc}")

    def _move(pan: float = 0.0, tilt: float = 0.0, zoom: float = 0.0) -> None:
        vec = PTZVector(pan=pan, tilt=tilt, zoom=zoom)
        _do(
            f"continuous_move pan={pan:+.2f} tilt={tilt:+.2f} zoom={zoom:+.2f} "
            f"timeout={duration:.2f}s",
            ctrl.continuous_move, vec, duration,
        )

    print(f"\nReady. magnitude={magnitude:.2f}, timeout={duration:.2f}s. Press '?' for help.")

    try:
        with _RawKeys() as keys:
            while True:
                key = keys.read()

                if key in ("q", "\x1b"):
                    print("\n  Leaving control mode.")
                    break
                if key == "w":
                    _move(tilt=magnitude)
                elif key == "s":
                    _move(tilt=-magnitude)
                elif key == "d":
                    _move(pan=magnitude)
                elif key == "a":
                    _move(pan=-magnitude)
                elif key in ("+", "="):
                    _move(zoom=magnitude)
                elif key in ("-", "_"):
                    _move(zoom=-magnitude)
                elif key == " ":
                    _do("stop", ctrl.stop)
                elif key == "h":
                    if ctrl.home_supported():
                        _do("goto_home", ctrl.goto_home)
                    else:
                        print("  goto_home: not supported by this camera (HomeSupported=False)")
                elif key == "H":
                    if ctrl.home_supported():
                        _do("set_home", ctrl.set_home)
                    else:
                        print("  set_home: not supported by this camera (HomeSupported=False)")
                elif key == "i":
                    try:
                        status = ctrl.get_status()
                        print(
                            f"  status: pos=(pan={status.position.pan:+.3f}, "
                            f"tilt={status.position.tilt:+.3f}, "
                            f"zoom={status.position.zoom:+.3f}) "
                            f"pan_tilt={status.pan_tilt_move_status or '-'} "
                            f"zoom={status.zoom_move_status or '-'}"
                        )
                    except Exception as exc:  # pylint: disable=broad-exception-caught
                        print(f"  get_status: [ERROR] {type(exc).__name__}: {exc}")
                elif key == "p":
                    _spawn_player(state, cap, rtsp_url)
                elif key == "x":
                    stopped = state.stop_players(key_filter=profile_key)
                    print(f"  stopped {stopped} background player(s) for this profile.")
                elif key == "<":
                    magnitude = max(0.05, round(magnitude - 0.1, 2))
                    print(f"  magnitude = {magnitude:.2f}")
                elif key == ">":
                    magnitude = min(1.0, round(magnitude + 0.1, 2))
                    print(f"  magnitude = {magnitude:.2f}")
                elif key in "123456789":
                    magnitude = int(key) / 10.0
                    print(f"  magnitude = {magnitude:.2f}")
                elif key == "?":
                    print(_CONTROL_HELP)
                # anything else: ignore
    except RuntimeError as exc:
        print(f"  [ERROR] {exc}")
    finally:
        ctrl.close()


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


def _scan_ptz_profiles_sync(state: TestAppState, cameras: list[dict]) -> None:
    """Read profiles from ``cameras`` via camera_profiles, keep PTZ-capable ones."""
    ptz_profiles: list[PTZCapableProfile] = []
    rtsp_map: dict[str, str] = {}
    ok = fail = 0
    for res in read_camera_profiles(
        cameras,
        username=state.username,
        password=state.password,
        verbose=state.verbose,
    ):
        if not res.ok:
            print(f"  [FAIL] {res.hostname}:{res.port}: {res.error}")
            fail += 1
            continue
        ok += 1
        for profile in res.profiles:
            if not is_ptz_profile(profile):
                continue
            cap = PTZCapableProfile(
                hostname=res.hostname,
                port=res.port,
                profile_token=profile.token,
                profile_name=profile.name,
                ptz_configuration_token=profile.ptz_token,
                ptz_node_token=profile.ptz_node_token,
            )
            ptz_profiles.append(cap)
            rtsp_map[f"{cap.camera_id}|{cap.profile_token}"] = profile.rtsp_url or ""

    state.ptz_profiles = ptz_profiles
    state.rtsp_by_key = rtsp_map
    print(f"  Cameras: {ok} ok / {fail} failed. PTZ-capable profiles: {len(ptz_profiles)}.")
    if ptz_profiles:
        _print_ptz_profiles(ptz_profiles)


def action_scan_sync(state: TestAppState) -> None:
    """Discover cameras (sync) and scan for PTZ-capable profiles."""
    print("\n-- Discover (sync) + scan PTZ-capable profiles --")
    if not _ensure_credentials(state):
        return

    start = time.monotonic()
    cameras = list(discover_onvif_cameras(state.verbose))
    elapsed = time.monotonic() - start
    state.cameras = cameras
    _print_cameras(cameras, f"Discovered {len(cameras)} camera(s) in {elapsed:.2f}s")
    if not cameras:
        return

    _scan_ptz_profiles_sync(state, cameras)


def action_scan_async(state: TestAppState) -> None:
    """Discover cameras (async, streaming) and scan for PTZ-capable profiles."""
    print("\n-- Discover (async) + scan PTZ-capable profiles --")
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
    _print_cameras(discovered, f"Discovered {len(discovered)} camera(s) in {elapsed:.2f}s")
    if not discovered:
        return

    _scan_ptz_profiles_sync(state, discovered)


def action_control(state: TestAppState) -> None:
    """Pick a PTZ profile and enter the arrow-key control screen."""
    print("\n-- Control a PTZ profile --")
    if not _ensure_credentials(state):
        return
    if not state.ptz_profiles:
        print("  No PTZ-capable profiles cached. Run option 2 or 3 first.")
        return

    _print_ptz_profiles(state.ptz_profiles)
    raw = _prompt("Pick profile by number (blank = cancel)")
    if not raw:
        return
    try:
        n = int(raw)
    except ValueError:
        print(f"  '{raw}' is not a valid number.")
        return
    if not 1 <= n <= len(state.ptz_profiles):
        print(f"  Index {n} out of range.")
        return

    _run_control_screen(state, state.ptz_profiles[n - 1])


def action_show_ptz_cameras(state: TestAppState) -> None:
    """List cached cameras that have at least one PTZ-capable profile."""
    print("\n-- PTZ-capable cameras --")
    if not state.ptz_profiles:
        print("  No PTZ-capable profiles cached. Run option 2 or 3 first.")
        return

    by_camera: dict[str, list[PTZCapableProfile]] = {}
    for prof in state.ptz_profiles:
        by_camera.setdefault(prof.camera_id, []).append(prof)

    rows = []
    for i, camera_id in enumerate(sorted(by_camera), 1):
        profiles = by_camera[camera_id]
        names = ", ".join(p.profile_name or p.profile_token for p in profiles)
        rows.append([str(i), camera_id, str(len(profiles)), names])
    _print_table(
        f"PTZ-capable cameras ({len(by_camera)} total, "
        f"{len(state.ptz_profiles)} profile(s))",
        ["#", "camera", "ptz_profiles", "profile names"],
        rows,
    )
    _print_ptz_profiles(state.ptz_profiles)


def action_show_state(state: TestAppState) -> None:
    """Dump current in-memory state."""
    print("\n-- Current state --")
    print(f"  username: '{state.username}'")
    print(f"  password: {'*' * len(state.password)} ({len(state.password)} chars)")
    print(f"  verbose:  {state.verbose}")
    _print_cameras(state.cameras, "Cached cameras")
    _print_ptz_profiles(state.ptz_profiles)

    state.prune_players()
    if not state.players:
        print("\n  No background RTSP players.")
        return
    rows = [
        [str(i), label, str(proc.pid), "running" if proc.poll() is None else "exited"]
        for i, (proc, label) in enumerate(state.players, 1)
    ]
    _print_table(
        "Background RTSP players",
        ["#", "label", "pid", "status"],
        rows,
    )


def action_stop_all_players(state: TestAppState) -> None:
    """Terminate every background RTSP player."""
    print("\n-- Stop all background RTSP players --")
    finished = state.prune_players()
    for proc, label in finished:
        print(f"  [info] already exited: {label} (rc={proc.returncode})")
    if not state.players:
        print("  No running players.")
        return
    labels = [label for _, label in state.players]
    stopped = state.stop_players()
    print(f"  Stopped {stopped} player(s):")
    for label in labels:
        print(f"    - {label}")


# ---------------------------------------------------------------------------
# Main loop
# ---------------------------------------------------------------------------


_MENU = """
===  Sample Application: ONVIF PTZ Library ===

Credentials: user='{user}'  password={pw_len} chars  verbose={verbose}
State:       cameras={n_cams}  ptz_profiles={n_ptz}  players={n_players}

  1) Configure credentials / verbose flag
  2) Discover cameras (sync)  + scan PTZ-capable profiles
  3) Discover cameras (async) + scan PTZ-capable profiles
  4) Control a PTZ profile   (WASD, non-blocking RTSP)
  5) Show PTZ-capable cameras (grouped by camera)
  6) Show current state
  7) Stop all background RTSP players
  0) Exit

Other samples: 
    - Discovery          : python -m dlstreamer.onvif.samples.discovery_sample
    - Profile querying   : python -m dlstreamer.onvif.samples.camera_profiles_sample
    - Event subscription : python -m dlstreamer.onvif.samples.event_manager_sample
    - Video engine       : python -m dlstreamer.onvif.samples.video_engine_sample
> """


def run() -> None:
    """Start the interactive sample application."""
    state = TestAppState()
    print("Welcome to the ONVIF PTZ Library sample app.")
    print("Tip: set ONVIF_USER / ONVIF_PASSWORD env vars to pre-fill credentials.")

    while True:
        state.prune_players()
        prompt = _MENU.format(
            user=state.username or "-",
            pw_len=len(state.password),
            verbose=state.verbose,
            n_cams=len(state.cameras),
            n_ptz=len(state.ptz_profiles),
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
                action_scan_sync(state)
            elif choice == "3":
                action_scan_async(state)
            elif choice == "4":
                action_control(state)
            elif choice == "5":
                action_show_ptz_cameras(state)
            elif choice == "6":
                action_show_state(state)
            elif choice == "7":
                action_stop_all_players(state)
            else:
                print(f"  Unknown choice: {choice!r}")
        except KeyboardInterrupt:
            print("\n  Interrupted; back to menu.")
        except Exception as exc:  # pylint: disable=broad-exception-caught
            print(f"  [ERROR] {type(exc).__name__}: {exc}")


if __name__ == "__main__":
    run()
