"""Run scenario steps inside the container, enforcing the binary allowlist."""
from __future__ import annotations

import subprocess
import time
from pathlib import Path

from .config import AgentConfig
from .models import ExecResult

_ASSIGN = __import__("re").compile(r"^[A-Za-z_][A-Za-z0-9_]*=")


def _first_binary(command_line: str) -> str | None:
    for token in command_line.strip().split():
        if _ASSIGN.match(token):  # skip leading VAR=value env assignments
            continue
        return token
    return None


def _is_allowed(command: str, allowlist: tuple[str, ...]) -> bool:
    for line in command.splitlines():
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        binary = _first_binary(line)
        if binary is None:
            continue
        if not any(binary == a or binary.startswith(a) for a in allowlist):
            return False
    return True


def run_script(commands: list[str], workdir: str, timeout: int, cfg: AgentConfig) -> ExecResult:
    """Run all of a scenario's commands in one shell so source/export/cd persist across them."""
    script = "\n".join(commands)
    if not _is_allowed(script, cfg.binary_allowlist):
        return ExecResult(script, exit_code=126, stdout="",
                          stderr="blocked: command not in binary allowlist", duration_s=0.0)
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
