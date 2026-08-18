<!--
SPDX-FileCopyrightText: (C) 2026 Intel Corporation
SPDX-License-Identifier: Apache-2.0
-->

# Skill Benchmark: dlstreamer-coding-agent

**Agents**: Copilot (`claude-haiku-4.5`)  
**Grader**: Copilot (`gpt-5.3-codex`)  
**Date**: 2026-08-07T08:55:40Z  
**Evals**: 1, 2, 3, 4, 5, 6, 7 (1 run per configuration)

## Summary

> Skill lift = with skill − without skill. ↑ = better, ↓ = higher cost (expected).

### Evals passed

| Agent | w/o skill | w/ skill | Lift |
|---|---|---|---|
| Copilot (`claude-haiku-4.5`) | 0 / 7 | 2 / 7 | **+2 ↑** |

### Pass rate (avg ± σ across evals)

| Agent | w/o skill | w/ skill | Lift |
|---|---|---|---|
| Copilot (`claude-haiku-4.5`) | 45% ±20% | 75% ±34% | **+30pp ↑** |

### Time (total across all evals)

| Agent | w/o skill | w/ skill | Lift |
|---|---|---|---|
| Copilot (`claude-haiku-4.5`) | 257 s | 808 s | +551 s ↓ |

### Tokens (total across all evals)

| Agent | w/o skill | w/ skill | Lift |
|---|---|---|---|
| Copilot (`claude-haiku-4.5`) | 273k | 1892k | +1619k ↓ |

## Per-Eval Detail

> Each cell is PASS/FAIL for that run, with the count of expectations met in parentheses (e.g. `PASS (5/5)`); `n/a` means no grading.json was found for that (eval, config, agent) combination.

| Eval | Prompt | Copilot (w/) | Copilot (w/o) |
|---|---|---|---|
| 1 | Model conversion from an Ultralytics YOLO to OpenVINO IR format for use with DL ... | PASS (4/4) | FAIL (3/4) |
| 2 | Model conversion from Hugging Face ViT to OpenVINO IR format for use with DL Str... | FAIL (0/5) | FAIL (3/5) |
| 3 | Object detection with YOLO26 using DL Streamer pipeline | FAIL (4/5) | FAIL (2/5) |
| 4 | Object tracking with YOLO26 using DL Streamer pipeline | PASS (6/6) | FAIL (3/6) |
| 5 | Multi-RTSP-stream analysis with AI analytics, recording, and WebRTC output using... | FAIL (5/6) | FAIL (3/6) |
| 6 | People detection and tracking with YOLO26m and Mars re-ID using DL Streamer pipe... | FAIL (4/5) | FAIL (1/5) |
| 7 | Multi-stream pose estimation with 4 YOLO pose models composed into single output... | FAIL (4/5) | FAIL (1/5) |
| | **Mean ±σ** | **75% ±34%** | **45% ±20%** |