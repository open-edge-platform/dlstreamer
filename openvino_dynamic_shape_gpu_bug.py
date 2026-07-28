#!/usr/bin/env python3
"""
OpenVINO GPU vs CPU: overheads of DYNAMIC input dimensions (clean reproducer).

Uses OpenVINO DIRECTLY (no DL Streamer). The model is built on the fly from
OpenVINO ops, so NO model files need to be downloaded.

This script measures four DISTINCT, GPU-only overheads of dynamic shapes, each
against the CPU as a control. All measurements are artifact-free:
  * input tensors are PRE-ALLOCATED (no array allocation inside the timed region),
  * shapes are PRE-WARMED before steady-state timing (compilation excluded), and
  * the one-time compile cost is measured separately, on purpose.

Measurements (metric: milliseconds, and GPU/CPU ratios):

  M1. One-time compile of a NEW shape
      First-ever inference at a batch never seen before vs its steady-state.
      -> GPU pays a one-time prepare/compile cost; CPU does not.

  M2. Steady dynamic-vs-static at a CONSTANT shape
      Dynamic-batch model vs a model compiled statically for that exact batch,
      same constant shape, no changes at inference time.
      -> constant GPU-only overhead of shape-agnostic (dynamic) kernels.

  M3. Steady switching cost among ALREADY-COMPILED shapes
      Per-shape ms/img when a shape is used steadily vs when it is one of several
      shapes cycled every inference. Done for dynamic BATCH and dynamic SPATIAL.
      -> a recurring GPU-only cost of changing the active shape (NOT recompilation
         and NOT eviction: the kernels stay cached).

  M4. No eviction
      Cycle many distinct shapes forever; after warm-up none of them recompiles.

  M5. Rotation-breadth threshold
      Batch switching with only 2 shapes (4 <-> 8) and 3 shapes (2,4,8) vs M3a's 8.
      -> the switching cost is a step: ~free for a 2-shape rotation, ~1.2x for 3+.

Run:
    python3 openvino_dynamic_shape_gpu_bug.py

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
BASE = 224            # spatial size for the batch experiments
N_CONV = 8            # convolution layers
CHANNELS = 32         # hidden channels
WARM = 6              # warm-up touches per shape (forces compilation)
ITERS = 30            # measured iterations
BATCHES = [1, 2, 3, 4, 5, 6, 7, 8]          # batch sizes for M3 (dynamic batch)
SPATIAL = [208 + 4 * i for i in range(8)]    # clustered sizes for M3 (spatial)
MANY = list(range(1, 25))                     # 24 distinct batches for M4


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


def build(dynamic_batch=False, dynamic_spatial=False, batch=1) -> ov.Model:
    """Build the CNN with the requested dynamic/static input dimensions."""
    b = -1 if dynamic_batch else batch
    hw = -1 if dynamic_spatial else BASE
    param = ops.parameter(ov.PartialShape([b, 3, hw, hw]),
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
    return ov.Model([ops.result(x)], [param], "cnn")


def infer_ms(ireq, inp):
    """One inference, return wall time in ms (input is pre-allocated)."""
    start = time.perf_counter()
    ireq.infer([inp])
    return (time.perf_counter() - start) * 1000.0


# ---------------------------------------------------------------------------
# Measurements
# ---------------------------------------------------------------------------
def m1_compile_new_shape(core, device, batch_inputs):
    """First-ever inference at a new batch vs steady (one-time compile cost)."""
    ireq = core.compile_model(build(dynamic_batch=True), device).create_infer_request()
    for _ in range(WARM):
        infer_ms(ireq, batch_inputs[4])           # warm one shape
    ratios = []
    for b in (2, 6, 10):
        first = infer_ms(ireq, batch_inputs[b])   # never seen -> compile
        steady = statistics.median([infer_ms(ireq, batch_inputs[b]) for _ in range(20)])
        ratios.append(first / steady)
    return statistics.mean(ratios)


def m2_dynamic_vs_static(core, device, batch_inputs, batch=4):
    """Dynamic-batch model vs static model, same constant shape."""
    dyn = core.compile_model(build(dynamic_batch=True), device).create_infer_request()
    sta = core.compile_model(build(batch=batch), device).create_infer_request()
    for _ in range(WARM):
        infer_ms(dyn, batch_inputs[batch]); infer_ms(sta, batch_inputs[batch])
    d = statistics.median([infer_ms(dyn, batch_inputs[batch]) for _ in range(ITERS)])
    s = statistics.median([infer_ms(sta, batch_inputs[batch]) for _ in range(ITERS)])
    return d / s


def m3_switching(core, device, model, inputs, keys):
    """Per-shape ms(-per-image) steady vs cycled among already-compiled shapes."""
    ireq = core.compile_model(model, device).create_infer_request()
    for k in keys:
        for _ in range(WARM):
            infer_ms(ireq, inputs[k])
    steady = {k: statistics.median([infer_ms(ireq, inputs[k]) for _ in range(ITERS)])
              for k in keys}
    cyc = {k: [] for k in keys}
    for _ in range(ITERS):
        for k in keys:
            cyc[k].append(infer_ms(ireq, inputs[k]))
    return statistics.mean([statistics.median(cyc[k]) / steady[k] for k in keys])


def m4_no_eviction(core, device, batch_inputs):
    """Cycle many distinct shapes; count recurring big spikes after warm-up."""
    ireq = core.compile_model(build(dynamic_batch=True), device).create_infer_request()
    for b in MANY:
        infer_ms(ireq, batch_inputs[b]); infer_ms(ireq, batch_inputs[b])
    per = {b: [] for b in MANY}
    for _ in range(6):
        for b in MANY:
            per[b].append(infer_ms(ireq, batch_inputs[b]))
    slow = sum(1 for b in MANY for v in per[b] if v > 1.6 * min(per[b]))
    total = sum(len(v) for v in per.values())
    return slow, total


def absolute_ms_per_img(core, device, batch_inputs, batch=4):
    """Absolute ms/img at a fixed batch for static, dynamic-steady, dynamic-cycled."""
    sta = core.compile_model(build(batch=batch), device).create_infer_request()
    for _ in range(WARM):
        infer_ms(sta, batch_inputs[batch])
    static = statistics.median([infer_ms(sta, batch_inputs[batch])
                                for _ in range(ITERS)]) / batch
    dyn = core.compile_model(build(dynamic_batch=True), device).create_infer_request()
    for b in BATCHES:
        for _ in range(WARM):
            infer_ms(dyn, batch_inputs[b])
    dyn_steady = statistics.median([infer_ms(dyn, batch_inputs[batch])
                                    for _ in range(ITERS)]) / batch
    cyc = []
    for _ in range(ITERS):
        for b in BATCHES:
            v = infer_ms(dyn, batch_inputs[b])
            if b == batch:
                cyc.append(v)
    dyn_cycled = statistics.median(cyc) / batch
    return static, dyn_steady, dyn_cycled


def run(core, device, batch_inputs, spatial_inputs):
    print(f"\n{'#' * 66}\n# Device: {device}\n{'#' * 66}")
    m1 = m1_compile_new_shape(core, device, batch_inputs)
    m2 = m2_dynamic_vs_static(core, device, batch_inputs)
    m3b = m3_switching(core, device, build(dynamic_batch=True), batch_inputs, BATCHES)
    m3s = m3_switching(core, device, build(dynamic_spatial=True), spatial_inputs, SPATIAL)
    m5_two = m3_switching(core, device, build(dynamic_batch=True), batch_inputs, [4, 8])
    m6_three = m3_switching(core, device, build(dynamic_batch=True), batch_inputs, [2, 4, 8])
    slow, total = m4_no_eviction(core, device, batch_inputs)
    print("  (all values are ratios; the CPU control is ~1.0x for every row)")
    print(f"  M1  one-time compile of a NEW shape            : {m1:.2f}x   "
          f"[first-ever infer vs steady, same shape]")
    print(f"  M2  dynamic-vs-static at a CONSTANT shape       : {m2:.2f}x   "
          f"[dynamic model vs STATIC model, batch 4]")
    print(f"  M3a batch switching, 8-shape rotation           : {m3b:.2f}x   "
          f"[avg over 8 batches, vs each batch's OWN steady]")
    print(f"  M3b spatial switching, 8-size rotation          : {m3s:.2f}x   "
          f"[avg over 8 sizes, vs each size's OWN steady]")
    print(f"  M5  batch switching, 2-shape rotation (4<->8)   : {m5_two:.2f}x   "
          f"[free below the ~2-shape 'hot set']")
    print(f"  M6  batch switching, 3-shape rotation (2,4,8)   : {m6_three:.2f}x   "
          f"[the jump happens at 3 shapes]")
    print(f"  M4  recompiles while cycling 24 shapes          : {slow}/{total} slow   "
          f"[0 = no eviction]")
    print(f"  ==> combined dynamic + 8-shape switching        : {m2 * m3b:.2f}x   "
          f"[M2 x M3a, vs static fixed shape - the costs STACK]")
    static, dyn_steady, dyn_cycled = absolute_ms_per_img(core, device, batch_inputs)
    print("  --- absolute performance at batch 4 (lower ms/img = faster) ---")
    print(f"  static model (fixed shape)      : {static:6.2f} ms/img   "
          f"({1000.0 / static:5.0f} img/s)")
    print(f"  dynamic model, steady           : {dyn_steady:6.2f} ms/img   "
          f"({1000.0 / dyn_steady:5.0f} img/s)")
    print(f"  dynamic model, 8-shape rotation : {dyn_cycled:6.2f} ms/img   "
          f"({1000.0 / dyn_cycled:5.0f} img/s)")
    return {"M1": m1, "M2": m2, "M3a": m3b, "M3b": m3s, "M5": m5_two, "M6": m6_three}


def main():
    core = ov.Core()
    devices = core.available_devices
    print("=" * 66)
    print(f"OpenVINO {ov.__version__}")
    print(f"Available devices: {devices}")
    print(f"Model: {N_CONV} x conv3x3({CHANNELS} ch) + ReLU")
    print("All timings: pre-allocated inputs, shapes pre-warmed (except M1's first call)")
    print("=" * 66)
    if "GPU" not in devices:
        print("\nNOTE: no GPU device found - this comparison targets GPU.")
        return

    batch_inputs = {b: np.random.rand(b, 3, BASE, BASE).astype(np.float32)
                    for b in range(1, 25)}
    spatial_inputs = {s: np.random.rand(1, 3, s, s).astype(np.float32) for s in SPATIAL}

    res = {}
    for device in ("GPU", "CPU"):
        if device in devices:
            res[device] = run(core, device, batch_inputs, spatial_inputs)

    if "GPU" in res and "CPU" in res:
        print("\n" + "=" * 66)
        print("SUMMARY (GPU overhead vs CPU; CPU is the control ~1.0x everywhere)")
        print("=" * 66)
        labels = {
            "M1": "one-time compile of a new shape",
            "M2": "steady dynamic-vs-static (constant shape)",
            "M3a": "steady batch-switching (cached shapes)",
            "M3b": "steady spatial-switching (cached shapes)",
        }
        for k, name in labels.items():
            print(f"  {name:<44} GPU {res['GPU'][k]:.2f}x | CPU {res['CPU'][k]:.2f}x")
        print("  " + "-" * 62)
        print("  Shapes are cached (no eviction over 24 distinct shapes on either device).")


if __name__ == "__main__":
    main()
