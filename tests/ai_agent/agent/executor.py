"""Run a scenario's commands in one shell, guarding only against destructive commands."""
from __future__ import annotations

import re
import subprocess
import time
from pathlib import Path

from .config import AgentConfig
from .models import ExecResult


def _is_dangerous(script: str, patterns: tuple[str, ...]) -> bool:
    return any(re.search(pat, script, re.IGNORECASE) for pat in patterns)


def run_script(commands: list[str], workdir: str, timeout: int, cfg: AgentConfig) -> ExecResult:
    """Run all of a scenario's commands in one shell so source/export/cd persist across them."""
    script = "\n".join(commands)
    if _is_dangerous(script, cfg.deny_patterns):
        return ExecResult(script, exit_code=126, stdout="",
                          stderr="blocked: matched a denied dangerous pattern", duration_s=0.0)
    if cfg.dry_run:
        return ExecResult(script, exit_code=0, stdout="", stderr="", duration_s=0.0, skipped=True)

    cwd = cfg.repo_root / workdir if workdir else cfg.repo_root
    if not Path(cwd).is_dir():
        cwd = cfg.repo_root
    start = time.monotonic()
    try:
        proc = subprocess.run(
            ["bash", "-c", script],
            cwd=str(cwd),
            capture_output=True,
            text=True,
            timeout=timeout,
            stdin=subprocess.DEVNULL,  # so sudo/other prompts fail fast instead of hanging
        )
        # Exit code of a multi-line bash -c is the last command's (the launch).
        return ExecResult(script, proc.returncode, proc.stdout, proc.stderr,
                          time.monotonic() - start)
    except subprocess.TimeoutExpired as exc:
        return ExecResult(script, exit_code=124, stdout=exc.stdout or "",
                          stderr="timeout", duration_s=time.monotonic() - start, timed_out=True)
    except OSError as exc:
        return ExecResult(script, exit_code=127, stdout="", stderr=str(exc),
                          duration_s=time.monotonic() - start)
