"""Shared data structures for the agent pipeline."""
from __future__ import annotations

from dataclasses import dataclass, field
from typing import Any


@dataclass
class Scenario:
    id: str
    source: str
    category: str
    commands: list[str] = field(default_factory=list)
    env_requirements: list[str] = field(default_factory=list)
    expected: list[str] = field(default_factory=list)
    workdir: str = ""


@dataclass
class Step:
    command: str
    workdir: str = ""
    timeout: int = 300


@dataclass
class Plan:
    scenario_id: str
    steps: list[Step] = field(default_factory=list)
    assertions: list[str] = field(default_factory=list)


@dataclass
class ExecResult:
    command: str
    exit_code: int
    stdout: str
    stderr: str
    duration_s: float
    skipped: bool = False
    timed_out: bool = False


@dataclass
class Verdict:
    scenario_id: str
    category: str  # pass | user-error | docs-bug | product-bug | flaky
    confidence: float
    reason: str
    evidence: dict[str, Any] = field(default_factory=dict)
    repro: list[str] = field(default_factory=list)
