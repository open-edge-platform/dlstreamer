---
description: Convert the provided application into a native Intel DL Streamer / OpenVINO C++ application.
---

# Goal

Convert the provided application into a native Intel DL Streamer / OpenVINO C++
application.

# How this prompt is organized

This prompt is intentionally short. It is a **sequential orchestrator** — each
step links to a single, focused reference file under
[`.github/skills/dlstreamer-coding-agent/convert-app/`](../skills/dlstreamer-coding-agent/convert-app/).
Read the linked reference **at the moment you start a given step** (not all
at once at the beginning) so the relevant rules stay in active attention
while you work on that step.

| # | Step | Reference |
|---|---|---|
| 0 | Pipeline construction skill (always available) | [SKILL.md](../skills/dlstreamer-coding-agent/SKILL.md) |
| 1 | Scaffold from scratch (source + output dir + env) | [conversion-bootstrap.md](../skills/dlstreamer-coding-agent/convert-app/conversion-bootstrap.md) |
| 2 | Build the per-run deprecation deny-list | [deprecation-discovery.md](../skills/dlstreamer-coding-agent/convert-app/deprecation-discovery.md) |
| 3 | Review the source app + build the functional block inventory | (this file, §Step 3) |
| 4 | Plan model substitutions + 1-to-1 element mapping | [model-sourcing.md](../skills/dlstreamer-coding-agent/convert-app/model-sourcing.md) |
| 5 | Implement (pipeline, probes, paths, encoders, traceability) | [pipeline-implementation.md](../skills/dlstreamer-coding-agent/convert-app/pipeline-implementation.md) + [cmake-and-deliverables.md](../skills/dlstreamer-coding-agent/convert-app/cmake-and-deliverables.md) |
| 6 | Verify correctness (clean-shell runs + auto-fix loop) | [validation-protocol.md](../skills/dlstreamer-coding-agent/convert-app/validation-protocol.md) + [runsh-pitfalls.md](../skills/dlstreamer-coding-agent/convert-app/runsh-pitfalls.md) |
| 7 | Document the conversion | [documentation-spec.md](../skills/dlstreamer-coding-agent/convert-app/documentation-spec.md) |
| 8 | Final compliance audit (last gate before reporting completion) | [final-audit-checklist.md](../skills/dlstreamer-coding-agent/convert-app/final-audit-checklist.md) |

# Target platform

Intel hardware — iGPU / dGPU (Arc, Iris Xe), CPU, or NPU. Default to `GPU`
with CPU fallback unless the user specifies otherwise.

# Checkpoint protocol (mandatory liveness signal)

After completing each numbered step below, the agent MUST emit a one-line
checkpoint message of the form
`Checkpoint N/8: <step name> — <one-line outcome>` AND make at least one tool
call as part of that checkpoint (typically `manage_todo_list` to flip the step
from `in-progress` to `completed` and the next from `not-started` to
`in-progress`).

If a step is naturally large (e.g. step 5 Implement), emit additional
intra-step checkpoints (`Checkpoint 5/8a`, `5/8b`, …) every time a major
sub-artifact is created (CMakeLists, main `.cpp`, `run.sh`, `export_models.sh`).

**Never go more than ~3 minutes of wall-clock time without a tool call or a
checkpoint message.** This guarantees a visible liveness signal lands in the
chat for every step, so the user can distinguish a working agent from a
stalled one even during a single long turn. If a substep is genuinely
long-running (e.g. `cmake --build`, `omz_downloader`), break it up: emit a
short `Checkpoint N/8x: <substep> — starting` before launching the command,
then a follow-up checkpoint with the outcome when it returns.

# Steps

## Step 1 — Scaffold from scratch

Execute the procedure in
[conversion-bootstrap.md](../skills/dlstreamer-coding-agent/convert-app/conversion-bootstrap.md):

- Resolve workspace root.
- Idempotent source clone.
- Create a **fresh numbered output directory** (`<app>_NNN/`).
- Announce the directory name to the user up-front.
- Apply the mandatory env-setup recipe (§4.1) in full, run the verification
  (§4.2), and if anything reports `MISSING:` apply the registry-cache
  recovery (§4.3) before proceeding.

All subsequent file writes happen inside the newly-created numbered directory.
The exact same env-setup recipe MUST also be wired verbatim into the generated
`run.sh` (see [cmake-and-deliverables.md](../skills/dlstreamer-coding-agent/convert-app/cmake-and-deliverables.md) §4).

## Step 2 — Run the upstream deprecation discovery

Execute the grep procedure in
[deprecation-discovery.md](../skills/dlstreamer-coding-agent/convert-app/deprecation-discovery.md)
and build the per-run deny-list. Keep it handy as the authoritative deny-list
for steps 4–6.

## Step 3 — Review the source app + build the functional block inventory

Review the cloned source application to understand its functionality,
structure, and generated output. Then build a **functional block inventory**
— a numbered list of every distinct:

- inference stage (PGIE, SGIE, secondary detectors/classifiers),
- pre/post-processing step,
- tracking,
- analytics,
- visualization element

…together with what each block contributes to the visible output (bounding
boxes, labels, counters, overlay text). This inventory is the contract for
step 4 — every block listed here MUST receive a dedicated counterpart in the
converted pipeline.

## Step 4 — Plan the conversion

Map **every** functional block from the step-3 inventory to a corresponding
DL Streamer element or OpenVINO API. The mapping MUST be **strictly 1-to-1**
— simplifications, merges, and stage drops are NOT permitted. Example: an
N-stage cascade in the source app (e.g. detect → detect → classify) becomes
an N-stage cascade in the conversion using the equivalent DL Streamer
elements (e.g. `gvadetect` → `gvadetect` on upstream ROIs → `gvaclassify`).
The only exception is blocks explicitly listed under *Scope → Exclude* in
[cmake-and-deliverables.md](../skills/dlstreamer-coding-agent/convert-app/cmake-and-deliverables.md).

For model substitutions, precision, OCR language defaults, and device
mapping, follow
[model-sourcing.md](../skills/dlstreamer-coding-agent/convert-app/model-sourcing.md).

The agent MUST make all element-mapping decisions **autonomously** based on
DL Streamer documentation, installed element properties
(`gst-inspect-1.0 <element>`), sample code, and the coding-agent skill —
**without** asking the user for confirmation. If multiple elements could
satisfy a block, pick the best match (highest performance, lowest deprecation
risk, closest semantic equivalence) and document the choice with a one-line
justification in the README.

Cross-check every chosen element/property against the deny-list built in
step 2.

## Step 5 — Implement

Implement the conversion in C++ following
[pipeline-implementation.md](../skills/dlstreamer-coding-agent/convert-app/pipeline-implementation.md)
end-to-end (see its §§1–10 for the full set of rules).

Generate the build system and wrapper per
[cmake-and-deliverables.md](../skills/dlstreamer-coding-agent/convert-app/cmake-and-deliverables.md):
`CMakeLists.txt` (with mandatory adjustments), `run.sh` (with `--help`,
`--sink display|file|fake`, headless auto-detection, pre-flight resource
audit), and `export_models.sh` if applicable.

Cross-check every element, property, file format, and metadata API against
the deny-list from step 2.

## Step 6 — Verify correctness

Run the full validation protocol in
[validation-protocol.md](../skills/dlstreamer-coding-agent/convert-app/validation-protocol.md):

1. Capture baseline outputs (if the original can be run on this host).
2. Build (`cmake -S . -B build && cmake --build build -j$(nproc)`).
3. Clean-shell `./run.sh` runs (`env -i …`) for all five invocations
   (`--help`, defaults, `--sink fake`, `--sink file`, `--sink invalid`) with
   the **auto-fix loop**.
4. Compare against baseline (if available).
5. Benchmark.
6. Deprecated-API scan (dynamic grep against the step-2 deny-list).

When a clean-shell run fails, map the error to
[runsh-pitfalls.md](../skills/dlstreamer-coding-agent/convert-app/runsh-pitfalls.md),
apply the fix, and **re-run every test** — fixes for one bug frequently
expose the next.

## Step 7 — Document

Generate the README **strictly** following
[documentation-spec.md](../skills/dlstreamer-coding-agent/convert-app/documentation-spec.md) — see its §1
(required README sections) and §3 (Conversion Notes sub-sections) for the
full contract.

Source traceability comments in the C++ code are mandatory and are enforced
by [pipeline-implementation.md](../skills/dlstreamer-coding-agent/convert-app/pipeline-implementation.md)
§9 — not by the README itself.

## Step 8 — Final requirements compliance audit

Walk through the checklist in
[final-audit-checklist.md](../skills/dlstreamer-coding-agent/convert-app/final-audit-checklist.md)
and verify every requirement with a concrete artifact or test result. Emit
the filled checklist (with `[x]` / `[ ]` and a one-line note per item) in the
final user-facing summary message. Any remaining `[ ]` items block completion
— fix them and re-audit.
