# Validation Protocol

Used by **step 6 (Verify correctness)** of [`convert-app.prompt.md`](../../../prompts/convert-app.prompt.md).

## 1. Capture baseline outputs

First reproduce the original application's environment (Docker image,
virtualenv, CUDA/driver requirements, command-line invocation) and document
those steps in the README. Then execute the original application with a
representative set of inputs and record its outputs (annotated frames, JSON,
logs, metrics).

If the original cannot be run (missing hardware, encrypted models, unavailable
SDK), explicitly state this in the README and rely on step 3's correctness
checks as the primary validation signal.

## 2. Build the converted application — MANDATORY, NON-NEGOTIABLE

Build the binary:

```bash
cmake -S . -B build && cmake --build build -j$(nproc)
```

Resolve every compilation error before proceeding. Do not hand off a binary
that failed to build, or that has never been built in this run. Direct binary
invocation for correctness/output checks is covered by step 3 (which exercises
the wrapper); do not duplicate runs here.

## 3. Verify the `run.sh` wrapper — MANDATORY, in a CLEAN SHELL, with auto-fix loop

Before reporting the conversion as finished, execute `./run.sh` and confirm it
succeeds end-to-end. The check has the following non-negotiable requirements.

### 3a. Run in a pristine shell

The agent's own terminal session almost always already has DL Streamer env
vars exported (from previous `source setup_dls_env.sh`, prior pipelines, etc.).
This hides bugs in `run.sh` that only manifest for the end user. Invoke
`run.sh` in an environment that mimics a fresh login shell:

```bash
env -i HOME="$HOME" PATH="/usr/local/bin:/usr/bin:/bin" TERM="${TERM:-xterm}" \
    bash -lc 'cd <output_dir> && ./run.sh [args]'
```

Run this clean-shell test for **every** documented invocation:

- `./run.sh --help`
- `./run.sh` (defaults)
- `./run.sh --sink fake`
- `./run.sh --sink display` (skip / mark as "requires display" only when no
  `$DISPLAY` is available — but still run it in headless mode to confirm the
  auto-fallback to `file` works)
- One negative case: `./run.sh --sink <invalid>` (must exit non-zero with a
  clear error).

### 3b. Pass criteria

For each clean-shell invocation:

- Exit status `0` (or `2` for the negative `--sink` case).
- The documented output artifact exists at the documented location (e.g.
  `lpr_output.mp4` for `--sink=file`).
- The pipeline reports a non-zero FPS via `gvafpscounter` (proves it actually
  processed frames, did not crash on first buffer).
- **Every detection / classification stage of the cascade reports a non-zero
  count over the full input clip.** Non-zero FPS and a non-zero aggregate
  end-of-stream count are necessary but **not sufficient** — in a cascade,
  the aggregate may be inflated by an earlier stage (e.g. the primary
  detector emitting an auxiliary class that shares the umbrella label with
  the final stage) while a downstream stage never ran. The wrapper MUST emit
  one per-stage counter (one log line per stage, prefixed with the stage
  name and showing the per-frame ROI / decoded-result count), and the
  validation MUST assert that **each** per-stage counter is non-zero on at
  least one frame (e.g. for a 3-stage detect → detect → classify cascade,
  `grep -c "<final-stage-tag>" run.log` ≥ 1).
- No `unbound variable`, `command not found`, `No such element`,
  `cannot open shared object file`, `Permission denied`, or unhandled `ERROR:`
  lines in stderr.

### 3c. Auto-fix loop

If any clean-shell invocation fails:

1. Read the actual error message from stderr (do **not** guess).
2. Map it to the correct fix from [`runsh-pitfalls.md`](./runsh-pitfalls.md).
3. Edit `run.sh` (or, if the bug is in the C++ code, the `.cpp` file) to
   apply the fix.
4. Re-run the clean-shell test from 3a.
5. Repeat until **all** invocations from 3a pass. Do not declare the
   conversion complete after a single fix without re-running every test —
   fixes for one bug frequently expose the next.

### 3d. Pitfall checklist

See [`runsh-pitfalls.md`](./runsh-pitfalls.md) for the full table of known
symptoms, root causes, and fixes. Wire these patterns into `run.sh` from the
start — do not wait for them to fail.

### 3e. Pre-flight resource audit (mandatory before first run)

Before running `run.sh` for the first time, verify that every resource the
script and binary depend on actually exists on disk. See
[`runsh-pitfalls.md`](./runsh-pitfalls.md) → *Pre-flight resource audit* for
the full list.

### 3f. Reporting

Record clean-shell test results in the README's `Observed Output` section per
[`documentation-spec.md`](./documentation-spec.md) §4 (one row per invocation
from 3a: command line, exit code, FPS snippet, pitfall triggered).
## 4. Compare against the baseline if available

When the original application's outputs were captured in step 1, compare them
to the outputs collected by the wrapper runs in step 3 and document
differences (account for acceptable numerical differences from FP32 → FP16/INT8
model conversion). If the original could not be run — see §1 — step 3's
correctness checks (meaningful output + non-zero FPS + non-zero per-stage
counters) are the primary validation signal.

## 5. Benchmark performance

Use the FPS numbers already captured by the step 3 wrapper runs (sink=file
and sink=fake at minimum). If the original was also run, record any
improvements or regressions side-by-side. Feed findings into the
documentation step.

## 6. Deprecated-API scan — MANDATORY

Before declaring the conversion complete, verify that no construct from the
per-run deprecation table (see
[`deprecation-discovery.md`](./deprecation-discovery.md)) appears in any file
generated under `<output_dir>/`.

Build the regex dynamically from that table (one alternative per discovered
symbol/element/file/API), then run:

```bash
grep -rnE "<dynamically-built-pattern>" <output_dir>/
```

Any non-empty result is a **conversion-blocking defect**. Replace the
offending construct with the documented replacement, re-run all build +
clean-shell `run.sh` tests from steps 2 and 3, and re-scan. Only after the
scan returns empty (or each remaining hit is documented under
`Conversion Notes → Deprecated API usage justification`) may the conversion
be reported as finished.

## 7. Final requirements compliance audit

See [`final-audit-checklist.md`](./final-audit-checklist.md) — the agent MUST
walk through that checklist as the last step before reporting completion.
