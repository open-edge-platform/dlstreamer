# ==============================================================================
# Copyright (C) 2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================
"""ONVIF discovery + pipeline orchestration sample built on current APIs.

This sample uses modern VideoEngine APIs:
- ``dlstreamer.onvif.discovery.discover_onvif_cameras_async``
- ``dlstreamer.onvif.video_engine.VideoEngine``
"""

from __future__ import annotations

import argparse
import asyncio
import json
import os
import sys
from pathlib import Path
from typing import Any

try:
    from dlstreamer.onvif.discovery import discover_onvif_cameras_async
    from dlstreamer.onvif.video_engine import VideoEngineEvent, create_video_engine
except ModuleNotFoundError:
    # Allow running directly from a source checkout without installing a wheel.
    repo_root = Path(__file__).resolve().parents[4]
    sys.path.insert(0, str(repo_root / "python"))
    from dlstreamer.onvif.discovery import discover_onvif_cameras_async
    from dlstreamer.onvif.video_engine import VideoEngineEvent, create_video_engine


def parse_args() -> argparse.Namespace:
    """Parse command-line arguments for the ONVIF discovery orchestrator."""
    parser = argparse.ArgumentParser(
        description="ONVIF Camera Async Discovery and DL Streamer Pipeline Launcher"
    )
    parser.add_argument(
        "--username",
        type=str,
        default=os.environ.get("ONVIF_USER"),
        help="ONVIF camera username (or set ONVIF_USER env var)",
    )
    parser.add_argument(
        "--password",
        type=str,
        default=os.environ.get("ONVIF_PASSWORD"),
        help="ONVIF camera password (or set ONVIF_PASSWORD env var)",
    )
    parser.add_argument(
        "--refresh-rate",
        type=int,
        default=60,
        help="Seconds between discovery cycles (default: 60)",
    )
    parser.add_argument(
        "--config-file",
        type=str,
        default="config.json",
        help="Path to pipeline configuration JSON file (default: config.json)",
    )
    parser.add_argument(
        "--verbose",
        action="store_true",
        default=False,
        help="Print detailed profile and engine event information",
    )

    args = parser.parse_args()

    if not args.username or not args.password:
        print(
            "Error: ONVIF username and password must be provided via command-line "
            "arguments or environment variables."
        )
        parser.print_help()
        sys.exit(1)

    if args.refresh_rate < 1:
        print("Error: --refresh-rate must be >= 1")
        sys.exit(2)

    return args


def _normalize_definition(definition: str) -> str:
    """Build a full gst-launch command from a config pipeline fragment."""
    tail = definition.strip()

    if "{rtsp_url}" in tail:
        return tail

    if tail.startswith("gst-launch-1.0"):
        return tail

    if tail.startswith("!"):
        return f"gst-launch-1.0 -e rtspsrc location={{rtsp_url}} protocols=tcp latency=100 {tail}"

    return f"gst-launch-1.0 -e rtspsrc location={{rtsp_url}} protocols=tcp latency=100 ! {tail}"


def _load_json(path: Path) -> Any:
    with path.open("r", encoding="utf-8") as file:
        return json.load(file)


def _is_new_engine_schema(payload: Any) -> bool:
    if isinstance(payload, list):
        return True
    if isinstance(payload, dict) and "pipelines" in payload:
        return True
    return False


def _apply_legacy_config(engine: Any, payload: dict[str, Any]) -> int:
    """Apply legacy config format used by the original sample.

    Expected shape:
    {
      "verbose": true,
      "camera_name": {
        "hostname": "...",
        "port": 8080,
        "definition": " ! ..."
      }
    }
    """
    count = 0
    for binding_id, item in payload.items():
        if binding_id == "verbose":
            continue
        if not isinstance(item, dict):
            continue

        hostname = item.get("hostname")
        port = item.get("port")
        definition = item.get("definition")
        profile_name = item.get("profile_name")

        if not hostname or port is None or not definition:
            continue

        pipeline = _normalize_definition(str(definition))
        engine.set_camera_pipeline(
            str(hostname),
            int(port),
            pipeline,
            binding_id=str(binding_id),
            persist=False,
        )

        if profile_name:
            engine.replace_pipeline(
                str(binding_id),
                pipeline,
                profile_name=str(profile_name),
            )

        count += 1

    return count


def _print_event(event: VideoEngineEvent) -> None:
    camera = event.camera.key()
    details = ", ".join(f"{key}={value}" for key, value in event.details.items())
    if details:
        print(f"[engine] {event.kind:<18} camera={camera} {details}")
    else:
        print(f"[engine] {event.kind:<18} camera={camera}")


async def _initial_probe(verbose: bool) -> None:
    """Run one low-level async probe so users can see immediate camera hits."""
    print("[probe] Running initial discover_onvif_cameras_async() sweep...")
    found = 0
    async for camera in discover_onvif_cameras_async(verbose):
        found += 1
        print(f"[probe] found camera {camera}")
    print(f"[probe] sweep done, discovered {found} camera(s)")


async def main(args: argparse.Namespace) -> None:
    config_path = Path(args.config_file)
    if not config_path.exists():
        raise FileNotFoundError(f"Config file not found: {config_path}")

    payload = _load_json(config_path)
    config_verbose = bool(payload.get("verbose", False)) if isinstance(payload, dict) else False
    effective_verbose = bool(args.verbose or config_verbose)

    await _initial_probe(effective_verbose)

    engine = create_video_engine(
        discovery_time=int(args.refresh_rate),
        timeout=max(120, int(args.refresh_rate) * 2),
        verbose=effective_verbose,
    )
    engine.set_default_credentials(str(args.username), str(args.password))
    engine.register_callback(_print_event)

    bindings_count = 0
    if _is_new_engine_schema(payload):
        engine.load_config(config_path)
        bindings_count = len(engine.list_camera_pipeline_pairs())
        print(f"[config] loaded as VideoEngine schema from {config_path} ({bindings_count} binding(s))")
    elif isinstance(payload, dict):
        bindings_count = _apply_legacy_config(engine, payload)
        print(f"[config] loaded legacy schema from {config_path} ({bindings_count} binding(s))")
    else:
        raise ValueError("Unsupported config format. Expected dict or list JSON.")

    if bindings_count == 0:
        print("[warn] no valid camera bindings loaded from config")

    print(
        "[engine] starting discovery loop "
        f"(refresh-rate={args.refresh_rate}s, verbose={effective_verbose})"
    )

    engine.start()
    try:
        while True:
            await asyncio.sleep(max(1, int(args.refresh_rate)))
            active_cameras = engine.get_active_cameras()
            active_pipelines = engine.get_active_pipelines()
            print(
                "[state] "
                f"active_cameras={len(active_cameras)} "
                f"active_pipelines={len(active_pipelines)}"
            )
    finally:
        print("[engine] stopping and cleaning up...")
        engine.destroy()


if __name__ == "__main__":
    cli_args = parse_args()
    try:
        asyncio.run(main(cli_args))
    except KeyboardInterrupt:
        print("\nDiscovery stopped by user.")
    except Exception as exc:  # pylint: disable=broad-exception-caught
        print(f"Fatal error: {type(exc).__name__}: {exc}")
        sys.exit(1)
