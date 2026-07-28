#!/usr/bin/env python3
"""
Detail test: dynamic-shape vs static-shape model at a CONSTANT input shape.

This isolates a phenomenon that is separate from per-shape recompilation:
even when the input shape NEVER changes at inference time, a model compiled with a
DYNAMIC dimension runs slightly slower on GPU than the same model compiled
statically for that exact shape. On CPU the two are essentially identical.

The likely cause: with a dynamic dimension the GPU plugin uses shape-agnostic
kernels, which are less specialised (and thus less optimal) than the kernels a
fully static shape allows. This overhead is CONSTANT (it does not grow over time),
but it is GPU-specific.

Method: for each batch size B, build
  * a STATIC model with input [B, 3, 224, 224], and
  * a DYNAMIC-batch model with input [?, 3, 224, 224],
then feed the SAME constant batch B repeatedly (no shape changes at all) and
measure ms per image on CPU and GPU. Report the dynamic/static ratio.

Run:
    python3 openvino_dynamic_vs_static_fixed_shape.py

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
BASE = 224            # spatial size (constant everywhere)
N_CONV = 8            # convolution layers
CHANNELS = 32         # hidden channels
BATCHES = [1, 2, 4, 8]  # batch sizes to probe (each kept constant)
WARMUP = 20           # warm-up iterations (not measured)
ITERS = 100           # measured iterations


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


def bench(core, model, device, batch) -> float:
    """ms PER IMAGE for a constantly repeated (batch, BASE, BASE) input."""
    ireq = core.compile_model(model, device).create_infer_request()
    inp = np.random.rand(batch, 3, BASE, BASE).astype(np.float32)
    for _ in range(WARMUP):
        ireq.infer([inp])
    samples = [ ]
    for _ in range(ITERS):
        start = time.perf_counter()
        ireq.infer([inp])
        samples.append((time.perf_counter() - start) * 1000.0)
    return statistics.median(samples) / batch


def run_device(core, device) -> None:
    print(f"\n{'#' * 62}\n# Device: {device}   (constant shape, no changes)\n{'#' * 62}")
    print(f"{'batch':>6}{'static ms/img':>16}{'dynamic ms/img':>17}{'dyn/static':>13}")
    print("-" * 62)
    ratios = []
    for b in BATCHES:
        static_ms = bench(core, build(dynamic_batch=False, batch=b), device, b)
        dynamic_ms = bench(core, build(dynamic_batch=True), device, b)
        ratio = dynamic_ms / static_ms
        ratios.append(ratio)
        print(f"{b:>6}{static_ms:>15.3f} {dynamic_ms:>16.3f} {ratio:>11.2f}x")
    print("-" * 62)
    avg = statistics.mean(ratios)
    if avg <= 1.03:
        print(f"  Dynamic == static on {device} (avg {avg:.2f}x): no measurable "
              f"steady-state overhead.")
    else:
        print(f"  Dynamic is on average {avg:.2f}x slower than static on {device} "
              f"— a CONSTANT, shape-independent overhead of the dynamic kernels.")


def main() -> None:
    core = ov.Core()
    devices = core.available_devices
    print("=" * 62)
    print(f"OpenVINO {ov.__version__}")
    print(f"Available devices: {devices}")
    print(f"Model: {N_CONV} x conv3x3({CHANNELS} ch) + ReLU, spatial {BASE}x{BASE}")
    print("Question: dynamic vs static at a CONSTANT shape (no shape changes)")
    print("=" * 62)

    for device in ("GPU", "CPU"):
        if device in devices:
            run_device(core, device)


if __name__ == "__main__":
    main()
