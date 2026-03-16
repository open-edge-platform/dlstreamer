#!/usr/bin/env python3
"""
Focused test: Replace TopK I64 argmax with pure FP32 argmax computation.
This completely avoids I64 operations on NPU for the labels path.
"""
import numpy as np
import openvino as ov
import openvino.opset13 as ops
import cv2

MODEL = "/home/dlstreamer/dlstreamer/dynamic_batch_models/optimized_model.xml"
VIDEO = "/home/dlstreamer/video-examples/warehouse.avi"

core = ov.Core()

cap = cv2.VideoCapture(VIDEO)
for _ in range(60):
    ret, frame = cap.read()
cap.release()

h, w = 416, 416
img = cv2.resize(frame, (w, h)).transpose(2, 0, 1)[np.newaxis].astype(np.float32)

# CPU reference
model_cpu = core.read_model(MODEL)
model_cpu.get_parameters()[0].set_layout(ov.Layout("NCHW"))
ov.set_batch(model_cpu, 1)
cpu_compiled = core.compile_model(model_cpu, "CPU")
cpu_req = cpu_compiled.create_infer_request()
cpu_req.infer({0: img})
cpu_labels = cpu_req.get_output_tensor(1).data.copy().flatten()
print(f"CPU labels: unique={np.unique(cpu_labels)}, dist: 0={np.sum(cpu_labels==0)}, 1={np.sum(cpu_labels==1)}, 2={np.sum(cpu_labels==2)}")

# ==========================================
# FP32 argmax replacement
# ==========================================
print("\n=== FP32 argmax replacement ===")
model_fix = core.read_model(MODEL)
model_fix.get_parameters()[0].set_layout(ov.Layout("NCHW"))
ov.set_batch(model_fix, 1)

# Find labels path: Result → Convert(I64→FP32) → Reshape → Gather → Reshape → Squeeze → TopK
labels_out = None
for out in model_fix.outputs:
    if "labels" in out.get_names():
        labels_out = out
        break

result_n = labels_out.get_node()
convert_n = result_n.input(0).get_source_output().get_node()
print(f"  Convert: {convert_n.get_type_name()} '{convert_n.get_friendly_name()}'")

# Find Gather
node = convert_n
while node.get_type_name() != "Gather":
    node = node.input(0).get_source_output().get_node()
gather_n = node
print(f"  Gather: '{gather_n.get_friendly_name()}'")

# Gather data → Reshape → Squeeze → TopK
node = gather_n.input(0).get_source_output().get_node()
while node.get_type_name() != "TopK":
    node = node.input(0).get_source_output().get_node()
topk_n = node
print(f"  TopK: '{topk_n.get_friendly_name()}'")

cls_scores = topk_n.input(0).get_source_output()
scores_shape = cls_scores.get_partial_shape()
num_classes = scores_shape[2].get_length()
print(f"  Classification scores: shape={cls_scores.get_partial_shape()} type={cls_scores.get_element_type()}")
print(f"  num_classes={num_classes}")

# Create FP32 argmax:
# scores [1, N, C] -> index of max class per anchor
# Method: ReduceMax → compare → multiply by [0,1,...,C-1] → ReduceMax
class_idx_arr = np.arange(num_classes, dtype=np.float32).reshape(1, 1, num_classes)
class_indices = ops.constant(class_idx_arr)
axis_const = ops.constant(np.array([2], dtype=np.int32))

# Max score per anchor
max_scores = ops.reduce_max(cls_scores, axis_const, keep_dims=True)

# Mask: 1 where score == max (within epsilon)
epsilon = ops.constant(np.float32(1e-6))
diff = ops.abs(ops.subtract(cls_scores, max_scores))
mask = ops.convert(ops.less(diff, epsilon), ov.Type.f32)

# Weighted class indices → argmax
weighted = ops.multiply(mask, class_indices)
argmax_f32 = ops.reduce_max(weighted, axis_const, keep_dims=True)  # [1, N, 1]

print(f"  FP32 argmax created, shape will be [1, N, 1]")

# Find Squeeze connected to TopK output 1 and replace its input
squeeze_found = False
for target_input in topk_n.output(1).get_target_inputs():
    target_node = target_input.get_node()
    if target_node.get_type_name() == "Squeeze":
        target_input.replace_source_output(argmax_f32.output(0))
        squeeze_found = True
        print(f"  -> Replaced Squeeze '{target_node.get_friendly_name()}' input with FP32 argmax")
        break

if not squeeze_found:
    print("  ERROR: Squeeze not found!")
    exit(1)

model_fix.validate_nodes_and_infer_types()
for out in model_fix.outputs:
    print(f"  Output '{out.get_any_name()}': {out.get_partial_shape()}, type: {out.get_element_type()}")

# Test on CPU to verify mathematical correctness
print("\n--- CPU validation of FP32 argmax ---")
cpu_fix = core.compile_model(model_fix, "CPU")
req_cpu = cpu_fix.create_infer_request()
req_cpu.infer({0: img})
fix_cpu_labels = req_cpu.get_output_tensor(1).data.copy().flatten()
print(f"CPU (fix) labels unique: {np.unique(fix_cpu_labels)}")
print(f"CPU (fix) dist: 0={np.sum(fix_cpu_labels==0)}, 1={np.sum(fix_cpu_labels==1)}, 2={np.sum(fix_cpu_labels==2)}")
match_cpu = np.sum(cpu_labels == fix_cpu_labels)
print(f"CPU fix vs CPU original: {match_cpu}/{len(cpu_labels)} ({100*match_cpu/len(cpu_labels):.1f}%)")

# Test on NPU
print("\n--- NPU test ---")
npu_fix = core.compile_model(model_fix, "NPU")
req_npu = npu_fix.create_infer_request()
req_npu.infer({0: img})
fix_npu_labels = req_npu.get_output_tensor(1).data.copy().flatten()
fix_npu_boxes = req_npu.get_output_tensor(0).data.copy().reshape(-1, 5)

print(f"NPU (fix) labels unique: {np.unique(fix_npu_labels)}")
print(f"NPU (fix) dist: 0={np.sum(fix_npu_labels==0)}, 1={np.sum(fix_npu_labels==1)}, 2={np.sum(fix_npu_labels==2)}")
print(f"NPU (fix) first 20: {fix_npu_labels[:20]}")

# NPU baseline for comparison
model_base = core.read_model(MODEL)
model_base.get_parameters()[0].set_layout(ov.Layout("NCHW"))
ov.set_batch(model_base, 1)
npu_base = core.compile_model(model_base, "NPU")
req_base = npu_base.create_infer_request()
req_base.infer({0: img})
base_labels = req_base.get_output_tensor(1).data.copy().flatten()
base_boxes = req_base.get_output_tensor(0).data.copy().reshape(-1, 5)

print(f"\nNPU (baseline) labels unique: {np.unique(base_labels)}")
print(f"NPU (baseline) dist: 0={np.sum(base_labels==0)}, 1={np.sum(base_labels==1)}, 2={np.sum(base_labels==2)}")

# Check if boxes changed (they shouldn't since we only modified labels path)
box_diff = np.abs(fix_npu_boxes - base_boxes)
print(f"\nBoxes diff (fix vs baseline): max={box_diff.max():.6f}, mean={box_diff.mean():.6f}")

# Multi-frame test
print("\n\n=== Multi-frame validation ===")
cap = cv2.VideoCapture(VIDEO)
total_cpu_dist = {0: 0, 1: 0, 2: 0}
total_npu_fix_dist = {0: 0, 1: 0, 2: 0}
total_npu_base_dist = {0: 0, 1: 0, 2: 0}
frames = 0

for frame_idx in range(0, 300, 30):
    cap.set(cv2.CAP_PROP_POS_FRAMES, frame_idx)
    ret, frame = cap.read()
    if not ret:
        break
    
    img = cv2.resize(frame, (w, h)).transpose(2, 0, 1)[np.newaxis].astype(np.float32)
    
    req_cpu.infer({0: img})
    c_labels = req_cpu.get_output_tensor(1).data.copy().flatten()
    
    req_npu.infer({0: img})
    n_labels = req_npu.get_output_tensor(1).data.copy().flatten()
    
    req_base.infer({0: img})
    b_labels = req_base.get_output_tensor(1).data.copy().flatten()
    
    for k in range(3):
        total_cpu_dist[k] += int(np.sum(c_labels == k))
        total_npu_fix_dist[k] += int(np.sum(n_labels == k))
        total_npu_base_dist[k] += int(np.sum(b_labels == k))
    
    frames += 1

cap.release()

print(f"Frames tested: {frames}")
print(f"CPU total:          0={total_cpu_dist[0]:5d}, 1={total_cpu_dist[1]:5d}, 2={total_cpu_dist[2]:5d}")
print(f"NPU fixed total:    0={total_npu_fix_dist[0]:5d}, 1={total_npu_fix_dist[1]:5d}, 2={total_npu_fix_dist[2]:5d}")
print(f"NPU baseline total: 0={total_npu_base_dist[0]:5d}, 1={total_npu_base_dist[1]:5d}, 2={total_npu_base_dist[2]:5d}")
