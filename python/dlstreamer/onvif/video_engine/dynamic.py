# ==============================================================================
# Copyright (C) 2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================
"""Event-driven (restart-based) dynamic pipeline control.

This module wires ONVIF camera events to analytics-pipeline changes using a
restart-based strategy (Option A): modifying a pipeline means stopping the
running process and spawning a new one. Responsibilities are split into small,
single-purpose classes so each functional concern is clearly separated:

* :class:`EventRuleMatcher`       — decides whether a notification fires a rule.
* :class:`PipelineActionExecutor` — turns a matched rule into a pipeline action.
* :class:`CameraEventWorker`      — consumes ONVIF events for one camera.
* :class:`DynamicPipelineController` — orchestrates rules, workers and dispatch.
"""

from __future__ import annotations

import json
import threading
import time
from contextlib import suppress
from pathlib import Path
from typing import Any, Callable, Dict, List, Optional

from ..event_manager import EventNotification
from ..event_manager.engine import OnvifEventEngine

from .engine import VideoEngine
from .types import (
    CameraIdentity,
    EventRule,
    EventTrigger,
    PipelineAction,
    PipelineBinding,
    as_camera_identity,
    pipeline_list,
)

NotificationHandler = Callable[[CameraIdentity, EventNotification], None]


class EventRuleParser:
    """Build :class:`EventRule` objects from plain dict/JSON payloads.

    Keeps deserialization separate from matching and execution so the config
    format can evolve without touching the runtime classes.
    """

    def parse_rules(
        self,
        payload: Any,
        pipeline_library: Optional[Dict[str, Any]] = None,
    ) -> List[EventRule]:
        """Parse a list/mapping payload into a list of :class:`EventRule`."""
        if isinstance(payload, dict):
            entries = payload.get("rules", [])
        elif isinstance(payload, list):
            entries = payload
        else:
            raise ValueError("dynamic-rule configuration must be a mapping or a list")

        return [self.parse_rule(entry, pipeline_library) for entry in entries]

    def parse_rule(
        self,
        entry: Any,
        pipeline_library: Optional[Dict[str, Any]] = None,
    ) -> EventRule:
        """Parse a single rule mapping into an :class:`EventRule`."""
        if not isinstance(entry, dict):
            raise ValueError("each rule entry must be a mapping")

        name = entry.get("name")
        if not name:
            raise ValueError("rule entry requires a name")

        camera = as_camera_identity(entry.get("camera", entry))

        action_value = str(entry.get("action", "")).lower()
        try:
            action = PipelineAction(action_value)
        except ValueError as exc:
            valid = ", ".join(item.value for item in PipelineAction)
            raise ValueError(f"rule '{name}' has invalid action '{action_value}' (expected one of: {valid})") from exc

        target_binding_id = entry.get("target_binding_id")
        if not target_binding_id:
            raise ValueError(f"rule '{name}' requires target_binding_id")

        trigger = self._parse_trigger(entry.get("trigger", {}), name)

        pipeline = entry.get("pipeline")
        pipeline_ref = entry.get("pipeline_ref")
        if pipeline is None and pipeline_ref is not None:
            if not pipeline_library or pipeline_ref not in pipeline_library:
                raise ValueError(
                    f"rule '{name}' references unknown pipeline_ref '{pipeline_ref}'"
                )
            pipeline = pipeline_library[pipeline_ref]
        if action in (PipelineAction.ADD, PipelineAction.MODIFY) and pipeline is None:
            raise ValueError(
                f"rule '{name}' requires a pipeline or pipeline_ref for action '{action.value}'"
            )

        return EventRule(
            name=str(name),
            camera=camera,
            trigger=trigger,
            action=action,
            target_binding_id=str(target_binding_id),
            pipeline=pipeline,
            profile_name=entry.get("profile_name"),
            username=str(entry.get("username", "")),
            password=str(entry.get("password", "")),
        )

    def _parse_trigger(self, payload: Any, rule_name: str) -> EventTrigger:
        if not isinstance(payload, dict):
            raise ValueError(f"rule '{rule_name}' trigger must be a mapping")
        data_equals = payload.get("data_equals", {}) or {}
        if not isinstance(data_equals, dict):
            raise ValueError(f"rule '{rule_name}' trigger.data_equals must be a mapping")
        return EventTrigger(
            topic_contains=payload.get("topic_contains"),
            property_operation=payload.get("property_operation"),
            data_equals={str(k): str(v) for k, v in data_equals.items()},
        )


class EventRuleMatcher:  # pylint: disable=too-few-public-methods
    """Evaluate an :class:`EventNotification` against an :class:`EventTrigger`.

    Stateless: matching is pure and side-effect free, which keeps the decision
    logic isolated from event transport and pipeline mutation.
    """

    def matches(self, rule: EventRule, notification: EventNotification) -> bool:
        """Return True when *notification* satisfies the rule's trigger."""
        trigger = rule.trigger

        if trigger.topic_contains and trigger.topic_contains not in notification.topic:
            return False

        if trigger.property_operation and trigger.property_operation != notification.property_operation:
            return False

        for key, expected in trigger.data_equals.items():
            if notification.data.get(key) != expected:
                return False

        return True


class PipelineActionExecutor:  # pylint: disable=too-few-public-methods
    """Apply the pipeline action described by a matched rule to the engine.

    Encapsulates the mapping ``PipelineAction -> VideoEngine call`` so the
    controller does not need to know how each action is realized.
    """

    def __init__(self, engine: VideoEngine, *, verbose: bool = False) -> None:
        self._engine = engine
        self._verbose = verbose

    def execute(self, rule: EventRule) -> None:
        """Realize the rule's :class:`PipelineAction` on the engine."""
        action = rule.action
        if self._verbose:
            print(f"[dynamic] rule '{rule.name}' -> {action.value} on '{rule.target_binding_id}'")

        if action == PipelineAction.ADD:
            self._engine.add_pipeline(self._binding_from_rule(rule))
        elif action == PipelineAction.MODIFY:
            if rule.pipeline is None:
                raise ValueError(f"rule '{rule.name}' MODIFY requires a pipeline")
            self._engine.replace_pipeline(
                rule.target_binding_id,
                rule.pipeline,
                camera=rule.camera,
                profile_name=rule.profile_name,
                username=rule.username,
                password=rule.password,
            )
        elif action == PipelineAction.REMOVE:
            self._engine.remove_pipeline(rule.target_binding_id)
        elif action == PipelineAction.RESTART:
            self._engine.restart_pipeline(rule.target_binding_id)

    def _binding_from_rule(self, rule: EventRule) -> PipelineBinding:
        if rule.pipeline is None:
            raise ValueError(f"rule '{rule.name}' ADD requires a pipeline")
        return PipelineBinding(
            camera=rule.camera,
            pipeline=pipeline_list(rule.pipeline),
            binding_id=rule.target_binding_id,
            events=(),
            profile_name=rule.profile_name,
            username=rule.username,
            password=rule.password,
        )


class CameraEventWorker(threading.Thread):  # pylint: disable=too-many-instance-attributes
    """Background thread pulling ONVIF events for a single camera.

    Owns one :class:`OnvifEventEngine` subscription and forwards every parsed
    notification to ``on_notification``. It knows nothing about rules or
    pipelines — its only concern is reliable event transport.
    """

    def __init__(  # pylint: disable=too-many-arguments
        self,
        camera: CameraIdentity,
        username: str,
        password: str,
        on_notification: NotificationHandler,
        stop_event: threading.Event,
        *,
        verbose: bool = False,
        poll_timeout: str = "PT30S",
        renew_every: float = 300.0,
        reconnect_delay: float = 5.0,
    ) -> None:
        super().__init__(name=f"dynamic-events-{camera.key()}", daemon=True)
        self._camera = camera
        self._username = username
        self._password = password
        self._on_notification = on_notification
        self._stop_event = stop_event
        self._verbose = verbose
        self._poll_timeout = poll_timeout
        self._renew_every = max(1.0, float(renew_every))
        self._reconnect_delay = max(0.5, float(reconnect_delay))

    def run(self) -> None:
        while not self._stop_event.is_set():
            engine = OnvifEventEngine(
                self._camera.hostname,
                self._camera.port,
                self._username,
                self._password,
                self._verbose,
            )
            try:
                engine.subscribe(termination_time="PT1H")
                self._pull_loop(engine)
            except Exception as exc:  # pylint: disable=broad-exception-caught
                if self._verbose:
                    print(f"[dynamic] event worker {self._camera.key()} error: {type(exc).__name__}: {exc}")
            finally:
                with suppress(Exception):
                    engine.close()

            if self._stop_event.wait(self._reconnect_delay):
                break

    def _pull_loop(self, engine: OnvifEventEngine) -> None:
        last_renew = time.monotonic()
        while not self._stop_event.is_set():
            batch = engine.pull(timeout=self._poll_timeout, limit=100)
            for notification in batch:
                if self._stop_event.is_set():
                    return
                self._on_notification(self._camera, notification)

            if time.monotonic() - last_renew >= self._renew_every:
                with suppress(Exception):
                    engine.renew("PT1H")
                last_renew = time.monotonic()


class DynamicPipelineController:  # pylint: disable=too-many-instance-attributes
    """Orchestrate event-driven pipeline changes for a :class:`VideoEngine`.

    Groups rules by camera, runs one :class:`CameraEventWorker` per camera and
    dispatches each notification through the matcher and the executor.

    ONVIF PullPoint notifications carry no unique event id and cameras commonly
    re-send the *current* property state on every poll. To avoid running an
    action repeatedly for the same event, the controller tracks the last seen
    state per ``(camera, topic, source)`` and only evaluates rules when that
    state actually changes (a real transition), ignoring identical repeats.
    """

    def __init__(self, engine: VideoEngine, *, verbose: bool = False) -> None:
        self._engine = engine
        self._verbose = verbose
        self._matcher = EventRuleMatcher()
        self._executor = PipelineActionExecutor(engine, verbose=verbose)
        self._parser = EventRuleParser()

        self._lock = threading.RLock()
        self._rules: List[EventRule] = []
        self._workers: Dict[str, CameraEventWorker] = {}
        self._stop_event = threading.Event()
        # Last observed property state per (camera, topic, source); used to
        # de-duplicate repeated notifications so actions fire only on change.
        self._last_states: Dict[Any, tuple] = {}
        self._default_username: str = ""
        self._default_password: str = ""
        self._pipeline_library: Dict[str, Any] = {}

    def set_default_credentials(self, username: str, password: str) -> None:
        """Set fallback ONVIF credentials used when a rule omits them."""
        self._default_username = username or ""
        self._default_password = password or ""

    def add_rule(self, rule: EventRule) -> None:
        """Register a single rule after validating its action/pipeline pair."""
        if rule.action in (PipelineAction.ADD, PipelineAction.MODIFY) and rule.pipeline is None:
            raise ValueError(f"rule '{rule.name}' requires a pipeline for action {rule.action.value}")
        with self._lock:
            self._rules.append(rule)

    def add_rules(self, rules: List[EventRule]) -> None:
        """Register several rules in order."""
        for rule in rules:
            self.add_rule(rule)

    def load_rules(self, config_path: str | Path) -> List[EventRule]:
        """Load event->action rules from a JSON file and register them.

        Rules may embed their pipeline inline or reference a named pipeline via
        ``pipeline_ref`` resolved from the pipeline library previously loaded
        with :meth:`load_pipeline_library`.
        """
        path = Path(config_path)
        payload = json.loads(path.read_text(encoding="utf-8"))
        rules = self._parser.parse_rules(payload, self._pipeline_library)
        self.add_rules(rules)
        return rules

    def load_pipeline_library(self, config_path: str | Path) -> Dict[str, Any]:
        """Load named pipeline definitions referenced by rule ``pipeline_ref``.

        The file is a mapping ``{"pipelines": {id: pipeline}}`` (or a bare
        ``{id: pipeline}`` mapping) where each pipeline is a command list or
        string. Cameras live in the rules, not here.
        """
        path = Path(config_path)
        payload = json.loads(path.read_text(encoding="utf-8"))
        library = payload.get("pipelines", payload) if isinstance(payload, dict) else payload
        if not isinstance(library, dict):
            raise ValueError("pipeline library must be a mapping of id -> pipeline")
        self._pipeline_library = {str(key): pipeline_list(value) for key, value in library.items()}
        return self._pipeline_library

    def list_rules(self) -> List[EventRule]:
        """Return a snapshot copy of the currently registered rules."""
        with self._lock:
            return list(self._rules)

    def start(self) -> None:
        """Start one event worker per camera referenced by the rules."""
        self._stop_event.clear()
        with self._lock:
            self._last_states.clear()
            cameras = self._cameras_by_key()
            for key, (camera, username, password) in cameras.items():
                worker = self._workers.get(key)
                if worker is not None and worker.is_alive():
                    continue
                worker = CameraEventWorker(
                    camera,
                    username,
                    password,
                    self._dispatch,
                    self._stop_event,
                    verbose=self._verbose,
                )
                self._workers[key] = worker
                worker.start()

    def stop(self) -> None:
        """Signal all workers to stop and wait for them to finish."""
        self._stop_event.set()
        with self._lock:
            workers = list(self._workers.values())
            self._workers.clear()
            self._last_states.clear()
        for worker in workers:
            if worker.is_alive():
                worker.join(timeout=2.0)

    def _cameras_by_key(self) -> Dict[str, tuple[CameraIdentity, str, str]]:
        cameras: Dict[str, tuple[CameraIdentity, str, str]] = {}
        for rule in self._rules:
            key = rule.camera.key()
            if key not in cameras:
                username = rule.username or self._default_username
                password = rule.password or self._default_password
                cameras[key] = (rule.camera, username, password)
        return cameras

    @staticmethod
    def _event_state_key(camera: CameraIdentity, notification: EventNotification) -> tuple:
        """Identity of the *thing* a notification is about (not its value).

        Combines camera, ONVIF topic and the notification ``Source`` items so
        that state changes are tracked independently per source/channel.
        """
        source = tuple(sorted(notification.source.items()))
        return (camera.key(), notification.topic, source)

    @staticmethod
    def _event_state_value(notification: EventNotification) -> tuple:
        """The current property state, excluding the ever-changing timestamp.

        ``utc_time`` is intentionally ignored so that a camera re-sending the
        same state produces an identical value and is treated as a duplicate.
        """
        data = tuple(sorted(notification.data.items()))
        return (notification.property_operation, data)

    def _is_duplicate_event(self, camera: CameraIdentity, notification: EventNotification) -> bool:
        """Return True when this notification repeats the last seen state.

        Updates the stored state as a side effect, so a genuine transition
        (e.g. motion true -> false -> true) is not suppressed.
        """
        key = self._event_state_key(camera, notification)
        value = self._event_state_value(notification)
        with self._lock:
            if self._last_states.get(key) == value:
                return True
            self._last_states[key] = value
        return False

    def _dispatch(self, camera: CameraIdentity, notification: EventNotification) -> None:
        if self._is_duplicate_event(camera, notification):
            if self._verbose:
                print(
                    f"[dynamic] duplicate event ignored: {notification.topic or '<unknown>'} "
                    f"op={notification.property_operation or '-'}"
                )
            return
        with self._lock:
            rules = [rule for rule in self._rules if rule.camera.key() == camera.key()]
        for rule in rules:
            if not self._matcher.matches(rule, notification):
                continue
            try:
                if self._verbose:
                    print(
                        f"[dynamic] rule '{rule.name}' matched event "
                        f"{notification.topic or '<unknown>'} "
                        f"op={notification.property_operation or '-'} — "
                        f"executing action '{rule.action.value}'"
                    )
                self._executor.execute(rule)
            except Exception as exc:  # pylint: disable=broad-exception-caught
                if self._verbose:
                    print(f"[dynamic] action failed for rule '{rule.name}': {type(exc).__name__}: {exc}")


__all__ = [
    "EventRuleParser",
    "EventRuleMatcher",
    "PipelineActionExecutor",
    "CameraEventWorker",
    "DynamicPipelineController",
]
