#!/usr/bin/env python3
"""
FINAL FIX: Use Sigmoid(cls) (NOT Multiply) as the source for argmax.
The objectness factor affects ranking (top-100 selection) but NOT which class
each detection belongs to. Sigmoid(cls) values are correct on NPU.

Plan:
1. Find Sigmoid(cls) [1, 3549, 3] - the raw classification probabilities
2. Reshape to [3549, 3] → Gather with same indices as labels → [1, 100, 3]
3. Pairwise argmax → [1, 100]
4. Replace labels Result
"""
import numpy as np
import openvino as ov
import openvino.opset13 as ops
from openvino._pyopenvino import op as ov_op
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
model_ref = core.read_model(MODEL)
model_ref.get_parameters()[0].set_layout(ov.Layout("NCHW"))
ov.set_batch(model_ref, 1)
cpu_ref = core.compile_model(model_ref, "CPU")
r_ref = cpu_ref.create_infer_request()
r_ref.infer({0: img})
ref_labels = r_ref.get_output_tensor(1).data.copy().flatten()
print(f"CPU ref: unique={np.unique(ref_labels)}, dist: 0={np.sum(ref_labels==0)}, 1={np.sum(ref_labels==1)}, 2={np.sum(ref_labels==2)}")

# ==========================================
# Build fix model
# ==========================================
model_fix = core.read_model(MODEL)
model_fix.get_parameters()[0].set_layout(ov.Layout("NCHW"))
ov.set_batch(model_fix, 1)

# Find labels path: Result → Convert → Reshape → Gather → ... → TopK → Multiply → Sigmoid
labels_out = None
for out in model_fix.outputs:
    if "labels" in out.get_names():
        labels_out = out
        break

labels_result = labels_out.get_node()
n = labels_result.input(0).get_source_output().get_node()  # Convert
while n.get_type_name() != "Gather":
    n = n.input(0).get_source_output().get_node()
labels_gather = n

# Get Gather indices
gather_indices = labels_gather.input(1).get_source_output()
gather_axis = labels_gather.input(2).get_source_output()

# Find TopK and its inputs
n = labels_gather.input(0).get_source_output().get_node()
while n.get_type_name() != "TopK":
    n = n.input(0).get_source_output().get_node()
topk = n

# Multiply is TopK's input
multiply = topk.input(0).get_source_output().get_node()
# Sigmoid is Multiply's input 0
sigmoid_output = multiply.input(0).get_source_output()  # [1, 3549, 3] FP32

na = sigmoid_output.get_partial_shape()[1].get_length()
nc = sigmoid_output.get_partial_shape()[2].get_length()

print(f"Sigmoid(cls): shape={sigmoid_output.get_partial_shape()} type={sigmoid_output.get_element_type()}")
print(f"Anchors: {na}, Classes: {nc}")

# Reshape Sigmoid from [1, 3549, 3] to [3549, 3]
sig_2d = ops.reshape(sigmoid_output, ops.constant(np.array([na, nc], dtype=np.int32)), special_zero=False)

# Gather: select top-100 from [3549, 3] → [1, 100, 3]
sig_gathered = ops.gather(sig_2d, gather_indices, gather_axis)

# Reshape to [1, 100, 3]  
sig_3d = ops.reshape(sig_gathered, ops.constant(np.array([1, 100, nc], dtype=np.int32)), special_zero=False)

# Pairwise argmax for C=3
def slice_c(data, c, n):
    b = ops.constant(np.array([0, 0, c], dtype=np.int32))
    e = ops.constant(np.array([1, n, c+1], dtype=np.int32))
    s = ops.constant(np.array([1, 1, 1], dtype=np.int32))
    return ops.strided_slice(data, b, e, s, begin_mask=[0,0,0], end_mask=[0,0,0])

c0 = slice_c(sig_3d, 0, 100)  # [1, 100, 1]
c1 = slice_c(sig_3d, 1, 100)
c2 = slice_c(sig_3d, 2, 100)

c1_gt_c0 = ops.convert(ops.greater(c1, c0), ov.Type.f32)
c2_gt_c0 = ops.convert(ops.greater(c2, c0), ov.Type.f32)
c2_gt_c1 = ops.convert(ops.greater(c2, c1), ov.Type.f32)
c2_wins = ops.multiply(c2_gt_c0, c2_gt_c1)
c1_wins = ops.multiply(c1_gt_c0, ops.subtract(ops.constant(np.float32(1.0)), c2_wins))
argmax = ops.add(
    ops.multiply(c1_wins, ops.constant(np.float32(1.0))),
    ops.multiply(c2_wins, ops.constant(np.float32(2.0)))
)  # [1, 100, 1]

argmax_2d = ops.squeeze(argmax, ops.constant(np.array([2], dtype=np.int32)))  # [1, 100]

# Replace labels Result
labels_result.input(0).replace_source_output(argmax_2d.output(0))
model_fix.validate_nodes_and_infer_types()

for out in model_fix.outputs:
    print(f"  Output '{out.get_any_name()}': {out.get_partial_shape()}, type: {out.get_element_type()}")

# CPU validation
print("\n--- CPU validation ---")
cpu_fix = core.compile_model(model_fix, "CPU")
rc = cpu_fix.create_infer_request()
rc.infer({0: img})
fix_cpu = rc.get_output_tensor(1).data.copy().flatten()
print(f"CPU fix:  unique={np.unique(fix_cpu)}, dist: 0={np.sum(fix_cpu==0)}, 1={np.sum(fix_cpu==1)}, 2={np.sum(fix_cpu==2)}")
cpu_match = np.sum(ref_labels == fix_cpu)
print(f"CPU match: {cpu_match}/{len(ref_labels)} ({100*cpu_match/len(ref_labels):.1f}%)")

# NPU test
print("\n--- NPU test ---")
npu_fix = core.compile_model(model_fix, "NPU")
rn = npu_fix.create_infer_request()
rn.infer({0: img})
fix_npu = rn.get_output_tensor(1).data.copy().flatten()
print(f"NPU fix:  unique={np.unique(fix_npu)}, dist: 0={np.sum(fix_npu==0)}, 1={np.sum(fix_npu==1)}, 2={np.sum(fix_npu==2)}")
print(f"NPU fix first 20: {fix_npu[:20]}")

# Baseline
model_base = core.read_model(MODEL)
model_base.get_parameters()[0].set_layout(ov.Layout("NCHW"))
ov.set_batch(model_base, 1)
npu_base = core.compile_model(model_base, "NPU")
rb = npu_base.create_infer_request()
rb.infer({0: img})
base_npu = rb.get_output_tensor(1).data.copy().flatten()
print(f"NPU base: unique={np.unique(base_npu)}")

# Boxes check
fix_boxes = rn.get_output_tensor(0).data.copy().reshape(-1, 5)
base_boxes = rb.get_output_tensor(0).data.copy().reshape(-1, 5)
print(f"Boxes fix vs base diff: {np.abs(fix_boxes - base_boxes).max():.6f}")

# Multi-frame
print("\n\n=== Multi-frame validation ===")
cap = cv2.VideoCapture(VIDEO)
tot_cpu = {0:0, 1:0, 2:0}
tot_npu = {0:0, 1:0, 2:0}
tot_base = {0:0, 1:0, 2:0}
frames = 0

for idx in range(0, 600, 20):
    cap.set(cv2.CAP_PROP_POS_FRAMES, idx)
    ret, frame = cap.read()
    if not ret:
        break
    test_img = cv2.resize(frame, (w, h)).transpose(2, 0, 1)[np.newaxis].astype(np.float32)
    
    r_ref.infer({0: test_img})
    cl = r_ref.get_output_tensor(1).data.copy().flatten()
    
    rn.infer({0: test_img})
    nl = rn.get_output_tensor(1).data.copy().flatten()
    
    rb.infer({0: test_img})
    bl = rb.get_output_tensor(1).data.copy().flatten()
    
    for k in range(3):
        tot_cpu[k] += int(np.sum(cl == k))
        tot_npu[k] += int(np.sum(nl == k))
        tot_base[k] += int(np.sum(bl == k))
    frames += 1

cap.release()
print(f"Frames tested: {frames}")
print(f"CPU ref:   0={tot_cpu[0]:5d}, 1={tot_cpu[1]:5d}, 2={tot_cpu[2]:5d}")
print(f"NPU fix:   0={tot_npu[0]:5d}, 1={tot_npu[1]:5d}, 2={tot_npu[2]:5d}")
print(f"NPU base:  0={tot_base[0]:5d}, 1={tot_base[1]:5d}, 2={tot_base[2]:5d}")
