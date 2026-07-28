#!/usr/bin/env python3
"""
Follow-up test: does a ONE-TIME batch-size change recover on GPU?

Companion to `openvino_dynamic_shape_gpu_bug.py`. That script showed that dynamic
shapes carry several modest GPU-only overheads. This script answers a different,
practical question:

    If the dynamic batch size changes ONCE (e.g. 8 -> 6, because we lost some
    cameras) and then STAYS at the new value, does performance recover to normal
    after that single change?

If the GPU cost is a one-time per-shape (re)compilation that is then cached, the
steady-state at batch=6 should be just as fast (per image) as batch=8, and the
penalty is paid only on the very first inference after the switch. If instead the
slowdown is persistent, batch=6 steady-state stays slow.

The script (on GPU, with CPU printed as reference):
  1. compiles ONE dynamic-batch model  ([?, 3, 224, 224]),
  2. measures steady-state at batch=8,
  3. switches to batch=6 and isolates the FIRST call (the switch) from the
     following steady-state calls,
  4. compares batch=6 steady-state against batch=8 steady-state and against a
     model compiled statically for batch=6 (the ideal).

Metric: milliseconds PER IMAGE (latency / batch), so batch=8 and batch=6 compare
directly.

Run:
    python3 openvino_dynamic_batch_switch_recovery.py

Requirements:
    pip install openvino numpy
"""

import statistics
import time

import numpy as np
import openvino as ov

# ---------------------------------------------------------------------------
# Parameters
# ---------------------------------------------------------------------------
BASE = 224          # spatial size (kept constant here; only batch changes)
N_CONV = 8          # convolution layers
CHANNELS = 32       # hidden channels
BATCH_HIGH = 8      # initial batch size (all cameras present)
BATCH_LOW = 6       # batch size after losing some cameras
WARMUP = 10         # warm-up iterations at BATCH_HIGH (not measured)
STEADY = 60         # measured steady-state iterations per phase
TRACE = 8           # per-iteration trace length printed right after the switch


def _opset():
    """Return an available OpenVINO opset module (name varies across versions)."""
    from importlib import import_module

    for name in ("opset13", "opset12", "opset11", "opset10", "opset9", "opset8"):
        if hasattr(ov, name):
            return getattr(ov, name)
        for module_path in (f"openvino.{name}", f"openvino.runtime.{name}"):
            try:
                return import_module(module_path)
            except ImportError:
                continue
    raise RuntimeError("No OpenVINO opset module found")


ops = _opset()


def build(dynamic_batch: bool, batch: int = 1) -> ov.Model:
    """Build the CNN with either a dynamic or a fixed batch dimension."""
    b = -1 if dynamic_batch else batch
    param = ops.parameter(ov.PartialShape([b, 3, BASE, BASE]),
                          dtype=ov.Type.f32, name="input")
    rng = np.random.default_rng(0)

    x = param
    c_in = 3
    for _ in range(N_CONV):
        weights = ops.constant(
            (rng.standard_normal((CHANNELS, c_in, 3, 3)) * 0.05).astype(np.float32)
        )
        x = ops.convolution(
            x, weights,
            strides=[1, 1], pads_begin=[1, 1], pads_end=[1, 1], dilations=[1, 1],
        )
        x = ops.relu(x)
        c_in = CHANNELS

    return ov.Model([ops.result(x)], [param],
                    f"cnn_b{'dyn' if dynamic_batch else batch}")


def infer_times_ms(ireq, batch: int, n: int) -> list:
    """Run n inferences at the given batch, return per-call latency in ms."""
    inp = np.random.rand(batch, 3, BASE, BASE).astype(np.float32)
    out = []
    for _ in range(n):
        start = time.perf_counter()
        ireq.infer([inp])
        out.append((time.perf_counter() - start) * 1000.0)
    return out


def run_device(core: ov.Core, device: str) -> None:
    print(f"\n{'#' * 70}\n# Device: {device}\n{'#' * 70}")

    # --- one dynamic-batch model, compiled once ---
    compiled = core.compile_model(build(dynamic_batch=True), device)
    ireq = compiled.create_infer_request()

    # 1) warm-up + steady-state at BATCH_HIGH
    infer_times_ms(ireq, BATCH_HIGH, WARMUP)
    hi = infer_times_ms(ireq, BATCH_HIGH, STEADY)
    hi_per_img = statistics.median(hi) / BATCH_HIGH

    # 2) the ONE-TIME switch to BATCH_LOW: first call vs the rest
    lo = infer_times_ms(ireq, BATCH_LOW, STEADY)
    switch_first_ms = lo[0]                         # includes any recompilation
    lo_steady = statistics.median(lo[1:]) / BATCH_LOW
    switch_first_per_img = switch_first_ms / BATCH_LOW

    # 2b) switch BACK to BATCH_HIGH: is the old shape still cached, or recompiled?
    ret = infer_times_ms(ireq, BATCH_HIGH, STEADY)
    return_first_ms = ret[0]                        # first-call transient after switching away
    ret_steady = statistics.median(ret[1:]) / BATCH_HIGH
    return_first_per_img = return_first_ms / BATCH_HIGH

    # 3) ideal reference: a model compiled statically for BATCH_LOW
    compiled_static = core.compile_model(build(dynamic_batch=False, batch=BATCH_LOW),
                                        device)
    ireq_static = compiled_static.create_infer_request()
    infer_times_ms(ireq_static, BATCH_LOW, WARMUP)
    st = infer_times_ms(ireq_static, BATCH_LOW, STEADY)
    static_per_img = statistics.median(st) / BATCH_LOW

    # --- report ---
    print(f"  batch={BATCH_HIGH} steady-state          : "
          f"{hi_per_img:7.3f} ms/img")
    print(f"  --- switch {BATCH_HIGH} -> {BATCH_LOW} ---")
    print(f"  first batch={BATCH_LOW} call (the switch) : "
          f"{switch_first_ms:7.3f} ms total  "
          f"({switch_first_per_img:7.3f} ms/img)")
    print(f"  batch={BATCH_LOW} steady-state (after)    : "
          f"{lo_steady:7.3f} ms/img")
    print(f"  --- switch BACK {BATCH_LOW} -> {BATCH_HIGH} ---")
    print(f"  first batch={BATCH_HIGH} call (return)    : "
          f"{return_first_ms:7.3f} ms total  "
          f"({return_first_per_img:7.3f} ms/img)")
    print(f"  batch={BATCH_HIGH} steady-state (return)  : "
          f"{ret_steady:7.3f} ms/img")
    print(f"  batch={BATCH_LOW} STATIC model (ideal)    : "
          f"{static_per_img:7.3f} ms/img")

    print(f"  trace of first {TRACE} batch={BATCH_LOW} calls (ms/img):")
    trace = "   ".join(f"{lo[i] / BATCH_LOW:6.3f}" for i in range(min(TRACE, len(lo))))
    print(f"     {trace}")
    print(f"  trace of first {TRACE} batch={BATCH_HIGH} return calls (ms/img):")
    trace_r = "   ".join(f"{ret[i] / BATCH_HIGH:6.3f}"
                         for i in range(min(TRACE, len(ret))))
    print(f"     {trace_r}")

    # --- verdict ---
    recovery = lo_steady / hi_per_img
    spike = switch_first_per_img / hi_per_img
    return_spike = return_first_per_img / hi_per_img
    print("  ---")
    print(f"  one-time switch spike ({BATCH_HIGH}->{BATCH_LOW}) : "
          f"{spike:.2f}x the steady per-image cost")
    if recovery <= 1.15:
        print(f"  >>> RECOVERS: batch={BATCH_LOW} steady-state is within "
              f"{(recovery - 1) * 100:+.0f}% of batch={BATCH_HIGH} — the switch cost "
              f"is a ONE-TIME compile/prepare of the new shape, then back to normal.")
    else:
        print(f"  >>> PERSISTENT: batch={BATCH_LOW} steady-state stays "
              f"{(recovery - 1) * 100:+.0f}% slower than batch={BATCH_HIGH} — the "
              f"slowdown does NOT recover after a single change.")
    print(f"  return spike ({BATCH_LOW}->{BATCH_HIGH})       : "
          f"{return_spike:.2f}x the steady per-image cost")
    if return_spike <= 1.30:
        print(f"  >>> batch={BATCH_HIGH} is still CACHED: returning to it costs "
              f"almost nothing (no recompilation).")
    else:
        print(f"  >>> returning to batch={BATCH_HIGH} shows a first-call transient "
              f"({return_spike:.2f}x on one call only); the shape stays cached "
              f"(this is the per-switch cost, not a recompilation).")
    vs_static = lo_steady / static_per_img
    print(f"  dynamic batch={BATCH_LOW} steady-state vs STATIC batch={BATCH_LOW}: "
          f"{vs_static:.2f}x")


def main() -> None:
    core = ov.Core()
    devices = core.available_devices

    print("=" * 70)
    print(f"OpenVINO {ov.__version__}")
    print(f"Available devices: {devices}")
    print(f"Model: {N_CONV} x conv3x3({CHANNELS} ch) + ReLU   (metric: ms per image)")
    print(f"Scenario: dynamic batch, one-time change {BATCH_HIGH} -> {BATCH_LOW}")
    print("=" * 70)

    if "GPU" not in devices:
        print("\nNOTE: no GPU device found - this test targets GPU.")
        return

    run_device(core, "GPU")
    if "CPU" in devices:
        run_device(core, "CPU")  # reference


if __name__ == "__main__":
    main()
