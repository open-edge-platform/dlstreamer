<!--
SPDX-FileCopyrightText: (C) 2026 Intel Corporation
SPDX-License-Identifier: Apache-2.0
-->

# Skill Benchmark: dlstreamer-coding-agent

**Agents**: Copilot (`claude-opus-4.6`)  
**Grader**: Copilot (`gpt-5.3-codex`)  
**Date**: 2026-08-26T08:55:09Z  
**Evals**: 1, 2, 3, 4, 5, 6, 7 (1 run per configuration)

## Summary

> Skill lift = with skill − without skill. ↑ = better, ↓ = higher cost (expected).

### Evals passed

| Agent | w/o skill | w/ skill | Lift |
|---|---|---|---|
| Copilot (`claude-opus-4.6`) | 1 / 7 | 7 / 7 | **+6 ↑** |

### Pass rate (avg ± σ across evals)

| Agent | w/o skill | w/ skill | Lift |
|---|---|---|---|
| Copilot (`claude-opus-4.6`) | 76% ±14% | 100% ±0% | **+24pp ↑** |

### Time (total across all evals)

| Agent | w/o skill | w/ skill | Lift |
|---|---|---|---|
| Copilot (`claude-opus-4.6`) | 442 s | 855 s | +414 s ↓ |

### Tokens (total across all evals)

| Agent | w/o skill | w/ skill | Lift |
|---|---|---|---|
| Copilot (`claude-opus-4.6`) | 741k | 3729k | +2988k ↓ |

## Per-Eval Detail

> Each cell is PASS/FAIL for that run, with the count of expectations met in parentheses (e.g. `PASS (5/5)`); `n/a` means no grading.json was found for that (eval, config, agent) combination.

| Eval | Prompt | Copilot (w/) | Copilot (w/o) |
|---|---|---|---|
| 1 | Model conversion from an Ultralytics YOLO to OpenVINO IR format for use with DL ... | PASS (4/4) | PASS (4/4) |
| 2 | Model conversion from Hugging Face ViT to OpenVINO IR format for use with DL Str... | PASS (5/5) | FAIL (4/5) |
| 3 | Object detection with YOLO26 using DL Streamer pipeline | PASS (5/5) | FAIL (3/5) |
| 4 | Object tracking with YOLO26 using DL Streamer pipeline | PASS (6/6) | FAIL (5/6) |
| 5 | Multi-RTSP-stream analysis with AI analytics, recording, and WebRTC output using... | PASS (6/6) | FAIL (4/6) |
| 6 | People detection and tracking with YOLO26m and mars-small128 re-ID (Deep Sort Tr... | PASS (5/5) | FAIL (4/5) |
| 7 | Multi-stream pose estimation with 4 YOLO pose models composed into single output... | PASS (5/5) | FAIL (3/5) |
| | **Mean ±σ** | **100% ±0%** | **76% ±14%** |