"""Runtime configuration for the agent (CLI args + environment)."""
from __future__ import annotations

import argparse
import os
from dataclasses import dataclass, field
from datetime import datetime, timezone
from pathlib import Path

# Only these binaries may be invoked by executed scenarios (safety boundary).
DEFAULT_BINARY_ALLOWLIST = (
    "gst-launch-1.0",
    "gst-inspect-1.0",
    "python",
    "python3",
    "bash",
    "sh",
    "./",  # sample scripts invoked by relative path
    # Shell verbs and setup helpers commonly used in the documented command blocks.
    "cd",
    "export",
    "source",
    ".",
    "echo",
    "mkdir",
    "set",
    "unset",
    "wget",
    "curl",
)

# Rough pre-run estimate used only to size the sampled set against the budget.
EST_CREDITS_PER_SCENARIO = 15

# GitHub Models is the only supported LLM back-end (OpenAI-compatible endpoint).
GITHUB_MODELS_BASE_URL = "https://models.github.ai/inference"
DEFAULT_MODEL = "openai/gpt-4o"


@dataclass
class AgentConfig:
    repo_root: Path
    output_dir: Path
    model: str = DEFAULT_MODEL
    budget_credits: int = 500
    max_scenarios: int = 30
    command_timeout: int = 300
    max_retries: int = 2
    seed: int | None = None
    filter: str | None = None
    offline: bool = False  # no LLM calls; deterministic heuristic fallbacks
    dry_run: bool = False  # do not execute scenario commands
    open_pr: bool = False
    open_issue: bool = False
    binary_allowlist: tuple[str, ...] = field(default_factory=lambda: DEFAULT_BINARY_ALLOWLIST)

    @property
    def api_key(self) -> str | None:
        return os.environ.get("AI_AGENT_LLM_API_KEY")

    @property
    def base_url(self) -> str:
        return GITHUB_MODELS_BASE_URL


def _default_repo_root() -> Path:
    # tests/ai_agent/agent/config.py -> repo root is three levels up.
    return Path(__file__).resolve().parents[3]


def build_config(argv: list[str] | None = None) -> AgentConfig:
    parser = argparse.ArgumentParser(description="DL Streamer AI user-simulation agent (PoC)")
    parser.add_argument("--repo-root", type=Path, default=_default_repo_root())
    parser.add_argument("--output-dir", type=Path, default=None)
    parser.add_argument("--model", default=DEFAULT_MODEL)
    parser.add_argument("--budget-credits", type=int, default=500)
    parser.add_argument("--max-scenarios", type=int, default=30)
    parser.add_argument("--command-timeout", type=int, default=300)
    parser.add_argument("--max-retries", type=int, default=2,
                        help="LLM-guided retries when a launch fails as user-error")
    parser.add_argument("--seed", type=int, default=None)
    parser.add_argument("--filter", default=None,
                        help="only run scenarios whose id contains this substring")
    parser.add_argument("--offline", action="store_true", help="skip all LLM calls")
    parser.add_argument("--dry-run", action="store_true", help="plan + judge but do not run commands")
    parser.add_argument("--open-pr", action="store_true", help="auto-draft a PR for docs fixes")
    parser.add_argument("--open-issue", action="store_true", help="file an Issue for product bugs")
    args = parser.parse_args(argv)

    repo_root = args.repo_root.resolve()
    stamp = datetime.now(timezone.utc).strftime("%Y%m%d_%H%M%S")
    output_dir = (args.output_dir or (repo_root / "tests" / "ai_agent" / "out" / stamp)).resolve()

    offline = args.offline or not os.environ.get("AI_AGENT_LLM_API_KEY")

    return AgentConfig(
        repo_root=repo_root,
        output_dir=output_dir,
        model=args.model,
        budget_credits=args.budget_credits,
        max_scenarios=args.max_scenarios,
        command_timeout=args.command_timeout,
        max_retries=args.max_retries,
        seed=args.seed,
        filter=args.filter,
        offline=offline,
        dry_run=args.dry_run,
        open_pr=args.open_pr,
        open_issue=args.open_issue,
    )
