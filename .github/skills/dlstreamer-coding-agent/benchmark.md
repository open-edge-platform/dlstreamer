# Skill Benchmark: dlstreamer-coding-agent

**Model**: claude-opus-4.6
**Date**: 2026-08-05T21:51:03Z
**Evals**: 1, 2, 3, 4, 5, 6, 7 (1 run(s) each per configuration)

## Summary

> **How to read this table** — **Avg** is the mean score across all evals; **Std Dev** (the ± spread) measures how much individual evals varied around that average — small spread means the agent behaved consistently, large spread means results were erratic; **Skill Lift** is the gain from loading the skill (with − without).

| Metric | Avg ± Std Dev (With Skill) | Avg ± Std Dev (Without Skill) | Skill Lift (Δ) |
|--------|---------------------------|-------------------------------|----------------|
| Pass Rate (% correct) | 98% avg, ±6% spread (consistent) | 77% avg, ±14% spread (variable) | +21pp |
| Time (s / question) | 70.6s avg, ±32.3s spread (variable) | 29.0s avg, ±4.4s spread (variable) | +41.6s |
| Tokens (context cost) | 325k avg, ±249k spread (unreliable) | 26k avg, ±377 spread (consistent) | +300k |