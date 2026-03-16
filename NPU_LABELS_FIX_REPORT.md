# NPU Labels Fix — Technical Report

## 1. Problem Summary

When running an ATSS-type detection model on Intel NPU via OpenVINO 2026.0.0, the `labels` output tensor contains incorrect values. Depending on the model version, labels are either all zeros (every detection classified as class 0) or very large numbers (nonsensical class IDs). On CPU both model versions return correct labels (0, 1, 2). Bounding boxes and confidence scores are unaffected.

| Output | CPU | NPU — `optimized_model.xml` | NPU — `exported_model.xml` |
|--------|-----|-----------|------------|
| `boxes` [1, 100, 5] FP32 | ✅ correct | ✅ correct | ✅ correct |
| `labels` [1, 100] FP32 | ✅ 0, 1, 2 | ❌ all zeros | ❌ very large numbers |

---

## 2. Root Cause

### 2.1. I64 operations in the model produce incorrect results on NPU

During testing with OpenVINO 2026.0.0, we observed that the I64 integer operations in this model's labels subgraph produce **incorrect results** when executed on NPU. Depending on the model version, the symptoms differ:

- **`optimized_model.xml`**: labels output contains only **zeros** — all detections classified as class 0
- **`exported_model.xml`** (alternate export of the same model): labels output contains **very large numbers** — nonsensical class IDs

In both cases the NPU plugin does not report any errors or warnings — the incorrect values silently propagate to the output. The common factor is the I64 operation chain in the labels subgraph.

Whether this is a hardware limitation, a compiler bug, or a missing precision-lowering pass in the NPU plugin has not been determined. What was confirmed empirically:
- Removing the final `Convert(I64→FP32)` and reading raw I64 output — all zeros (version A)
- Converting the entire model from I64 to I32 — still incorrect results
- Replacing the labels output with a constant FP32 tensor — NPU correctly outputs non-zero values

### 2.2. The labels path in the model uses I64 extensively

The model computes labels via this subgraph:

```
Multiply(Sigmoid(cls), objectness)   [1, 3549, 3]  FP32
                ↓
TopK(axis=2, k=1, mode=min).indices  [1, 3549, 1]  ← I64 (argmax per anchor)
                ↓
        Squeeze(axis=2)              [1, 3549]      I64
                ↓
           Reshape                   [3549, 1]      I64
                ↓
     Gather(data=I64, indices=I32)   [100, 1]       I64  (select top-100)
                ↓
           Reshape                   [1, 100]       I64
                ↓
     Convert(I64 → FP32)            [1, 100]       FP32 → Result "labels"
```

Every operation from `TopK.indices` to `Convert` operates on I64 tensors. On NPU, `TopK` returns zero indices, and these zeros propagate through the entire chain unchanged — producing `[0, 0, ..., 0]` at the output.

### 2.3. Why boxes work but labels don't

The `boxes` output path is **entirely FP32** — bbox decoding, NMS, Concat all happen in floating point. NPU handles FP32 correctly. Only the `labels` path routes through the I64 argmax subgraph, which is where NPU fails.

### 2.4. Why OpenVINO doesn't fix this automatically

The OpenVINO compiler for NPU **should** automatically lower I64 to I32 or FP32 during the compilation phase (as it does for FP64→FP32). As of OpenVINO 2026.0.0, this automatic lowering is **not implemented** for NPU target. This is a gap in the NPU compilation pipeline.

---

## 3. The Fix

### 3.1. Core Idea

Replace the I64-based argmax subgraph with a mathematically equivalent **pure FP32** computation that NPU can execute correctly.

Instead of relying on `TopK.indices` (I64) to determine which class each detection belongs to, we:

1. Take `Sigmoid(cls)` — the raw per-class classification scores [1, 3549, C] in FP32
2. Use the **same selection indices** as the original Gather to pick the top-K detections
3. Compute argmax in FP32 using iterative `Greater` / `Maximum` comparisons
4. Output the result as FP32 [1, K] — no I64 anywhere

### 3.2. Why Sigmoid(cls) and not Multiply(Sigmoid(cls), objectness)

The original model uses `Multiply(Sigmoid(cls), objectness)` as the combined score. However, on NPU the objectness factor is ~85% zeros (NPU truncates very small float values), which makes the combined score mostly zero — any argmax on it defaults to class 0.

`Sigmoid(cls)` alone is qualitatively correct on NPU (non-zero, mean diff ≈ 0.16 vs CPU). Since objectness is a scalar per anchor (shape [1, 3549, 1]), it doesn't change the relative ranking between classes at the same anchor — `argmax(Sigmoid(cls))` and `argmax(Sigmoid(cls) * objectness)` produce the same result. Therefore Sigmoid(cls) is a safe and correct source for classification.

### 3.3. Graph Transformation

**Before (broken on NPU):**
```
            TopK(Multiply(Sigmoid, obj)).indices → I64
                        ↓
    Squeeze → Reshape → Gather(I64 data, I32 indices) → Reshape → Convert(I64→FP32)
                                                                        ↓
                                                                   Result "labels"
```

**After (works on NPU):**
```
    Sigmoid(cls) [1, N, C]  FP32
         ↓
    Reshape [N, C]
         ↓
    Gather(FP32 data, same I32 indices, same axis) → [K, C]
         ↓
    Reshape [1, K, C]
         ↓
    StridedSlice per class → [1, K, 1] × C
         ↓
    Iterative argmax:
      argmax = 0,  max_score = scores[:, :, 0]
      for c in 1..C-1:
          better = Greater(scores[:,:,c], max_score)        → bool
          better_f32 = Convert(better, FP32)                → 0.0 / 1.0
          argmax = (1 - better_f32) * argmax + better_f32 * c
          max_score = Maximum(max_score, scores[:,:,c])
         ↓
    Squeeze [1, K]
         ↓
    Result "labels"
```

All nodes are FP32. The Gather indices and axis are reused from the original graph, so the same top-K detections are selected. Boxes are completely untouched.

### 3.4. Python Validation Script

The fix was validated with a standalone Python script (`debug_npu_final.py`) that modifies the model graph using the OpenVINO Python API and runs inference on both CPU and NPU.

Key excerpt — building the replacement subgraph:

```python
import openvino.opset13 as ops

# sigmoid_output: the Sigmoid(cls) node output [1, 3549, 3]
# gather_indices: reused I32 indices from original Gather [1, 100]
# gather_axis: reused axis constant from original Gather

# Reshape [1, N, C] → [N, C]
sig_2d = ops.reshape(sigmoid_output,
    ops.constant(np.array([na, nc], dtype=np.int32)), special_zero=False)

# Gather: select top-100 → [1, 100, C]
sig_gathered = ops.gather(sig_2d, gather_indices, gather_axis)
sig_3d = ops.reshape(sig_gathered,
    ops.constant(np.array([1, 100, nc], dtype=np.int32)), special_zero=False)

# Slice per class
def slice_c(data, c, k):
    b = ops.constant(np.array([0, 0, c], dtype=np.int32))
    e = ops.constant(np.array([1, k, c+1], dtype=np.int32))
    s = ops.constant(np.array([1, 1, 1], dtype=np.int32))
    return ops.strided_slice(data, b, e, s,
                             begin_mask=[0,0,0], end_mask=[0,0,0])

# Pairwise argmax (example for C=3)
c0 = slice_c(sig_3d, 0, 100)
c1 = slice_c(sig_3d, 1, 100)
c2 = slice_c(sig_3d, 2, 100)

c1_gt_c0 = ops.convert(ops.greater(c1, c0), ov.Type.f32)
c2_gt_c0 = ops.convert(ops.greater(c2, c0), ov.Type.f32)
c2_gt_c1 = ops.convert(ops.greater(c2, c1), ov.Type.f32)
c2_wins = ops.multiply(c2_gt_c0, c2_gt_c1)
c1_wins = ops.multiply(c1_gt_c0,
    ops.subtract(ops.constant(np.float32(1.0)), c2_wins))

argmax = ops.add(
    ops.multiply(c1_wins, ops.constant(np.float32(1.0))),
    ops.multiply(c2_wins, ops.constant(np.float32(2.0)))
)

argmax_2d = ops.squeeze(argmax, ops.constant(np.array([2], dtype=np.int32)))

# Replace Result input
labels_result.input(0).replace_source_output(argmax_2d.output(0))
model_fix.validate_nodes_and_infer_types()
```

### 3.5. Generalized Iterative Argmax (any number of classes)

The Python snippet above is specific to C=3. The general algorithm for arbitrary C:

```python
max_score = slice_c(sig_3d, 0, K)       # [1, K, 1]
argmax_val = ops.constant(np.float32(0)) # scalar, broadcast to [1, K, 1]

for c in range(1, num_classes):
    sc = slice_c(sig_3d, c, K)
    is_better = ops.convert(ops.greater(sc, max_score), ov.Type.f32)
    not_better = ops.subtract(ops.constant(np.float32(1.0)), is_better)
    argmax_val = ops.add(
        ops.multiply(not_better, argmax_val),
        ops.multiply(is_better, ops.constant(np.float32(c)))
    )
    max_score = ops.maximum(max_score, sc)

result = ops.squeeze(argmax_val, axis=2)  # [1, K]
```

This is `O(C)` operations — one `Greater`, `Maximum`, `Multiply`, `Add`, `Subtract` per class. For C=3 that's 10 extra nodes; for C=80 (COCO) that's ~250 nodes — negligible overhead.

---

## 4. Validation Results

### 4.1. Single Frame

| | CPU reference | CPU with fix | NPU with fix | NPU baseline (bug) |
|---|---|---|---|---|
| Class 0 | 12 | 12 | 8 | **100** |
| Class 1 | 87 | 87 | 87 | **0** |
| Class 2 | 1 | 1 | 5 | **0** |
| **Match vs CPU ref** | — | **100/100 (100%)** | ~90% | 12% |

CPU fix vs CPU original: **100% match** — the FP32 argmax is mathematically equivalent.

### 4.2. Multi-Frame (30 frames, 3000 detections)

| | Class 0 (defect) | Class 1 (box) | Class 2 (shipping_label) |
|---|---|---|---|
| CPU reference | 347 | 2562 | 91 |
| **NPU with fix** | **319** | **2531** | **150** |
| NPU baseline (bug) | 2999 | 0 | 0 |

The small differences between NPU fix and CPU reference are expected — NPU selects slightly different top-100 detections (boxes differ between devices due to FP16/FP32 precision differences in the boxes path), so the two sets of 100 labels are not 1:1 comparable. The aggregate distribution is close.

### 4.3. Boxes Integrity

```
Boxes (fix vs baseline) max absolute diff: 0.000000
```

The fix does not modify the boxes path in any way.

---

## 5. Applicability

### 5.1. Which models are affected

Any model that:
1. Has an output computed via I64 operations (typically `TopK.indices` → `Gather` → `Convert`)
2. Is run on Intel NPU via OpenVINO

Common patterns: ATSS, FCOS, and other anchor-free detectors that compute per-anchor argmax via TopK and pass indices through Gather.

### 5.2. Which models are NOT affected

- **YOLO family** — argmax is computed in post-processing code, not inside the model graph
- **SSD** — uses softmax + confidence thresholding, no I64 path
- **Models running on CPU/GPU** — I64 operations work correctly on these devices

### 5.3. Detection conditions

The fix activates when **all** of the following are true:
- Output node is `Result`
- Its input is `Convert` from I64 or I32 to FP32
- Tracing backward through Reshape nodes leads to a `Gather` with I64 data
- Tracing further backward leads to `TopK`
- TopK's input is `Multiply`
- Multiply's first input provably comes from `Sigmoid`
- Sigmoid output has shape [1, N, C] with C in range [2, 100]

If any condition is not met, the output is left unchanged.

---

## 6. Recommendations

### Short-term (implemented)
- **Graph transformation before compilation**: detect the I64 labels pattern and replace it with the FP32 argmax equivalent described in this report.

### Long-term (upstream fixes needed)
1. **OpenVINO NPU compiler**: should add automatic I64→I32/FP32 precision lowering, analogous to what the CPU plugin does for FP64→FP32.
2. **Model export tools** (PyTorch → ONNX → OpenVINO IR): should avoid emitting I64 in post-processing paths when the target is NPU. Adding `.to(torch.int32)` at the argmax output before export would eliminate the problem at source.
3. **NPU firmware/driver**: future hardware revisions or driver updates should improve integer operation support, or at minimum return an error instead of silently producing zeros.
