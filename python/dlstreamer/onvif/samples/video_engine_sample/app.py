# ==============================================================================
# Copyright (C) 2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================
"""Interactive table-based UI for :mod:`dlstreamer.onvif.video_engine`."""

from __future__ import annotations

import argparse
import getpass
import json
import os
from dataclasses import asdict
from datetime import datetime, timezone
from pathlib import Path
from typing import Any, Iterable, Optional

from dlstreamer.onvif.camera_profiles import read_camera_profiles
from dlstreamer.onvif.video_engine import (
    DynamicPipelineController,
    VideoEngine,
    VideoEngineEvent,
    create_dynamic_controller,
    create_video_engine,
)


DEFAULT_CONFIG_PATH = Path(__file__).parent / "video_engine_pipelines.json"
DEFAULT_RULES_PATH = Path(__file__).parent / "video_engine_event_mapping.json"
DEFAULT_BINDINGS_PATH = Path(__file__).parent / "video_engine_static_mapping.json"
REQUIRED_ENV_VARS = [
    ("ONVIF_USER", "required", "ONVIF authentication username"),
    ("ONVIF_PASSWORD", "required", "ONVIF authentication password"),
]
OPTIONAL_ENV_VARS = [
    ("DISPLAY", "optional", "Useful for GUI media players"),
    ("WAYLAND_DISPLAY", "optional", "Useful for Wayland GUI media players"),
    ("PYTHONPATH", "optional", "Helpful when running from a source checkout"),
    ("DLSTREAMER_INSTALL_PREFIX", "optional", "Useful when using an installed runtime"),
]


class TestAppState:
    """Mutable UI state for the interactive demo application."""

    def __init__(self, config_path: str | Path = DEFAULT_CONFIG_PATH) -> None:
        # Try to find config file: first in app directory, then in current working directory
        self.config_path = Path(config_path)
        if not self.config_path.exists():
            cwd_config = Path.cwd() / "video_engine_pipelines.json"
            if cwd_config.exists():
                self.config_path = cwd_config
        self.engine: VideoEngine = create_video_engine(verbose=True)
        self.controller: DynamicPipelineController = create_dynamic_controller(self.engine, verbose=True)
        self.rules_path = DEFAULT_RULES_PATH
        self.bindings_path = DEFAULT_BINDINGS_PATH
        self.bindings_loaded = False
        self.pipeline_library: dict[str, Any] = {}
        if self.config_path.exists():
            self.pipeline_library = self.controller.load_pipeline_library(self.config_path)
        self.username = os.environ.get("ONVIF_USER", "")
        self.password = os.environ.get("ONVIF_PASSWORD", "")
        self.engine.set_default_credentials(self.username, self.password)
        self.controller.set_default_credentials(self.username, self.password)
        self.dynamic_active = False
        self.logs: list[dict[str, Any]] = []
        self.discovery_active = False
        self.engine.register_callback(self._on_engine_event)

    def close(self) -> None:
        if self.dynamic_active:
            self.controller.stop()
            self.dynamic_active = False
        self.engine.destroy()
        self.discovery_active = False

    def _on_engine_event(self, event: VideoEngineEvent) -> None:
        self.log_event("INFO", event.kind, event.camera.key(), event.details)

    def log_event(
        self,
        level: str,
        kind: str,
        camera: str,
        details: dict[str, Any],
        *,
        message: str | None = None,
    ) -> None:
        self.logs.append(
            {
                "time": datetime.now(timezone.utc).isoformat(timespec="seconds"),
                "level": level,
                "kind": kind,
                "camera": camera,
                "message": message or kind,
                "details": details,
            }
        )


def _render_table(title: str, headers: list[str], rows: Iterable[list[str]]) -> str:
    rows = list(rows)
    lines = [title]
    if not rows:
        lines.append("  (no rows)")
        return "\n".join(lines)

    widths = [max(len(header), *(len(row[index]) for row in rows)) for index, header in enumerate(headers)]
    separator = "+-" + "-+-".join("-" * width for width in widths) + "-+"
    lines.append(separator)
    lines.append("| " + " | ".join(header.ljust(width) for header, width in zip(headers, widths)) + " |")
    lines.append(separator)
    for row in rows:
        lines.append("| " + " | ".join(value.ljust(width) for value, width in zip(row, widths)) + " |")
    lines.append(separator)
    return "\n".join(lines)


def _rows_for_pairs(pairs: list[dict[str, Any]]) -> list[list[str]]:
    rows: list[list[str]] = []
    for index, item in enumerate(pairs, 1):
        camera = item.get("camera", {})
        rows.append(
            [
                str(index),
                str(item.get("binding_id", "-")),
                f"{camera.get('hostname', '-')}:{camera.get('port', '-')}",
                str(camera.get("mac", "-")),
                json.dumps(item.get("pipeline", ""))[:80],
                ", ".join(item.get("events", [])) or "-",
            ]
        )
    return rows


def _rows_for_pairs_with_profile(pairs: list[dict[str, Any]]) -> list[list[str]]:
    rows: list[list[str]] = []
    for index, item in enumerate(pairs, 1):
        camera = item.get("camera", {})
        rows.append(
            [
                str(index),
                str(item.get("binding_id", "-")),
                f"{camera.get('hostname', '-')}:{camera.get('port', '-')}",
                str(camera.get("mac", "-")),
                str(item.get("profile_name") or "-"),
                json.dumps(item.get("pipeline", ""))[:80],
                ", ".join(item.get("events", [])) or "-",
            ]
        )
    return rows


def _rows_for_cameras(cameras: list[dict[str, Any]]) -> list[list[str]]:
    rows: list[list[str]] = []
    for index, item in enumerate(cameras, 1):
        camera = item.get("camera", {})
        rows.append(
            [
                str(index),
                f"{camera.get('hostname', '-')}:{camera.get('port', '-')}"
                + (f" ({camera.get('mac')})" if camera.get("mac") else ""),
                str(item.get("status", "-")),
                str(item.get("last_seen", "-"))[:19],
                ", ".join(item.get("pipeline_binding_ids", [])) or "-",
                str(item.get("error", "-") or "-"),
            ]
        )
    return rows


def _rows_for_pipelines(pipelines: list[dict[str, Any]]) -> list[list[str]]:
    rows: list[list[str]] = []
    for index, item in enumerate(pipelines, 1):
        rows.append([str(index), str(item.get("binding_key", "-")), str(item.get("pid", "-"))])
    return rows


def _rows_for_pipeline_library(library: dict[str, Any]) -> list[list[str]]:
    rows: list[list[str]] = []
    for index, (pipeline_id, pipeline) in enumerate(library.items(), 1):
        if isinstance(pipeline, list):
            command = " ".join(str(token) for token in pipeline)
        else:
            command = str(pipeline)
        rows.append([str(index), str(pipeline_id), command[:120]])
    return rows


def _rows_for_events(events: list[dict[str, Any]]) -> list[list[str]]:
    rows: list[list[str]] = []
    for index, item in enumerate(events, 1):
        camera = item.get("camera", {})
        rows.append(
            [
                str(index),
                str(item.get("name", "-")),
                f"{camera.get('hostname', '-')}:{camera.get('port', '-')}"
                + (f" ({camera.get('mac')})" if camera.get("mac") else ""),
                str(item.get("topic_filter", "-") or "-"),
                str(item.get("renew_every", "-") or "-"),
            ]
        )
    return rows


def _rows_for_rules(rules: list[Any]) -> list[list[str]]:
    rows: list[list[str]] = []
    for index, rule in enumerate(rules, 1):
        trigger = rule.trigger
        parts: list[str] = []
        if trigger.topic_contains:
            parts.append(f"topic~{trigger.topic_contains}")
        if trigger.property_operation:
            parts.append(f"op={trigger.property_operation}")
        if trigger.data_equals:
            parts.append(",".join(f"{key}={value}" for key, value in trigger.data_equals.items()))
        rows.append(
            [
                str(index),
                str(rule.name),
                rule.camera.key(),
                str(rule.camera.hostname),
                rule.action.value,
                str(rule.target_binding_id),
                "; ".join(parts) or "-",
            ]
        )
    return rows


def _rows_for_profiles(hostname: str, port: int, profiles: list[Any]) -> list[list[str]]:
    rows: list[list[str]] = []
    for index, profile in enumerate(profiles, 1):
        resolution = profile.vec_resolution or {}
        width = str(resolution.get("Width", resolution.get("width", "-")))
        height = str(resolution.get("Height", resolution.get("height", "-")))
        resolution_str = f"{width}x{height}" if width != "-" else "-"
        rows.append(
            [
                str(index),
                f"{hostname}:{port}",
                str(profile.name or "-"),
                str(profile.token or "-"),
                str(profile.vec_encoding or "-"),
                resolution_str,
                str(profile.rtsp_url or "-"),
            ]
        )
    return rows


def _rows_for_logs(logs: list[dict[str, Any]]) -> list[list[str]]:
    rows: list[list[str]] = []
    for index, entry in enumerate(logs, 1):
        rows.append(
            [
                str(index),
                str(entry.get("time", "-")),
                str(entry.get("level", "-")),
                str(entry.get("kind", "-")),
                str(entry.get("camera", "-")),
                str(entry.get("message", "-")),
            ]
        )
    return rows


def _rows_for_environment() -> list[list[str]]:
    rows: list[list[str]] = []
    for index, (name, requirement, description) in enumerate(REQUIRED_ENV_VARS + OPTIONAL_ENV_VARS, 1):
        value = os.environ.get(name)
        if name.endswith("PASSWORD") and value:
            rendered_value = "***"
        elif value:
            rendered_value = value
        else:
            rendered_value = "-"
        status = "set" if value else ("missing" if requirement == "required" else "unset")
        rows.append([str(index), name, requirement, status, rendered_value[:60], description])
    return rows


def _camera_label(camera: dict[str, Any]) -> str:
    hostname = str(camera.get("hostname", camera.get("ip", "-")))
    port = str(camera.get("port", "-"))
    mac = str(camera.get("mac", camera.get("mac_address", "")) or "")
    if mac:
        return f"{hostname}:{port} ({mac})"
    return f"{hostname}:{port}"


def _rows_for_json_camera(camera: dict[str, Any], entry: dict[str, Any], index: int) -> list[list[str]]:
    pipeline_value = entry.get("pipeline")
    if pipeline_value is None:
        # Reference-based binding: show the pipeline id it points at.
        ref = entry.get("pipeline_ref")
        pipeline_cell = f"pipeline_ref: {ref}" if ref else "-"
    elif isinstance(pipeline_value, list):
        pipeline_cell = " | ".join(str(item) for item in pipeline_value)
    else:
        pipeline_cell = str(pipeline_value)

    return [
        [str(index), "camera", _camera_label(camera)],
        [str(index), "binding_id", str(entry.get("binding_id", "-"))],
        [str(index), "pipeline", pipeline_cell[:120]],
        [str(index), "events", ", ".join(str(item) for item in entry.get("events", [])) or "-"],
    ]


def build_json_report(config_path: str | Path) -> str:
    """Return the camera-pipeline mapping from the JSON config as tables.

    Renders one table per camera for the camera-pipeline mapping schema
    (a list of entries, each carrying a ``camera``).

    If the file is a pipeline library (a mapping ``id -> pipeline``),
    this option does not render it — it points the user to the dedicated
    "Show loaded pipeline library" option instead, to avoid duplication.

    The source file name is always shown in the header.
    """

    path = Path(config_path)
    payload = json.loads(path.read_text(encoding="utf-8"))
    if isinstance(payload, dict):
        section = payload.get("pipelines", payload)
    elif isinstance(payload, list):
        section = payload
    else:
        raise ValueError("JSON config must be a list or a mapping with a pipelines key")

    lines = [f"JSON configuration source: {path}"]

    # Pipeline library schema: not this option's responsibility.
    if isinstance(section, dict):
        lines.append("")
        lines.append("Schema: pipeline library (id -> pipeline)")
        lines.append("  This file is a pipeline library, not a camera-pipeline mapping.")
        lines.append("  Use option 'Show loaded pipeline library' to view its contents.")
        return "\n".join(lines)

    # Camera-pipeline mapping schema: list of entries carrying a camera.
    grouped: dict[str, list[dict[str, Any]]] = {}
    for entry in section:
        if not isinstance(entry, dict):
            continue
        camera = entry.get("camera", entry)
        hostname = str(camera.get("hostname", camera.get("ip", "")))
        port = str(camera.get("port", ""))
        mac = str(camera.get("mac", camera.get("mac_address", "")) or "").upper()
        group_key = f"{hostname}:{port}:{mac}" if mac else f"{hostname}:{port}"
        grouped.setdefault(group_key, []).append(entry)

    lines.append("")
    lines.append("Schema: camera-pipeline mapping")
    if not grouped:
        lines.append("  (no cameras)")
        return "\n".join(lines)

    for camera_index, (group_key, camera_entries) in enumerate(sorted(grouped.items()), 1):
        first_entry = camera_entries[0]
        camera = first_entry.get("camera", first_entry)
        rows: list[list[str]] = []
        for entry in camera_entries:
            rows.extend(_rows_for_json_camera(camera, entry, camera_index))
        lines.append("")
        lines.append(_render_table(f"Camera {camera_index}: {group_key}", ["#", "field", "value"], rows))

    return "\n".join(lines)


def build_environment_report() -> str:
    """Return the environment-variable status table as plain text."""

    return _render_table(
        "Environment status",
        ["#", "variable", "required", "status", "value", "description"],
        _rows_for_environment(),
    )


def build_dump_report(state: TestAppState) -> str:
    """Return a full table-based snapshot of the current engine state."""

    lines = [
        "video_engine sample app snapshot",
        f"config: {state.config_path}",
        f"discovery_time: {state.engine.getDiscoveryTime()} sec",
        f"timeout: {state.engine.getTimeout()} sec",
        f"discovery_running: {state.discovery_active}",
    ]

    lines.append("")
    lines.append(_render_table("Configured camera-pipeline pairs", ["#", "binding_id", "camera", "mac", "pipeline", "events"], _rows_for_pairs(state.engine.list_camera_pipeline_pairs())))

    lines.append("")
    lines.append(_render_table("Active cameras", ["#", "camera", "status", "last_seen", "pipelines", "error"], _rows_for_cameras(state.engine.get_active_cameras())))

    lines.append("")
    lines.append(_render_table("Active pipelines", ["#", "binding_key", "pid"], _rows_for_pipelines(state.engine.get_active_pipelines())))

    lines.append("")
    all_events: list[dict[str, Any]] = []
    for pair in state.engine.list_camera_pipeline_pairs():
        camera = pair.get("camera", {})
        all_events.extend(state.engine.list_camera_events(str(camera.get("hostname", "")), int(camera.get("port", 0)), camera.get("mac")))
    lines.append(_render_table("Configured ONVIF events", ["#", "name", "camera", "topic_filter", "renew_every"], _rows_for_events(all_events)))

    lines.append("")
    lines.append(f"dynamic_control_running: {state.dynamic_active}")
    lines.append(_render_table("Dynamic event->action rules", ["#", "name", "camera", "ip", "action", "target_binding_id", "trigger"], _rows_for_rules(state.controller.list_rules())))

    lines.append("")
    lines.append(_render_table("Logs", ["#", "time", "level", "kind", "camera", "message"], _rows_for_logs(state.logs)))

    lines.append("")
    lines.append(build_environment_report())

    lines.append("")
    lines.append(build_json_report(state.bindings_path))

    return "\n".join(lines)


def _prompt(prompt: str, default: Optional[str] = None) -> str:
    suffix = f" [{default}]" if default is not None else ""
    try:
        value = input(f"{prompt}{suffix}: ").strip()
    except KeyboardInterrupt:
        print()
        return "__cancel__"
    return value or (default or "")


def _prompt_int(prompt: str, default: int) -> int:
    while True:
        raw = _prompt(prompt, str(default))
        try:
            return int(raw)
        except ValueError:
            print(f"  '{raw}' is not a valid integer.")


def _prompt_bool(prompt: str, default: bool = False) -> bool:
    value = _prompt(f"{prompt} (y/n)", "y" if default else "n").lower()
    return value in {"y", "yes", "1", "true"}


def _prompt_list(prompt: str, default: str = "") -> list[str]:
    raw = _prompt(prompt, default)
    if not raw:
        return []
    return [item.strip() for item in raw.split(",") if item.strip()]


def _action_load_config(state: TestAppState) -> None:
    path = _prompt("Pipeline library file", str(state.config_path))
    state.config_path = Path(path)
    library = state.controller.load_pipeline_library(state.config_path)
    state.pipeline_library = library
    state.log_event("INFO", "load_pipeline_library", "local", {"config_path": str(state.config_path), "count": len(library)}, message="pipeline library loaded")
    print(f"Pipeline library loaded ({len(library)} pipeline(s)): {', '.join(library)}")


def _action_show_pipeline_library(state: TestAppState) -> None:
    state.log_event("INFO", "show_pipeline_library", "local", {"count": len(state.pipeline_library)}, message="pipeline library inspected")
    print(_render_table(
        "Loaded pipeline library",
        ["#", "pipeline_id", "command"],
        _rows_for_pipeline_library(state.pipeline_library),
    ))


def _action_load_bindings(state: TestAppState) -> None:
    """Load static camera-pipeline bindings from a JSON file (enables them).

    Uses :meth:`VideoEngine.load_config`. Each entry carries a ``camera``
    identity and references a pipeline by id (``pipeline_ref``) resolved
    against the loaded pipeline library — the single source of pipeline
    definitions (``video_engine_pipelines.json``). An inline ``pipeline`` is
    still accepted for standalone configs. Once loaded and discovery is
    started (option "Start discovery..."), matching cameras auto-launch their
    bound pipelines.
    """
    path = _prompt("Static bindings file", str(state.bindings_path))
    bindings_path = Path(path)
    if not bindings_path.exists():
        state.log_event("ERROR", "load_bindings", "local", {"bindings_path": str(bindings_path)}, message="bindings file not found")
        print(f"File not found: {bindings_path}")
        return
    if not state.pipeline_library:
        print("Note: no pipeline library loaded — 'pipeline_ref' entries will fail to resolve.")
        print("      Load it first via 'Load pipeline json file'.")
    try:
        state.engine.load_config(bindings_path, pipeline_library=state.pipeline_library)
    except (OSError, ValueError, json.JSONDecodeError) as exc:
        state.log_event("ERROR", "load_bindings", "local", {"error": str(exc)}, message="failed to load bindings")
        print(f"Failed to load static bindings: {exc}")
        return

    state.bindings_path = bindings_path
    state.bindings_loaded = True
    pairs = state.engine.list_camera_pipeline_pairs()
    state.log_event("INFO", "load_bindings", "local", {"bindings_path": str(bindings_path), "count": len(pairs)}, message="static bindings loaded")
    print(f"Static camera-pipeline bindings loaded ({len(pairs)} binding(s)) from {bindings_path}.")
    print()
    print(_render_table(
        "Loaded static camera-pipeline bindings",
        ["#", "binding_id", "camera", "mac", "pipeline", "events"],
        _rows_for_pairs(pairs),
    ))


def _action_show_bindings(state: TestAppState) -> None:
    """Display the currently loaded static camera-pipeline bindings as a table."""
    pairs = state.engine.list_camera_pipeline_pairs()
    state.log_event("INFO", "show_bindings", "local", {"count": len(pairs)}, message="static bindings inspected")
    if not state.bindings_loaded and not pairs:
        print("No static bindings loaded. Use 'Load static camera-pipeline bindings' first.")
        return
    print(_render_table(
        f"Static camera-pipeline mapping (source: {state.bindings_path})",
        ["#", "binding_id", "camera", "mac", "profile", "pipeline", "events"],
        _rows_for_pairs_with_profile(pairs),
    ))


def _action_clear_bindings(state: TestAppState) -> None:
    """Disable static bindings: stop any running pipelines and forget them."""
    pairs = state.engine.list_camera_pipeline_pairs()
    for pair in pairs:
        binding_id = pair.get("binding_id")
        if binding_id:
            state.engine.remove_pipeline(binding_id)
    state.bindings_loaded = False
    state.log_event("INFO", "clear_bindings", "local", {"count": len(pairs)}, message="static bindings cleared")
    print(f"Static camera-pipeline bindings disabled ({len(pairs)} removed).")


def _action_add_pipeline(state: TestAppState) -> None:
    hostname = _prompt("Camera hostname/ip")
    print("  Hint: use the camera's configured ONVIF service port, not necessarily 80 (e.g. 2020 for this camera).")
    port = _prompt_int("Camera port", 80)
    mac = _prompt("MAC address (optional)", "") or None
    binding_id = _prompt("Binding id", "") or None
    pipeline = _prompt("Pipeline command (JSON list or shell string)")
    events = _prompt_list("ONVIF events (comma-separated)")
    persist = _prompt_bool("Persist to JSON file", False)

    try:
        parsed = json.loads(pipeline)
        pipeline_value: list[str] | str = parsed
    except json.JSONDecodeError:
        pipeline_value = pipeline

    binding = state.engine.set_camera_pipeline(
        hostname,
        port,
        pipeline_value,
        mac=mac,
        binding_id=binding_id,
        events=events,
        persist=persist,
    )
    state.log_event("INFO", "set_camera_pipeline", binding.camera.key(), binding.to_dict(), message="pipeline mapping added")
    print("Pipeline mapping added.")


def _action_add_event(state: TestAppState) -> None:
    hostname = _prompt("Camera hostname/ip")
    print("  Hint: use the camera's configured ONVIF service port, not necessarily 80 (e.g. 2020 for this camera).")
    port = _prompt_int("Camera port", 80)
    mac = _prompt("MAC address (optional)", "") or None
    name = _prompt("Event name")
    topic_filter = _prompt("Topic filter (optional)", "") or None
    renew_every_raw = _prompt("Renew every seconds (optional)", "")
    renew_every = float(renew_every_raw) if renew_every_raw else None
    metadata_raw = _prompt("Metadata as JSON object (optional)", "{}")
    metadata = json.loads(metadata_raw) if metadata_raw else {}
    event = state.engine.set_camera_event(
        hostname,
        port,
        name,
        mac=mac,
        topic_filter=topic_filter,
        renew_every=renew_every,
        **metadata,
    )
    state.log_event("INFO", "set_camera_event", event.camera.key(), asdict(event), message="event definition added")
    print("Event definition added.")


def _action_set_timers(state: TestAppState) -> None:
    discovery_time = _prompt_int("Discovery interval in seconds", state.engine.getDiscoveryTime())
    timeout = _prompt_int("Timeout in seconds", state.engine.getTimeout())
    state.engine.setDiscoveryTime(discovery_time)
    state.engine.setTimeout(timeout)
    state.log_event("INFO", "set_timers", "local", {"discovery_time": discovery_time, "timeout": timeout}, message="timers updated")
    print("Timers updated.")


def _action_start(state: TestAppState) -> None:
    state.engine.start()
    state.discovery_active = True
    state.log_event("INFO", "start", "local", {}, message="engine started")
    print("Engine started.")


def _action_stop(state: TestAppState) -> None:
    state.engine.stop()
    state.discovery_active = False
    state.log_event("INFO", "stop", "local", {}, message="engine stopped")
    print("Engine stopped.")


def _action_dump(state: TestAppState) -> None:
    print()
    print(build_dump_report(state))


def _action_clear_logs(state: TestAppState) -> None:
    state.logs.clear()
    print("Logs cleared.")


def _action_check_environment(state: TestAppState) -> None:
    state.log_event("INFO", "check_environment", "local", {}, message="environment inspected")
    print(build_environment_report())



def _action_load_rules(state: TestAppState) -> None:
    path = _prompt("Dynamic rules file", str(state.rules_path))
    state.rules_path = Path(path)
    try:
        rules = state.controller.load_rules(state.rules_path)
    except (OSError, ValueError, json.JSONDecodeError) as exc:
        state.log_event("ERROR", "load_rules", "local", {"error": str(exc)}, message="failed to load rules")
        print(f"Failed to load rules: {exc}")
        return
    state.log_event("INFO", "load_rules", "local", {"rules_path": str(state.rules_path), "count": len(rules)}, message="dynamic rules loaded")
    print(f"Loaded {len(rules)} dynamic rule(s).")


def _action_start_dynamic(state: TestAppState) -> None:
    if not state.controller.list_rules():
        print("No rules loaded. Load dynamic rules first.")
        return
    state.controller.start()
    state.dynamic_active = True
    state.log_event("INFO", "start_dynamic", "local", {"rules": len(state.controller.list_rules())}, message="dynamic pipeline control started")
    print("Dynamic pipeline control started.")


def _action_stop_dynamic(state: TestAppState) -> None:
    state.controller.stop()
    state.dynamic_active = False
    state.log_event("INFO", "stop_dynamic", "local", {}, message="dynamic pipeline control stopped")
    print("Dynamic pipeline control stopped.")


def _action_show_rules(state: TestAppState) -> None:
    state.log_event("INFO", "show_rules", "local", {"rules_path": str(state.rules_path)}, message="dynamic rules inspected")
    print(f"dynamic_control_running: {state.dynamic_active}")
    print(_render_table(
        "Dynamic event->action rules",
        ["#", "name", "camera", "ip", "action", "target_binding_id/pipeline_id", "trigger"],
        _rows_for_rules(state.controller.list_rules()),
    ))


def _action_configure_credentials(state: TestAppState) -> None:
    """Prompt for ONVIF username/password and use them as engine-wide defaults.

    The credentials become the fallback for any pipeline binding or event
    rule that does not carry its own, and are used by the profile/preview
    actions. The password is read via getpass so it is not echoed.
    """
    print("\n-- Configure ONVIF credentials --")
    username = _prompt("ONVIF username", state.username or None)
    if username == "__cancel__":
        return
    masked = "*" * len(state.password) if state.password else ""
    try:
        password = getpass.getpass(f"ONVIF password [{masked}]: ")
    except (KeyboardInterrupt, EOFError):
        print()
        return
    state.username = username
    if password:
        state.password = password
    state.engine.set_default_credentials(state.username, state.password)
    state.controller.set_default_credentials(state.username, state.password)
    state.log_event("INFO", "configure_credentials", "local", {"username": state.username}, message="ONVIF credentials configured")
    print(f"Credentials set: user='{state.username or '-'}', password={len(state.password)} chars.")


def _collect_all_profiles(
    state: TestAppState,
) -> list[tuple[str, int, Any]]:
    """Return a flat list of (hostname, port, ONVIFProfile) for all active cameras."""
    username = state.username
    password = state.password
    active = state.engine.get_active_cameras()
    if not active:
        return []
    cameras = [{"hostname": item["camera"]["hostname"], "port": item["camera"]["port"]} for item in active]
    entries: list[tuple[str, int, Any]] = []
    for result in read_camera_profiles(cameras, username=username, password=password):
        if not result.ok:
            print(f"  {result.hostname}:{result.port}  ERROR: {result.error}")
            continue
        for profile in result.profiles:
            entries.append((result.hostname, result.port, profile))
    return entries


def _action_list_camera_profiles(state: TestAppState) -> None:
    active = state.engine.get_active_cameras()
    if not active:
        print("No active cameras. Start discovery first (option 10).")
        return
    username = state.username
    password = state.password
    cameras = [{"hostname": item["camera"]["hostname"], "port": item["camera"]["port"]} for item in active]
    print()
    for result in read_camera_profiles(cameras, username=username, password=password):
        if not result.ok:
            print(f"  {result.hostname}:{result.port}  ERROR: {result.error}")
            continue
        rows = _rows_for_profiles(result.hostname, result.port, result.profiles)
        print(_render_table(
            f"{result.hostname}:{result.port}  ({len(result.profiles)} profile(s))",
            ["#", "camera", "profile_name", "token", "encoding", "resolution", "rtsp_url"],
            rows,
        ))
        print()
    state.log_event("INFO", "list_camera_profiles", "local", {"camera_count": len(cameras)}, message="camera profiles listed")


def _action_preview_stream(state: TestAppState) -> None:
    active = state.engine.get_active_cameras()
    if not active:
        print("No active cameras. Start discovery first (option 10).")
        return

    print(f"  Reading profiles from {len(active)} camera(s)...", end="\r")
    entries = _collect_all_profiles(state)
    print(" " * 50, end="\r")  # wyczyść linię statusu
    if not entries:
        print(f"  Could not read any profiles from {len(active)} camera(s). Check ONVIF_USER / ONVIF_PASSWORD.")
        return

    print()
    rows = [
        [
            str(index),
            f"{hostname}:{port}",
            str(profile.name or "-"),
            str(profile.vec_encoding or "-"),
            str(profile.rtsp_url or "-"),
        ]
        for index, (hostname, port, profile) in enumerate(entries, 1)
    ]
    print(_render_table(
        "Available streams",
        ["#", "camera", "profile_name", "encoding", "rtsp_url"],
        rows,
    ))
    print()

    raw = _prompt("Select stream number (or Enter to cancel)", "")
    if not raw or raw == "__cancel__":
        return
    try:
        index = int(raw) - 1
        if not (0 <= index < len(entries)):
            raise ValueError
    except ValueError:
        print("Invalid selection.")
        return

    hostname, port, profile = entries[index]
    rtsp_url = str(profile.rtsp_url or "")
    if not rtsp_url:
        print("Selected profile has no RTSP URL.")
        return

    username = state.username
    password = state.password
    if username and password and "@" not in rtsp_url:
        from urllib.parse import urlparse, urlunparse
        parsed = urlparse(rtsp_url)
        rtsp_url = urlunparse(parsed._replace(netloc=f"{username}:{password}@{parsed.hostname}:{parsed.port or 554}"))

    cmd = ["ffplay", "-rtsp_transport", "tcp", "-i", rtsp_url]
    print(f"Launching: {' '.join(cmd[:3])} -i <rtsp_url>")
    print(f"RTSP URL: {rtsp_url}")
    try:
        import subprocess
        import time
        proc = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        state.log_event("INFO", "preview_stream", f"{hostname}:{port}", {"profile": profile.name, "pid": proc.pid}, message="stream preview started")
        print(f"Preview started (PID {proc.pid}). Close the player window to stop.")
        
        # Monitor process for early exit/errors
        time.sleep(1.0)  # Give ffplay time to start and connect
        if proc.poll() is not None:  # Process already ended
            returncode = proc.returncode
            _, stderr = proc.communicate()
            print(f"ffplay exited with code {returncode}.")
            if stderr:
                print(f"ffplay stderr:\n{stderr}")
        else:
            print("Stream is running. Waiting for user to close the window...")
    except FileNotFoundError:
        print("ffplay not found. Install ffmpeg to use stream preview.")


def run(argv: Optional[list[str]] = None) -> None:
    parser = argparse.ArgumentParser(prog="video_engine_sample", add_help=True)
    parser.add_argument("--config", default=str(DEFAULT_CONFIG_PATH), help="Path to the camera-pipeline JSON file")
    args = parser.parse_args(argv)

    state = TestAppState(args.config)
    try:
        while True:
            print("===  Sample Application: ONVIF Video Engine Library ===\n")
            print("1) Configure ONVIF credentials")
            print("2) Load pipeline library file (JSON)(file with defined pipelines)")
            print("3) Show loaded pipeline library")
            print("4) Load static camera-pipeline mapping (JSON)")
            print("5) Show static camera-pipeline mapping")
            print("6) Clear static camera-pipeline mapping")
            print("7) Load dynamic event->action rules")
            print("8) Show dynamic event->action rules")
            print("9) Add camera-pipeline mapping")
            print("10) Add ONVIF event definition")
            print("11) Set discovery interval and timeout")
            print("12) Start discovery and pipeline orchestration")
            print("13) Stop discovery and pipelines")
            print("14) Start dynamic pipeline control (event-driven)")
            print("15) Stop dynamic pipeline control")
            print("16) Dump current state")
            print("17) Check environment variables")
            print("18) Clear logs")
            print("19) List camera profiles")
            print("20) Preview stream")
            print("21) Quit")

            choice = _prompt("Select option")
            if choice == "__cancel__":
                break
            os.system("clear")
            if choice == "1":
                _action_configure_credentials(state)
            elif choice == "2":
                _action_load_config(state)
            elif choice == "3":
                _action_show_pipeline_library(state)
            elif choice == "4":
                _action_load_bindings(state)
            elif choice == "5":
                _action_show_bindings(state)
            elif choice == "6":
                _action_clear_bindings(state)
            elif choice == "7":
                _action_load_rules(state)
            elif choice == "8":
                _action_show_rules(state)
            elif choice == "9":
                _action_add_pipeline(state)
            elif choice == "10":
                _action_add_event(state)
            elif choice == "11":
                _action_set_timers(state)
            elif choice == "12":
                _action_start(state)
            elif choice == "13":
                _action_stop(state)
            elif choice == "14":
                _action_start_dynamic(state)
            elif choice == "15":
                _action_stop_dynamic(state)
            elif choice == "16":
                _action_dump(state)
            elif choice == "17":
                _action_check_environment(state)
            elif choice == "18":
                _action_clear_logs(state)
            elif choice == "19":
                _action_list_camera_profiles(state)
            elif choice == "20":
                _action_preview_stream(state)
            elif choice == "21":
                break
            else:
                print("Unknown option.")
    finally:
        state.close()


if __name__ == "__main__":
    run()
