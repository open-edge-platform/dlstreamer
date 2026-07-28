# OpenVINO GPU: dynamic input dimensions carry recurring GPU-only overheads (not seen on CPU)

## Summary

Compiling a model with **dynamic input dimensions** introduces several **GPU-only
overheads** that are **absent on the CPU plugin**. Each is modest on its own
(~1.2–1.4x), but they stack, and they specifically affect the case we care about:
**a dynamic batch size** on GPU.

Measured on the GPU (CPU is the control and stays at ~1.0x for all of them):

| Overhead | GPU | CPU |
|---|---|---|
| One-time compile of a **new** shape (per distinct shape) | **~1.40x** of one inference | ~1.04x |
| Steady **dynamic-vs-static** at a *constant* shape | **~1.2x** | ~1.00x |
| Steady **batch-switching** among already-compiled shapes | **~1.24x** per inference | ~1.01x |
| Steady **spatial-switching** among already-compiled shapes | ~1.03–1.09x (small) | ~1.01x |

Two facts frame these correctly:

- **Shapes are cached — there is no thrashing.** Once a shape has been compiled it
  stays resident; we cycled **24 distinct** shapes indefinitely with **zero**
  recurring recompilations on both devices. Re-using any previously-seen shape does
  **not** recompile.
- **The recurring cost is a light per-switch transient, not recompilation.** Changing
  the active *batch* shape between inferences costs ~1.24x on GPU even though nothing
  is recompiled and nothing is evicted (see *Interpretation*). This cost has a sharp
  threshold: alternating between just **2** batch shapes is free (~1.02x), but rotating
  through **3 or more** distinct shapes costs ~1.24x on every switch (and does not grow
  further with more shapes).

## Environment

| Component | Version |
|---|---|
| OpenVINO | `2026.2.0-21903-52ddc073857-releases/2026/2` |
| CPU | 11th Gen Intel(R) Core(TM) i7-1165G7 @ 2.80GHz (8 threads) |
| GPU | Intel(R) Iris(R) Xe Graphics (iGPU), device id `0x9a49`, arch `v12.0.0` |
| GPU compute runtime (NEO) | `intel-opencl-icd` / `libze-intel-gpu1` `26.18.38308.4-1~24.04~ppa1` |
| GPU OpenCL driver | `OpenCL 3.0 NEO`, Driver Version `26.18.38308.4` |
| Level Zero loader | `libze1` `1.28.2-1~24.04~ppa1` |
| OS | Ubuntu 24.04.4 LTS |
| Kernel | `6.8.0-107-generic` |

## Reproducer

A self-contained Python script (`openvino_dynamic_shape_gpu_bug.py`) uses OpenVINO
**directly**. It builds a small CNN on the fly from OpenVINO ops, so **no model
files need to be downloaded**.

All timings are **artifact-free**: input tensors are **pre-allocated** (no array
allocation inside the timed region), and shapes are **pre-warmed** before
steady-state timing so that compilation is excluded — except in M1, which measures
the compile cost on purpose.

### Requirements

```bash
pip install openvino numpy
```

### Run

```bash
python3 openvino_dynamic_shape_gpu_bug.py
```

The script performs the following measurements on CPU and GPU (M1–M3 are the
overheads; M4–M6 are diagnostics):

| # | Measurement | What it isolates |
|---|---|---|
| M1 | First-ever inference at a **new** batch vs its steady-state | one-time compile of a new shape |
| M2 | Dynamic-batch model vs a **static** model at the *same constant* batch | constant dynamic-kernel overhead |
| M3a | Per-shape ms steady vs cycled, dynamic **batch** `1..8` (8 shapes) | recurring batch-switching cost |
| M3b | Per-shape ms steady vs cycled, dynamic **spatial** sizes | recurring spatial-switching cost |
| M4 | Cycle **24** distinct shapes forever, count recurring recompiles | eviction / thrashing (expected: none) |
| M5 | Batch switching with only **2** shapes (4 <-> 8) | rotation-breadth: 2-shape rotation |
| M6 | Batch switching with **3** shapes (2, 4, 8) | rotation-breadth: 3-shape rotation |

It also prints an **absolute ms/img and img/s** comparison at batch 4 (static model
vs dynamic-steady vs dynamic 8-shape rotation), so the ratios above map to real
throughput.

## Observed results

```
##################################################################
# Device: GPU
##################################################################
  M1  one-time compile of a NEW shape            : 1.44x   [first-ever infer vs steady, same shape]
  M2  dynamic-vs-static at a CONSTANT shape       : 1.21x   [dynamic model vs STATIC model, batch 4]
  M3a batch switching, 8-shape rotation           : 1.23x   [avg over 8 batches, vs each batch's OWN steady]
  M3b spatial switching, 8-size rotation          : 1.06x   [avg over 8 sizes, vs each size's OWN steady]
  M5  batch switching, 2-shape rotation (4<->8)   : 1.02x   [free below the ~2-shape 'hot set']
  M6  batch switching, 3-shape rotation (2,4,8)   : 1.25x   [the jump happens at 3 shapes]
  M4  recompiles while cycling 24 shapes          : 0/144 slow   [0 = no eviction]
  ==> combined dynamic + 8-shape switching        : 1.49x   [M2 x M3a, vs static fixed shape - the costs STACK]
  --- absolute performance at batch 4 (lower ms/img = faster) ---
  static model (fixed shape)      :   5.92 ms/img   (  169 img/s)
  dynamic model, steady           :   7.17 ms/img   (  139 img/s)
  dynamic model, 8-shape rotation :   8.82 ms/img   (  113 img/s)

##################################################################
# Device: CPU
##################################################################
  M1  one-time compile of a NEW shape            : 1.04x   [first-ever infer vs steady, same shape]
  M2  dynamic-vs-static at a CONSTANT shape       : 1.00x   [dynamic model vs STATIC model, batch 4]
  M3a batch switching, 8-shape rotation           : 0.99x   [avg over 8 batches, vs each batch's OWN steady]
  M3b spatial switching, 8-size rotation          : 1.01x   [avg over 8 sizes, vs each size's OWN steady]
  M5  batch switching, 2-shape rotation (4<->8)   : 1.00x   [free below the ~2-shape 'hot set']
  M6  batch switching, 3-shape rotation (2,4,8)   : 1.01x   [the jump happens at 3 shapes]
  M4  recompiles while cycling 24 shapes          : 0/144 slow   [0 = no eviction]
  ==> combined dynamic + 8-shape switching        : 1.00x   [M2 x M3a, vs static fixed shape - the costs STACK]
  --- absolute performance at batch 4 (lower ms/img = faster) ---
  static model (fixed shape)      :  18.10 ms/img   (   55 img/s)
  dynamic model, steady           :  18.18 ms/img   (   55 img/s)
  dynamic model, 8-shape rotation :  18.26 ms/img   (   55 img/s)
```

In absolute throughput this means the GPU drops from **169 img/s** (static) to
**113 img/s** (dynamic model, 8-shape rotation) at batch 4 — a **~33% throughput
loss** — while the CPU stays flat at **~55 img/s** regardless of dynamic vs static or
any shape switching.

(The M2–M3 ratios wobble run-to-run by ~±0.05x (e.g. M2 has been seen at 1.17–1.24x
across runs). M4 is essentially always 0/144 (a rare single jitter outlier at most,
never systematic). The one-time M1 compile is more variable — we observed ~1.1–1.5x
depending on the model size and the previously-used shape (e.g. compiling batch 6
right after batch 8 is cheaper than after batch 2) — but it is always a one-time cost
per distinct shape, never recurring.)

## Interpretation

- **CPU is flat.** Every dynamic effect is ~1.0x on CPU — the CPU plugin handles new
  shapes, dynamic kernels and shape switching at negligible cost.
- **GPU pays three main GPU-only overheads** (M1 compile, M2 dynamic-kernel, M3a
  batch-switching), all ~1.2–1.4x and all absent on CPU; spatial switching (M3b) is
  much smaller (~1.08x). M4–M6 are diagnostics (no eviction; the rotation-breadth
  threshold).
- **The recurring switching cost (M3a) is a transient, not recompilation.** Three
  independent observations support this:
  1. **Magnitude.** A first-ever compile of a new shape (M1) costs ~1.4x of one
     inference; the recurring per-switch cost is only ~1.24x — a different, smaller
     order of magnitude than a full compile.
  2. **Persistence.** After using other shapes and returning to an earlier one, only
     the **first** call is slightly slow; the next calls are back to full speed. If
     the shape had been evicted/recompiled it would have paid the full compile cost
     and been rebuilt; instead the kernel was still cached.
  3. **No thrashing (M4).** Cycling 24 distinct shapes indefinitely shows **zero**
     recurring recompiles — nothing is evicted.
- **Spatial switching is small (M3b, ~1.03–1.09x); batch switching is much larger
  (~1.24x).** Changing the batch dimension between inferences carries a real
  recurring GPU cost that changing the spatial size largely does not.
- **The overheads have different baselines and they stack.** M2 is measured against a
  *static* model; M3a/M3b are measured against the *same dynamic model's own
  steady-state* (so the M2 overhead is present in both sides of the M3 ratio and
  cancels — M3 is the switching cost alone, not "M2 subtracted"). Because they
  multiply, a dynamic model rotating through 3+ batch sizes versus a static model at a
  fixed batch costs about **1.5x** overall (M2 × M3a; the reproducer prints this as
  the `combined` line — **1.49x** in the run above). Note M3a's ~1.24x is the
  **average** switch cost over the 8 batch sizes; a single batch (e.g. batch 4) is
  ~1.2x, so `combined` for one batch is ~1.2 × 1.2 ≈ 1.49x.
- **The batch-switching cost depends on how many distinct shapes are in active
  rotation, as a step, not a slope.** Beyond the large compiled-kernel cache, the GPU
  appears to keep only ~2 shapes "hot" (immediately runnable). Measured penalty by
  rotation breadth (reproducer M5/M6/M3a): 2 shapes = 1.02x (free), 3 = 1.25x,
  8 = 1.23x. So alternating two batch sizes is free, but as soon as a third distinct
  size enters the rotation every switch pays ~1.24x (and it does not grow further).
  This is why returning `6 -> 8` after only using batch 6 (a two-shape rotation) is
  free, while cycling `1..8` is not.

## Why this matters to us

Our main use case is a pipeline that ingests several video streams from different
cameras (e.g. 8 streams) and batches them together for inference. With a fixed
`batch-size` of 8 this works perfectly as long as all 8 cameras are delivering
frames. The problem appears when we **lose some cameras**: we can no longer fill the
batch before the batch timeout elapses, and we are forced to either wait/pad or drop
frames.

**Dynamic batch size would be the natural solution** — when the batch cannot be
filled in time, run inference on whatever frames are available. The good news from
these measurements is that this is **viable on GPU**: any set of batch sizes gets
compiled once and then stays cached (no thrashing, no matter how many distinct sizes
occur), and a one-time change to a new steady batch recovers immediately.

The cost we pay on GPU is: a **one-time ~1.4x** hit the first time each batch size
appears, a constant **~1.2x** dynamic-vs-static overhead, and — if **3 or more**
batch sizes are in active rotation — an additional **~1.24x** switching cost on every
inference (alternating just two sizes is free). None of these exist on CPU, where
dynamic dimensions are a near-zero-cost convenience. We would like the GPU to be
closer to that.

## Expected vs actual

- **Expected:** enabling dynamic dimensions has a small, bounded cost on GPU,
  comparable to CPU, once each shape has been compiled.
- **Actual:** on GPU, dynamic dimensions add a constant ~1.2x kernel overhead, a
  recurring ~1.24x cost whenever the batch size changes (for 3+ shapes in rotation),
  and a ~1.4x one-time compile per new shape — while the CPU shows none of these.

## Ask

- Confirm the source of the **constant ~1.2x** overhead of a dynamic-shape model at
  a *fixed* shape on GPU (shape-agnostic kernels?), and whether it can be reduced.
- Confirm the **recurring ~1.24x cost of switching the batch dimension** between
  inferences on GPU (kernels stay cached — this is not recompilation), why spatial
  switching does not show it, and whether it can be avoided.
- Confirm the **one-time ~1.4x compile** on the first inference at each new shape and
  whether it can be pre-warmed or cached to disk.

## Tuning knobs in the reproducer

Constants at the top of `openvino_dynamic_shape_gpu_bug.py`:

- `N_CONV`, `CHANNELS` — model size (larger kernels → higher one-time compile cost)
- `BATCHES`, `SPATIAL` — the shape sets used for the switching measurements
- `MANY` — how many distinct shapes are cycled in the no-eviction test (M4)

## Companion scripts

- `openvino_dynamic_shape_gpu_bug.py` — the main reproducer (M1–M6 above).
- `openvino_dynamic_batch_switch_recovery.py` — shows that a one-time `8 -> 6` batch
  change pays a single one-time cost on the first batch-6 inference and then recovers
  to full speed; returning to `8` is free because it is a **two-shape** rotation
  (8 and 6 stay "hot"). Note: rotating through 3+ shapes is not free — see M3a.
- `openvino_dynamic_vs_static_fixed_shape.py` — measures the constant dynamic-vs-static
  overhead at a constant shape across batch sizes (~1.2x on GPU, ~1.0x on CPU).
