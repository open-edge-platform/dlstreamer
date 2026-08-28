"""Orchestrator: mine -> sample -> plan -> execute -> judge -> report."""
from __future__ import annotations

import random
import sys

from .config import build_config
from .executor import run_step
from .judge import judge_outcome
from .llm_client import BudgetExceeded, LLMClient
from .planner import build_plan
from .report import write_reports
from .scenario_miner import mine_scenarios
from .scenario_sampler import sample_scenarios


def main(argv: list[str] | None = None) -> int:
    cfg = build_config(argv)
    seed = cfg.seed if cfg.seed is not None else random.SystemRandom().randrange(2**32)

    print(f"[agent] repo_root={cfg.repo_root}")
    print(f"[agent] seed={seed} offline={cfg.offline} dry_run={cfg.dry_run} "
          f"model={cfg.model} budget={cfg.budget_credits}")
    if cfg.open_pr or cfg.open_issue:
        print("[agent] note: auto PR/Issue is not implemented in Phase 0; running advisory-only.")

    all_scenarios = mine_scenarios(cfg.repo_root)
    print(f"[agent] mined {len(all_scenarios)} runnable scenarios")
    picked = sample_scenarios(all_scenarios, seed, cfg.budget_credits, cfg.max_scenarios)
    print(f"[agent] sampled {len(picked)} scenarios "
          f"({sorted({s.category for s in picked})})")

    llm = LLMClient(cfg.model, cfg.api_key, cfg.budget_credits, cfg.offline, cfg.base_url)

    verdicts = []
    for i, scenario in enumerate(picked, 1):
        print(f"[agent] ({i}/{len(picked)}) {scenario.id}")
        try:
            plan = build_plan(scenario, llm, cfg.command_timeout)
            results = [run_step(step, cfg) for step in plan.steps]
            verdict = judge_outcome(scenario, results, llm)
        except BudgetExceeded:
            print("[agent] budget exhausted; stopping scenario loop")
            break
        verdicts.append(verdict)
        print(f"        -> {verdict.category} ({verdict.confidence:.2f})")

    summary = {
        "seed": seed,
        "model": cfg.model,
        "offline": cfg.offline,
        "dry_run": cfg.dry_run,
        "budget_credits": cfg.budget_credits,
        "credits_spent": round(llm.spent_credits, 1),
        "llm_calls": llm.calls,
        "scenarios_mined": len(all_scenarios),
        "scenarios_run": len(verdicts),
    }
    report_path = write_reports(cfg.output_dir, summary, verdicts)
    print(f"[agent] report: {report_path}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
