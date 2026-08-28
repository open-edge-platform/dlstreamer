"""Pick a varied, randomized subset of scenarios within the credit budget."""
from __future__ import annotations

import random
from collections import defaultdict

from .config import EST_CREDITS_PER_SCENARIO
from .models import Scenario


def sample_scenarios(
    scenarios: list[Scenario],
    seed: int,
    budget_credits: int,
    max_scenarios: int,
) -> list[Scenario]:
    """Stratify by category then round-robin randomly so runs stay diverse and unpredictable."""
    budget_cap = max(1, budget_credits // EST_CREDITS_PER_SCENARIO)
    target = min(max_scenarios, budget_cap, len(scenarios))

    rng = random.Random(seed)
    by_category: dict[str, list[Scenario]] = defaultdict(list)
    for scenario in scenarios:
        by_category[scenario.category].append(scenario)
    for bucket in by_category.values():
        rng.shuffle(bucket)

    categories = list(by_category)
    rng.shuffle(categories)

    picked: list[Scenario] = []
    while len(picked) < target and any(by_category[c] for c in categories):
        for category in categories:
            if by_category[category]:
                picked.append(by_category[category].pop())
                if len(picked) >= target:
                    break
    return picked
