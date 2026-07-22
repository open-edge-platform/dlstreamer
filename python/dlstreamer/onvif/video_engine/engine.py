# ==============================================================================
# Copyright (C) 2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================
"""Runtime engine for ONVIF discovery and DL Streamer pipeline orchestration."""

# Cohesive orchestrator kept in a single module; just over the line threshold.
# pylint: disable=too-many-lines

from __future__ import annotations

import json
import shlex
import tempfile
from contextlib import suppress
import subprocess
import threading
from dataclasses import asdict
from datetime import datetime, timezone
from pathlib import Path
from typing import Any, Callable, Dict, Iterable, List, Optional, Sequence
from urllib.parse import quote, urlsplit, urlunsplit

from ..discovery import discover_onvif_cameras
from ..camera_profiles import read_camera_profiles

from .types import (
    CameraEventDefinition,
    CameraIdentity,
    CameraProfileSnapshot,
    CameraRuntimeState,
    PipelineBinding,
    PipelineTemplate,
    VideoEngineEvent,
    as_camera_identity,
    as_pipeline_template,
    pipeline_list,
    select_profile,
)

CallbackType = Callable[[VideoEngineEvent], None]
PipelineRunnerType = Callable[[PipelineBinding], subprocess.Popen[Any]]


class VideoEngine:  # pylint: disable=too-many-instance-attributes,too-many-public-methods
    """Coordinate discovery, camera matching, pipeline control and events."""

    def __init__(
        self,
        config_path: str | Path | None = None,
        *,
        discovery_time: int = 60,
        timeout: int = 120,
        verbose: bool = False,
        pipeline_runner: Optional[PipelineRunnerType] = None,
    ) -> None:
        self._config_path = Path(config_path) if config_path is not None else None
        self._discovery_time = max(1, int(discovery_time))
        self._timeout = max(1, int(timeout))
        self._verbose = verbose
        self._pipeline_runner = pipeline_runner

        self._lock = threading.RLock()
        self._stop_event = threading.Event()
        self._discovery_thread: Optional[threading.Thread] = None
        self._reaper_thread: Optional[threading.Thread] = None

        self._bindings: list[PipelineBinding] = []
        self._binding_lookup: Dict[str, PipelineBinding] = {}
        self._event_definitions: Dict[str, list[CameraEventDefinition]] = {}
        self._camera_states: Dict[str, CameraRuntimeState] = {}
        self._active_processes: Dict[str, subprocess.Popen[Any]] = {}
        self._callbacks: list[CallbackType] = []
        self._default_username: str = ""
        self._default_password: str = ""

        # ---- opt-in auto-templates / auto-profile-fetch state ----
        # Everything below defaults to a no-op behavior so existing callers
        # that do not touch the new API keep the exact same runtime path.
        self._templates: list[PipelineTemplate] = []
        self._auto_fetch_profiles: bool = False
        self._profile_ttl_seconds: float = 300.0
        self._profile_cache: Dict[str, CameraProfileSnapshot] = {}
        self._template_bindings_by_camera: Dict[str, set[str]] = {}

        if self._config_path is not None:
            self.load_config(self._config_path)

    def load_config(
        self,
        config_path: str | Path,
        *,
        pipeline_library: Optional[Dict[str, Any]] = None,
    ) -> None:
        """Load pipeline bindings and templates from a JSON config file."""
        path = Path(config_path)
        if not path.exists():
            with self._lock:
                self._bindings = []
                self._binding_lookup = {}
                self._config_path = path
            return

        payload = json.loads(path.read_text(encoding="utf-8"))
        bindings = self._parse_bindings(payload, pipeline_library)
        templates = self._parse_templates(payload)
        with self._lock:
            self._bindings = bindings
            self._binding_lookup = {binding.binding_id: binding for binding in bindings}
            if templates is not None:
                self._templates = list(templates)
                for template in templates:
                    if self._template_needs_profiles(template) and not self._auto_fetch_profiles:
                        self._auto_fetch_profiles = True
            self._config_path = path

    def save_config(self) -> None:
        """Persist the current pipeline bindings to the configured path."""
        if self._config_path is None:
            raise ValueError("no configuration path was set")

        with self._lock:
            payload = {"pipelines": [binding.to_dict() for binding in self._bindings]}
        self._config_path.write_text(json.dumps(payload, indent=2, sort_keys=True), encoding="utf-8")

    def start(self) -> None:
        """Start the discovery loop and the stale-camera reaper."""
        self.discovery_start()
        self._start_reaper()

    def stop(self) -> None:
        """Stop discovery, the reaper, and all running pipelines."""
        self.discovery_stop()
        self._stop_reaper()
        self._stop_all_processes()

    def destroy(self) -> None:
        """Stop the engine and clear all state, bindings and callbacks."""
        self.stop()
        with self._lock:
            self._bindings = []
            self._binding_lookup = {}
            self._event_definitions.clear()
            self._camera_states.clear()
            self._active_processes.clear()
            self._callbacks.clear()
            self._templates = []
            self._profile_cache.clear()
            self._template_bindings_by_camera.clear()

    def discovery_start(self) -> None:
        """Start the background ONVIF discovery thread (idempotent)."""
        with self._lock:
            if self._discovery_thread is not None and self._discovery_thread.is_alive():
                return
            self._stop_event.clear()
            self._discovery_thread = threading.Thread(target=self._discovery_loop, name="video-engine-discovery", daemon=True)
            self._discovery_thread.start()

    def discovery_stop(self) -> None:
        """Signal the discovery thread to stop and wait for it to finish."""
        self._stop_event.set()
        thread = None
        with self._lock:
            thread = self._discovery_thread
            self._discovery_thread = None
        if thread is not None and thread.is_alive():
            thread.join(timeout=2.0)

    # pylint: disable=invalid-name
    # camelCase kept for backward-compatible public API (see PUBLIC_API.md).
    def setTimeout(self, sec: int) -> None:
        """Set the camera-liveness timeout, in seconds."""
        self._timeout = max(1, int(sec))

    def getTimeout(self) -> int:
        """Return the camera-liveness timeout, in seconds."""
        return self._timeout

    def setDiscoveryTime(self, sec: int) -> None:
        """Set the interval between discovery sweeps, in seconds."""
        self._discovery_time = max(1, int(sec))

    def getDiscoveryTime(self) -> int:
        """Return the interval between discovery sweeps, in seconds."""
        return self._discovery_time
    # pylint: enable=invalid-name

    def set_default_credentials(self, username: str, password: str) -> None:
        """Set fallback ONVIF credentials for profile (RTSP URL) resolution.

        Used when a pipeline binding does not carry its own credentials.
        """
        self._default_username = username or ""
        self._default_password = password or ""

    def register_callback(self, callback: CallbackType) -> None:
        """Register an event callback (ignored if already registered)."""
        with self._lock:
            if callback not in self._callbacks:
                self._callbacks.append(callback)

    def unregister_callback(self, callback: CallbackType) -> None:
        """Remove a previously registered event callback (if present)."""
        with self._lock:
            if callback in self._callbacks:
                self._callbacks.remove(callback)

    def list_camera_pipeline_pairs(self) -> list[dict[str, Any]]:
        """Return all configured camera/pipeline bindings as dicts."""
        with self._lock:
            return [binding.to_dict() for binding in self._bindings]

    def get_pipeline_for_camera(
        self,
        hostname: str,
        port: int,
        mac: str | None = None,
    ) -> list[dict[str, Any]]:
        """Return the bindings matching the given camera identity."""
        identity = CameraIdentity(hostname, int(port), mac)
        with self._lock:
            matches = [binding.to_dict() for binding in self._bindings if self._binding_matches(binding, identity)]
        return matches

    def set_camera_pipeline(  # pylint: disable=too-many-arguments
        self,
        hostname: str,
        port: int,
        pipeline: Sequence[str] | str,
        *,
        mac: str | None = None,
        binding_id: str | None = None,
        events: Iterable[str] | None = None,
        persist: bool = False,
    ) -> PipelineBinding:
        """Create and register a pipeline binding for a camera."""
        identity = CameraIdentity(hostname, int(port), mac)
        normalized = pipeline_list(pipeline)
        event_tuple = tuple(str(item) for item in (events or ()))
        with self._lock:
            resolved_binding_id = binding_id or f"{identity.key()}:{len(self._bindings)}"
            binding = PipelineBinding(identity, normalized, resolved_binding_id, event_tuple)
            self._bindings.append(binding)
            self._binding_lookup[resolved_binding_id] = binding
        if persist:
            self.save_config()
        return binding

    def set_camera_event(  # pylint: disable=too-many-arguments
        self,
        hostname: str,
        port: int,
        event_name: str,
        *,
        mac: str | None = None,
        topic_filter: str | None = None,
        renew_every: float | None = None,
        **metadata: Any,
    ) -> CameraEventDefinition:
        """Attach an ONVIF event definition to a camera identity."""
        identity = CameraIdentity(hostname, int(port), mac)
        definition = CameraEventDefinition(event_name, identity, topic_filter, renew_every, dict(metadata))
        with self._lock:
            self._event_definitions.setdefault(identity.key(), []).append(definition)
        return definition

    def list_camera_events(self, hostname: str, port: int, mac: str | None = None) -> list[dict[str, Any]]:
        """Return the event definitions registered for a camera."""
        identity = CameraIdentity(hostname, int(port), mac)
        with self._lock:
            definitions = self._event_definitions.get(identity.key(), [])
            return [asdict(item) for item in definitions]

    def add_pipeline(self, binding: PipelineBinding, *, start: bool = True) -> PipelineBinding:
        """Register a pipeline binding and optionally start it immediately.

        Replaces any existing binding sharing the same ``binding_id`` so the
        operation is idempotent for event-driven callers.
        """
        with self._lock:
            self._bindings = [b for b in self._bindings if b.binding_id != binding.binding_id]
            self._bindings.append(binding)
            self._binding_lookup[binding.binding_id] = binding
        if start:
            self._start_pipeline_if_needed(binding)
        return binding

    def remove_pipeline(self, binding_id: str) -> None:
        """Stop a running pipeline (if any) and forget its binding."""
        self._stop_pipeline(binding_id)
        with self._lock:
            self._bindings = [b for b in self._bindings if b.binding_id != binding_id]
            self._binding_lookup.pop(binding_id, None)

    def replace_pipeline(  # pylint: disable=too-many-arguments,too-many-locals
        self,
        binding_id: str,
        pipeline: Sequence[str] | str,
        *,
        camera: CameraIdentity | None = None,
        profile_name: str | None = None,
        events: Iterable[str] | None = None,
        username: str | None = None,
        password: str | None = None,
        start: bool = True,
    ) -> PipelineBinding:
        """Restart-based modification: stop, swap the command, start again.

        Preserves the existing binding's camera/profile/events when they are
        not overridden. Falls back to ``camera`` when the binding is new.
        """
        with self._lock:
            current = self._binding_lookup.get(binding_id)
        target_camera = camera or (current.camera if current is not None else None)
        if target_camera is None:
            raise ValueError("replace_pipeline requires a known binding or an explicit camera")

        self._stop_pipeline(binding_id)

        event_tuple = (
            tuple(str(item) for item in events)
            if events is not None
            else (current.events if current is not None else ())
        )
        resolved_profile = profile_name if profile_name is not None else (current.profile_name if current else None)
        resolved_username = username if username is not None else (current.username if current else "")
        resolved_password = password if password is not None else (current.password if current else "")
        new_binding = PipelineBinding(
            target_camera,
            pipeline_list(pipeline),
            binding_id,
            event_tuple,
            resolved_profile,
            resolved_username,
            resolved_password,
        )

        with self._lock:
            self._bindings = [b for b in self._bindings if b.binding_id != binding_id]
            self._bindings.append(new_binding)
            self._binding_lookup[binding_id] = new_binding
        if start:
            self._start_pipeline_if_needed(new_binding)
        return new_binding

    def restart_pipeline(self, binding_id: str) -> None:
        """Stop and start the pipeline for an existing binding unchanged."""
        with self._lock:
            binding = self._binding_lookup.get(binding_id)
        if binding is None:
            return
        self._stop_pipeline(binding_id)
        self._start_pipeline_if_needed(binding)

    def get_active_cameras(self) -> list[dict[str, Any]]:
        """Return runtime state for every currently discovered camera."""
        with self._lock:
            return [self._runtime_state_to_dict(state) for state in self._camera_states.values()]

    def get_active_pipelines(self) -> list[dict[str, Any]]:
        """Return the binding key and PID of every running pipeline."""
        with self._lock:
            active: list[dict[str, Any]] = []
            for binding_key, process in self._active_processes.items():
                active.append(
                    {
                        "binding_key": binding_key,
                        "pid": process.pid,
                    }
                )
            return active

    # ------------------------------------------------------------------
    # Auto-templates / auto-profile-fetch (opt-in, purely additive API)
    # ------------------------------------------------------------------

    def enable_auto_profile_fetch(
        self,
        enabled: bool = True,
        *,
        ttl_seconds: float = 300.0,
    ) -> None:
        """Enable/disable automatic ONVIF media-profile fetch on discovery.

        When enabled, the discovery loop queries each discovered camera for
        its media profiles once (respecting ``ttl_seconds``) and caches the
        result. Templates registered via :meth:`add_pipeline_template` are
        evaluated against those cached profiles.
        """
        with self._lock:
            self._auto_fetch_profiles = bool(enabled)
            self._profile_ttl_seconds = max(0.0, float(ttl_seconds))
            if not enabled:
                self._profile_cache.clear()

    def add_pipeline_template(self, template: PipelineTemplate) -> PipelineTemplate:
        """Register a :class:`PipelineTemplate` used to auto-bind pipelines.

        Templates are matched against every discovered camera. On match, a
        deterministic :class:`PipelineBinding` is created and started.
        Existing bindings (registered via :meth:`set_camera_pipeline` or
        :meth:`load_config`) are unaffected. Registering the same
        ``template_id`` twice replaces the previous entry.
        """
        if not isinstance(template, PipelineTemplate):
            raise TypeError("template must be a PipelineTemplate instance")
        with self._lock:
            self._templates = [t for t in self._templates if t.template_id != template.template_id]
            self._templates.append(template)
            # Implicitly enable profile fetch when the pipeline needs profile-
            # level information; the caller can still opt out afterwards.
            if self._template_needs_profiles(template) and not self._auto_fetch_profiles:
                self._auto_fetch_profiles = True
        return template

    def remove_pipeline_template(self, template_id: str) -> None:
        """Unregister a template by id (does not stop bindings it produced).

        Bindings previously instantiated from the template keep running; call
        :meth:`remove_pipeline` or :meth:`_handle_camera_lost` (via reaper) to
        stop them.
        """
        with self._lock:
            self._templates = [t for t in self._templates if t.template_id != template_id]

    def list_pipeline_templates(self) -> list[dict[str, Any]]:
        """Return the currently registered templates as plain dicts."""
        with self._lock:
            return [t.to_dict() for t in self._templates]

    # ---- helpers ----

    @staticmethod
    def _template_needs_profiles(template: PipelineTemplate) -> bool:
        pipeline = template.pipeline
        text = pipeline if isinstance(pipeline, str) else " ".join(str(x) for x in pipeline)
        return "{rtsp_url}" in text or "{profile_name}" in text

    def _profile_cache_get(self, key: str) -> Optional[CameraProfileSnapshot]:
        with self._lock:
            snapshot = self._profile_cache.get(key)
            if snapshot is None:
                return None
            if self._profile_ttl_seconds <= 0.0:
                return snapshot
            age = (datetime.now(timezone.utc) - snapshot.fetched_at).total_seconds()
            if age > self._profile_ttl_seconds:
                return None
            return snapshot

    def _maybe_fetch_profiles(
        self,
        identity: CameraIdentity,
        *,
        force_refresh: bool = False,
    ) -> CameraProfileSnapshot:
        key = identity.key()
        if not force_refresh:
            cached = self._profile_cache_get(key)
            if cached is not None:
                return cached

        snapshot = self._fetch_profiles(identity)

        with self._lock:
            self._profile_cache[key] = snapshot
            state = self._camera_states.get(key)
            if state is not None:
                state.profiles_snapshot = snapshot

        if snapshot.ok:
            self._emit(
                "camera_profiles_fetched",
                identity,
                {"profile_count": len(snapshot.profiles)},
            )
        else:
            self._emit(
                "camera_profiles_error",
                identity,
                {"error": snapshot.error},
            )
        return snapshot

    def _fetch_profiles(self, identity: CameraIdentity) -> CameraProfileSnapshot:
        username = self._default_username
        password = self._default_password
        cameras = [{"hostname": identity.hostname, "port": int(identity.port)}]

        try:
            results = list(
                read_camera_profiles(cameras, username=username, password=password)
            )
        except Exception as exc:  # pylint: disable=broad-exception-caught
            return CameraProfileSnapshot(
                identity=identity,
                profiles=(),
                error=f"{type(exc).__name__}: {exc}",
            )

        if not results:
            return CameraProfileSnapshot(
                identity=identity, profiles=(), error="no profiles returned"
            )
        result = results[0]
        error = getattr(result, "error", None)
        profiles = tuple(getattr(result, "profiles", ()) or ())
        mac = getattr(result, "mac_address", "") or ""
        return CameraProfileSnapshot(
            identity=identity,
            profiles=profiles,
            mac_address=str(mac),
            error=error,
        )

    def _apply_templates_if_enabled(
        self,
        identity: CameraIdentity,
        snapshot: Optional[CameraProfileSnapshot],
    ) -> None:
        if not self._templates:
            return
        profiles = snapshot.profiles if snapshot is not None else ()
        camera_key = identity.key()

        with self._lock:
            templates = list(self._templates)
            already_bound = set(self._template_bindings_by_camera.get(camera_key, ()))

        for template in templates:
            if not template.matcher.matches(identity):
                continue
            selected = select_profile(profiles, template.profile_selector) if profiles else None

            binding_id = self._template_binding_id(template, identity, selected)
            if binding_id in already_bound:
                # Same template + camera already instantiated; skip.
                continue

            binding = self._render_template_binding(template, identity, selected)
            self.add_pipeline(binding, start=template.auto_start)
            self._bind_camera_to_binding(camera_key, binding.binding_id)
            with self._lock:
                self._template_bindings_by_camera.setdefault(camera_key, set()).add(
                    binding.binding_id
                )
                state = self._camera_states.get(camera_key)
                if state is not None and binding.binding_id not in state.template_binding_ids:
                    state.template_binding_ids = tuple(
                        (*state.template_binding_ids, binding.binding_id)
                    )
            self._emit(
                "template_matched",
                identity,
                {
                    "template_id": template.template_id,
                    "binding_id": binding.binding_id,
                    "profile_name": binding.profile_name or "",
                },
            )

    def _template_binding_id(
        self,
        template: PipelineTemplate,
        identity: CameraIdentity,
        profile: Any,
    ) -> str:
        profile_name = getattr(profile, "name", "") if profile is not None else ""
        base = f"tpl:{template.template_id}:{identity.key()}"
        return f"{base}:{profile_name}" if profile_name else base

    def _render_template_binding(
        self,
        template: PipelineTemplate,
        identity: CameraIdentity,
        profile: Any,
    ) -> PipelineBinding:
        profile_name = str(getattr(profile, "name", "")) if profile is not None else ""
        rtsp_url = str(getattr(profile, "rtsp_url", "")) if profile is not None else ""
        mac = identity.mac or ""

        substitutions = {
            "{hostname}": identity.hostname,
            "{port}": str(int(identity.port)),
            "{mac}": mac,
            "{profile_name}": profile_name,
            "{rtsp_url}": rtsp_url,
        }

        def _apply(value: str) -> str:
            out = value
            for placeholder, replacement in substitutions.items():
                if placeholder in out:
                    out = out.replace(placeholder, replacement)
            return out

        pipeline = template.pipeline
        if isinstance(pipeline, str):
            rendered: Sequence[str] | str = _apply(pipeline)
        else:
            rendered = tuple(_apply(str(item)) for item in pipeline)

        return PipelineBinding(
            camera=identity,
            pipeline=rendered,
            binding_id=self._template_binding_id(template, identity, profile),
            events=template.events,
            profile_name=profile_name or None,
            username=template.username,
            password=template.password,
        )

    def _parse_bindings(  # pylint: disable=too-many-locals
        self,
        payload: Any,
        pipeline_library: Optional[Dict[str, Any]] = None,
    ) -> list[PipelineBinding]:
        if isinstance(payload, dict):
            entries = payload.get("pipelines", [])
        elif isinstance(payload, list):
            entries = payload
        else:
            raise ValueError("video_engine configuration must be a mapping or a list")

        bindings: list[PipelineBinding] = []
        for index, entry in enumerate(entries):
            if not isinstance(entry, dict):
                raise ValueError("each pipeline entry must be a mapping")

            camera_payload = entry.get("camera", entry)
            identity = as_camera_identity(camera_payload)

            binding_id = str(entry.get("binding_id") or entry.get("pipeline_id") or f"{identity.key()}:{index}")

            # Prefer a single source of truth: a binding may reference a pipeline
            # by id (``pipeline_ref``) resolved against the pipeline library.
            # An inline ``pipeline`` is still accepted for standalone configs.
            pipeline_value = entry.get("pipeline")
            pipeline_ref = entry.get("pipeline_ref")
            if pipeline_value is None and pipeline_ref is not None:
                if not pipeline_library or pipeline_ref not in pipeline_library:
                    raise ValueError(
                        f"binding '{binding_id}' references unknown pipeline_ref '{pipeline_ref}'"
                    )
                pipeline_value = pipeline_library[pipeline_ref]
            if pipeline_value is None:
                raise ValueError(
                    f"binding '{binding_id}' requires a pipeline or pipeline_ref field"
                )

            events = tuple(str(item) for item in entry.get("events", []))
            profile_name = entry.get("profile_name")
            username = str(entry.get("username", ""))
            password = str(entry.get("password", ""))
            bindings.append(
                PipelineBinding(
                    identity,
                    pipeline_list(pipeline_value),
                    binding_id,
                    events,
                    profile_name,
                    username,
                    password,
                )
            )
        return bindings

    def _parse_templates(self, payload: Any) -> Optional[List[PipelineTemplate]]:
        """Parse the optional ``templates`` config section.

        Returns ``None`` when the payload has no ``templates`` key so the
        caller can distinguish "template list not present" (leave the current
        list untouched) from "template list empty" (clear the list).
        """
        if not isinstance(payload, dict) or "templates" not in payload:
            return None
        entries = payload.get("templates") or []
        if not isinstance(entries, list):
            raise ValueError("'templates' must be a list of template mappings")
        return [as_pipeline_template(entry) for entry in entries]

    def _binding_matches(self, binding: PipelineBinding, identity: CameraIdentity) -> bool:
        if binding.camera.hostname != identity.hostname:
            return False
        if int(binding.camera.port) != int(identity.port):
            return False
        if binding.camera.mac and identity.mac:
            return binding.camera.mac == identity.mac
        return True

    def _discovery_loop(self) -> None:
        while not self._stop_event.is_set():
            try:
                for camera in discover_onvif_cameras(verbose=self._verbose):
                    if self._stop_event.is_set():
                        break
                    self._handle_camera_seen(camera)
            except (OSError, RuntimeError, ValueError) as exc:
                self._emit("discovery_error", CameraIdentity("unknown", 0), {"error": str(exc)})

            if self._stop_event.wait(self._discovery_time):
                break

    def _start_reaper(self) -> None:
        with self._lock:
            if self._reaper_thread is not None and self._reaper_thread.is_alive():
                return
            self._stop_event.clear()
            self._reaper_thread = threading.Thread(target=self._reaper_loop, name="video-engine-reaper", daemon=True)
            self._reaper_thread.start()

    def _stop_reaper(self) -> None:
        self._stop_event.set()
        thread = None
        with self._lock:
            thread = self._reaper_thread
            self._reaper_thread = None
        if thread is not None and thread.is_alive():
            thread.join(timeout=2.0)

    def _reaper_loop(self) -> None:
        while not self._stop_event.is_set():
            self._reap_stale_cameras()
            if self._stop_event.wait(max(1, min(self._timeout, self._discovery_time))):
                break

    def _reap_stale_cameras(self) -> None:
        now = datetime.now(timezone.utc)
        stale_keys: list[str] = []
        with self._lock:
            for key, state in self._camera_states.items():
                if (now - state.last_seen).total_seconds() > self._timeout:
                    stale_keys.append(key)

        for key in stale_keys:
            self._handle_camera_lost(key)

    def _handle_camera_seen(self, camera: dict[str, Any]) -> None:
        identity = as_camera_identity(camera)
        key = identity.key()
        matched_bindings: list[PipelineBinding]
        with self._lock:
            matched_bindings = [binding for binding in self._bindings if self._binding_matches(binding, identity)]
            state = self._camera_states.get(key)
            if state is None:
                state = CameraRuntimeState(identity)
                self._camera_states[key] = state
            state.status = "matched" if matched_bindings else "discovered"
            state.error = None
            state.touch()

        if self._verbose:
            print(f"[video_engine] Camera discovered: {key}, matched bindings: {len(matched_bindings)}")
            for binding in matched_bindings:
                print(f"[video_engine]   -> binding '{binding.binding_id}' (profile: {binding.profile_name})")

        self._emit("camera_seen", identity, {"matched": bool(matched_bindings)})

        for binding in matched_bindings:
            self._start_pipeline_if_needed(binding)
            self._bind_camera_to_binding(key, binding.binding_id)

        # Opt-in extension: auto-fetch profiles and/or apply pipeline templates.
        # This branch is a no-op unless the caller enabled auto-profile fetch or
        # registered at least one template — preserves the original discovery
        # path for legacy configurations. Profiles are only queried when
        # auto-fetch is on (templates needing profiles enable it implicitly);
        # identity-only templates never touch the network.
        if self._auto_fetch_profiles or self._templates:
            snapshot = self._maybe_fetch_profiles(identity) if self._auto_fetch_profiles else None
            if self._templates:
                self._apply_templates_if_enabled(identity, snapshot)

    def _bind_camera_to_binding(self, camera_key: str, binding_id: str) -> None:
        with self._lock:
            state = self._camera_states.get(camera_key)
            if state is None:
                return
            if binding_id in state.pipeline_binding_ids:
                return
            state.pipeline_binding_ids = tuple((*state.pipeline_binding_ids, binding_id))

    def _handle_camera_lost(self, camera_key: str) -> None:
        with self._lock:
            state = self._camera_states.pop(camera_key, None)
            if state is None:
                return
            binding_ids = list(state.pipeline_binding_ids)
            template_binding_ids = list(self._template_bindings_by_camera.pop(camera_key, ()))
            self._profile_cache.pop(camera_key, None)
        # Merge template-owned bindings so they get stopped and forgotten too.
        for binding_id in template_binding_ids:
            if binding_id not in binding_ids:
                binding_ids.append(binding_id)
        for binding_id in binding_ids:
            self._stop_pipeline(binding_id)
        # Fully remove template-owned bindings so re-discovery re-instantiates
        # them cleanly (regular bindings live for the life of the engine).
        if template_binding_ids:
            with self._lock:
                self._bindings = [
                    b for b in self._bindings if b.binding_id not in template_binding_ids
                ]
                for binding_id in template_binding_ids:
                    self._binding_lookup.pop(binding_id, None)
        self._emit("camera_lost", state.camera, {})
        if template_binding_ids:
            self._emit("template_unmatched", state.camera, {"binding_ids": template_binding_ids})

    def _start_pipeline_if_needed(self, binding: PipelineBinding) -> None:
        binding_key = binding.binding_id
        with self._lock:
            existing = self._active_processes.get(binding_key)
            if existing is not None:
                if existing.poll() is None:
                    # A live process is already running for this binding.
                    return
                # The previous process has exited; drop the stale entry so the
                # binding can be respawned (e.g. camera reconnect, changed
                # profile, or a pipeline that crashed on its own).
                self._active_processes.pop(binding_key, None)
                if self._verbose:
                    print(
                        f"[video_engine] Previous pipeline for '{binding_key}' "
                        f"exited (code {existing.poll()}); respawning."
                    )

        process = self._spawn_pipeline(binding)
        with self._lock:
            self._active_processes[binding_key] = process

        self._emit("pipeline_started", binding.camera, {"binding_id": binding_key, "pid": process.pid})

    def _spawn_pipeline(self, binding: PipelineBinding) -> subprocess.Popen[Any]:
        if self._pipeline_runner is not None:
            return self._pipeline_runner(binding)

        command = self._resolve_pipeline_command(binding)
        if self._verbose:
            if isinstance(command, str):
                print(f"[video_engine] Spawning pipeline for {binding.camera.key()}: {command[:100]}...")
            else:
                cmd_str = " ".join(str(c) for c in list(command)[:5])
                print(f"[video_engine] Spawning pipeline for {binding.camera.key()}: {cmd_str}...")

        # Log pipeline stdout/stderr to a temp file for debugging. The handle
        # must outlive this function: it is handed to subprocess as the
        # pipeline's stdout/stderr and closed when the process is reaped.
        log_name = f"video_engine_pipeline_{binding.binding_id.replace(' ', '_')}.log"
        log_path = Path(tempfile.gettempdir()) / log_name
        log_file = open(  # pylint: disable=consider-using-with
            log_path,
            "w",
            encoding="utf-8",
        )
        if self._verbose:
            print(f"[video_engine] Logging pipeline output to: {log_file.name}")

        # Split a string command into argv so the pipeline runs without a shell
        # (avoids shell-injection; Bandit B602).
        argv = shlex.split(command) if isinstance(command, str) else list(command)
        return subprocess.Popen(argv, stdout=log_file, stderr=subprocess.STDOUT)

    def _resolve_pipeline_command(self, binding: PipelineBinding) -> Sequence[str] | str:
        """Resolve {rtsp_url} placeholder in pipeline using camera profile."""
        command = binding.pipeline
        if isinstance(command, str):
            pipeline_str = command
        else:
            pipeline_str = " ".join(str(c) for c in command)

        # If no {rtsp_url} placeholder or no profile_name, return as-is
        if "{rtsp_url}" not in pipeline_str or not binding.profile_name:
            return command

        if self._verbose:
            print(f"[video_engine] Resolving {{rtsp_url}} placeholder for profile '{binding.profile_name}'")

        # Try to resolve RTSP URL from camera profile
        rtsp_url, reason = self._get_profile_rtsp_url(
            binding.camera, binding.profile_name, binding.username, binding.password
        )
        if rtsp_url is None:
            if self._verbose:
                print(
                    f"[video_engine] ERROR: could not resolve {{rtsp_url}} for "
                    f"binding '{binding.binding_id}' "
                    f"(requested profile '{binding.profile_name}', "
                    f"camera {binding.camera.hostname}:{binding.camera.port}): {reason}"
                )
            return command

        if self._verbose:
            print(f"[video_engine] Resolved RTSP URL: {rtsp_url}")

        # Replace placeholder
        if isinstance(command, str):
            return command.replace("{rtsp_url}", rtsp_url)

        return tuple(str(c).replace("{rtsp_url}", rtsp_url) for c in command)

    def _get_profile_rtsp_url(
        self,
        camera: CameraIdentity,
        profile_name: str,
        username: str = "",
        password: str = "",
    ) -> tuple[Optional[str], str]:
        """Resolve a profile's RTSP URL, returning ``(url, reason)``.

        On success returns ``(rtsp_url, "")``; on failure returns
        ``(None, reason)`` where ``reason`` is a human-readable explanation
        (camera unreachable, profile name mismatch with the available names,
        empty RTSP URL, etc.).
        """
        username = username or self._default_username
        password = password or self._default_password
        target = f"{camera.hostname}:{camera.port}"
        cameras = [{"hostname": camera.hostname, "port": camera.port}]

        try:
            results = list(
                read_camera_profiles(cameras, username=username, password=password)
            )
        except Exception as exc:  # pylint: disable=broad-exception-caught
            return None, f"failed to query camera {target}: {type(exc).__name__}: {exc}"

        if not results:
            return None, f"camera {target} returned no profile results"

        available: list[str] = []
        for result in results:
            if not result.ok:
                return None, f"camera {target} profile read failed: {result.error}"
            for profile in result.profiles:
                available.append(profile.name)
                if profile.name == profile_name:
                    if profile.rtsp_url:
                        url = self._inject_rtsp_credentials(profile.rtsp_url, username, password)
                        return url, ""
                    return None, f"profile '{profile_name}' on {target} has no RTSP URL"

        names = ", ".join(repr(name) for name in available) or "(none)"
        return None, (
            f"no profile named '{profile_name}' on {target}; "
            f"available profiles: {names} (names are case-sensitive)"
        )

    @staticmethod
    def _inject_rtsp_credentials(rtsp_url: str, username: str, password: str) -> str:
        """Return ``rtsp_url`` with ``username``/``password`` embedded in it.

        ONVIF cameras report their stream URL without credentials, but the
        stream itself is typically authenticated. GStreamer ``rtspsrc`` accepts
        credentials inline as ``rtsp://user:pass@host:port/path``. Userinfo is
        percent-encoded so special characters in the password are safe. When no
        credentials are supplied, or the URL already carries userinfo, the URL
        is returned unchanged.
        """
        if not username and not password:
            return rtsp_url
        parts = urlsplit(rtsp_url)
        if "@" in parts.netloc:
            return rtsp_url
        host = parts.hostname or ""
        if not host:
            return rtsp_url
        port = f":{parts.port}" if parts.port else ""
        userinfo = quote(username, safe="")
        if password:
            userinfo = f"{userinfo}:{quote(password, safe='')}"
        netloc = f"{userinfo}@{host}{port}"
        return urlunsplit(
            (parts.scheme, netloc, parts.path, parts.query, parts.fragment)
        )

    def _stop_pipeline(self, binding_id: str) -> None:
        with self._lock:
            process = self._active_processes.pop(binding_id, None)
            binding = self._binding_lookup.get(binding_id)
        if process is None:
            return

        if process.poll() is None:
            process.terminate()
            try:
                process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait(timeout=5)

        camera = binding.camera if binding is not None else CameraIdentity("unknown", 0)
        self._emit("pipeline_stopped", camera, {"binding_id": binding_id, "pid": process.pid})

    def _stop_all_processes(self) -> None:
        with self._lock:
            binding_ids = list(self._active_processes.keys())
        for binding_id in binding_ids:
            self._stop_pipeline(binding_id)

    def _emit(self, kind: str, camera: CameraIdentity, details: Dict[str, Any]) -> None:
        with self._lock:
            callbacks = list(self._callbacks)
        event = VideoEngineEvent(kind, camera, details)
        for callback in callbacks:
            with suppress(Exception):
                callback(event)

    def _runtime_state_to_dict(self, state: CameraRuntimeState) -> Dict[str, Any]:
        return {
            "camera": {
                "hostname": state.camera.hostname,
                "port": state.camera.port,
                "mac": state.camera.mac,
            },
            "status": state.status,
            "last_seen": state.last_seen.isoformat(),
            "pipeline_binding_ids": list(state.pipeline_binding_ids),
            "template_binding_ids": list(state.template_binding_ids),
            "error": state.error,
        }


__all__ = ["VideoEngine"]
