#!/usr/bin/env python3
"""
1. Trace the boxes output path to understand how top-100 are selected
2. Try: add FP32 classification scores for the top-100 boxes as extra output
3. Try: use the same Gather mechanism as boxes to select classification scores
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

# ==========================================
# Trace the boxes path
# ==========================================
print("=== Tracing BOXES output path ===")
model_t = core.read_model(MODEL)
model_t.get_parameters()[0].set_layout(ov.Layout("NCHW"))
ov.set_batch(model_t, 1)

boxes_out = None
labels_out = None
for out in model_t.outputs:
    if "boxes" in out.get_names():
        boxes_out = out
    if "labels" in out.get_names():
        labels_out = out

def trace_back(node, depth=0, max_depth=8, visited=None):
    if visited is None:
        visited = set()
    nid = id(node)
    if nid in visited or depth > max_depth:
        return
    visited.add(nid)
    
    types = [str(node.get_output_element_type(i)) for i in range(node.get_output_size())]
    shapes = []
    for i in range(node.get_output_size()):
        try:
            shapes.append(str(node.get_output_partial_shape(i)))
        except:
            shapes.append("?")
    
    print(f"{'  '*depth}{node.get_type_name()} '{node.get_friendly_name()}' types={types} shapes={shapes}")
    
    for i in range(node.get_input_size()):
        src = node.input(i).get_source_output().get_node()
        trace_back(src, depth+1, max_depth, visited)

boxes_result = boxes_out.get_node()
trace_back(boxes_result, depth=0, max_depth=6)

print("\n\n=== Tracing LABELS output path ===")
labels_result = labels_out.get_node()
trace_back(labels_result, depth=0, max_depth=6)

# ==========================================
# Find shared Gather indices between boxes and labels paths
# ==========================================
print("\n\n=== Comparing Gather operations ===")

# Find all Gather nodes
all_gathers = []
for op in model_t.get_ordered_ops():
    if op.get_type_name() == "Gather":
        data_type = op.input(0).get_source_output().get_element_type()
        idx_type = op.input(1).get_source_output().get_element_type()
        data_shape = op.input(0).get_source_output().get_partial_shape()
        idx_shape = op.input(1).get_source_output().get_partial_shape()
        out_shape = op.get_output_partial_shape(0)
        out_type = op.get_output_element_type(0)
        
        idx_src = op.input(1).get_source_output().get_node().get_friendly_name()
        
        print(f"  Gather '{op.get_friendly_name()}':")
        print(f"    data: {data_type} {data_shape}")
        print(f"    indices: {idx_type} {idx_shape} from '{idx_src}'")
        print(f"    output: {out_type} {out_shape}")
        
        all_gathers.append(op)

# ==========================================
# KEY FIX: Create new labels output by selecting from Sigmoid(cls) 
# using the SAME mechanism as boxes Gather
# ==========================================
print("\n\n=== Fix: Add Sigmoid(cls) scores selected by boxes Gather indices ===")

model_fix = core.read_model(MODEL)
model_fix.get_parameters()[0].set_layout(ov.Layout("NCHW"))
ov.set_batch(model_fix, 1)

# Find labels path
for out in model_fix.outputs:
    if "labels" in out.get_names():
        labels_out_f = out
        break

labels_result_f = labels_out_f.get_node()
n = labels_result_f.input(0).get_source_output().get_node()  # Convert
while n.get_type_name() != "Gather":
    n = n.input(0).get_source_output().get_node()
labels_gather = n

# Get Gather indices and axis
gather_indices = labels_gather.input(1).get_source_output()
gather_axis = labels_gather.input(2).get_source_output()
print(f"Labels Gather indices: {gather_indices.get_node().get_friendly_name()} type={gather_indices.get_element_type()} shape={gather_indices.get_partial_shape()}")

# Find TopK (argmax) and get classification scores
n = labels_gather.input(0).get_source_output().get_node()
while n.get_type_name() != "TopK":
    n = n.input(0).get_source_output().get_node()
argmax_topk = n
cls_multiply = argmax_topk.input(0).get_source_output()  # [1, 3549, 3] FP32

# Instead of using I64 argmax, compute argmax from FP32 classification scores
# Selected by same Gather indices

# cls_multiply is [1, 3549, 3]. We need [3549, 3] for Gather axis=0
# Or we could reshape to match Gather axis
print(f"Cls scores: shape={cls_multiply.get_partial_shape()} type={cls_multiply.get_element_type()}")
print(f"Gather indices: shape={gather_indices.get_partial_shape()}")

# The original Gather selects from [3549, 1] using indices [1, 100] on axis=0
# To select from cls_scores [1, 3549, 3], we need:
# Reshape to [3549, 3] → Gather(indices, axis=0) → [100, 3] or [1, 100, 3]
# Then argmax(axis=-1) → [1, 100]

na = cls_multiply.get_partial_shape()[1].get_length()
nc = cls_multiply.get_partial_shape()[2].get_length()

# Reshape cls_scores from [1, 3549, 3] to [3549, 3]
cls_2d = ops.reshape(cls_multiply, ops.constant(np.array([na, nc], dtype=np.int32)), special_zero=False)

# Use same Gather indices and axis as labels Gather  
cls_gathered = ops.gather(cls_2d, gather_indices, gather_axis)  # [1, 100, 3] or similar
print(f"Gathered cls shape: {cls_gathered.get_output_partial_shape(0)}")

# Compute argmax using pairwise comparisons (for C=3)
# Reshape to [1, 100, 3] if needed
cls_3d = ops.reshape(cls_gathered, ops.constant(np.array([1, 100, nc], dtype=np.int32)), special_zero=False)

# Slice per class
def slice_c(data, c, n):
    b = ops.constant(np.array([0, 0, c], dtype=np.int32))
    e = ops.constant(np.array([1, n, c+1], dtype=np.int32))
    s = ops.constant(np.array([1, 1, 1], dtype=np.int32))
    return ops.strided_slice(data, b, e, s, begin_mask=[0,0,0], end_mask=[0,0,0])

c0 = slice_c(cls_3d, 0, 100)  # [1, 100, 1]
c1 = slice_c(cls_3d, 1, 100)
c2 = slice_c(cls_3d, 2, 100)

# Pairwise argmax
c1_gt_c0 = ops.convert(ops.greater(c1, c0), ov.Type.f32)
c2_gt_c0 = ops.convert(ops.greater(c2, c0), ov.Type.f32)
c2_gt_c1 = ops.convert(ops.greater(c2, c1), ov.Type.f32)
c2_wins = ops.multiply(c2_gt_c0, c2_gt_c1)
c1_wins = ops.multiply(c1_gt_c0, ops.subtract(ops.constant(np.float32(1.0)), c2_wins))
argmax = ops.add(
    ops.multiply(c1_wins, ops.constant(np.float32(1.0))),
    ops.multiply(c2_wins, ops.constant(np.float32(2.0)))
)  # [1, 100, 1]

# Squeeze to [1, 100]
argmax_2d = ops.squeeze(argmax, ops.constant(np.array([2], dtype=np.int32)))

# Replace the labels Result input with our computed argmax
labels_result_f.input(0).replace_source_output(argmax_2d.output(0))
model_fix.validate_nodes_and_infer_types()

for out in model_fix.outputs:
    print(f"  Output '{out.get_any_name()}': {out.get_partial_shape()}, type: {out.get_element_type()}")

# CPU validation
print("\n--- CPU validation ---")
cpu_ref = core.compile_model(core.read_model(MODEL), "CPU")
# Need to set batch
model_ref = core.read_model(MODEL)
model_ref.get_parameters()[0].set_layout(ov.Layout("NCHW"))
ov.set_batch(model_ref, 1)
cpu_ref = core.compile_model(model_ref, "CPU")
rc = cpu_ref.create_infer_request()
rc.infer({0: img})
ref_labels = rc.get_output_tensor(1).data.copy().flatten()

cpu_fix = core.compile_model(model_fix, "CPU")
rcf = cpu_fix.create_infer_request()
rcf.infer({0: img})
fix_cpu_labels = rcf.get_output_tensor(1).data.copy().flatten()
print(f"CPU ref:  unique={np.unique(ref_labels)}, dist: 0={np.sum(ref_labels==0)}, 1={np.sum(ref_labels==1)}, 2={np.sum(ref_labels==2)}")
print(f"CPU fix:  unique={np.unique(fix_cpu_labels)}, dist: 0={np.sum(fix_cpu_labels==0)}, 1={np.sum(fix_cpu_labels==1)}, 2={np.sum(fix_cpu_labels==2)}")
cpu_match = np.sum(ref_labels == fix_cpu_labels)
print(f"CPU match: {cpu_match}/{len(ref_labels)} ({100*cpu_match/len(ref_labels):.1f}%)")

# NPU test
print("\n--- NPU test ---")
npu_fix = core.compile_model(model_fix, "NPU")
rnf = npu_fix.create_infer_request()
rnf.infer({0: img})
fix_npu_labels = rnf.get_output_tensor(1).data.copy().flatten()
fix_npu_boxes = rnf.get_output_tensor(0).data.copy().reshape(-1, 5)

print(f"NPU fix:  unique={np.unique(fix_npu_labels)}, dist: 0={np.sum(fix_npu_labels==0)}, 1={np.sum(fix_npu_labels==1)}, 2={np.sum(fix_npu_labels==2)}")
print(f"NPU fix first 20: {fix_npu_labels[:20]}")

# NPU baseline
model_base = core.read_model(MODEL)
model_base.get_parameters()[0].set_layout(ov.Layout("NCHW"))
ov.set_batch(model_base, 1)
npu_base = core.compile_model(model_base, "NPU")
rnb = npu_base.create_infer_request()
rnb.infer({0: img})
base_labels = rnb.get_output_tensor(1).data.copy().flatten()
base_boxes = rnb.get_output_tensor(0).data.copy().reshape(-1, 5)

print(f"NPU base: unique={np.unique(base_labels)}, dist: 0={np.sum(base_labels==0)}")

# Compare boxes between fix and baseline (should be identical since Gather uses same indices)
box_diff = np.abs(fix_npu_boxes - base_boxes)
print(f"NPU boxes fix vs base: max_diff={box_diff.max():.6f}")

# Multi-frame test if NPU fix works
if np.any(fix_npu_labels != 0):
    print("\n\n=== Multi-frame validation ===")
    cap = cv2.VideoCapture(VIDEO)
    
    total_cpu = {0:0, 1:0, 2:0}
    total_npu = {0:0, 1:0, 2:0}
    frames = 0
    
    for idx in range(0, 300, 30):
        cap.set(cv2.CAP_PROP_POS_FRAMES, idx)
        ret, frame = cap.read()
        if not ret:
            break
        test_img = cv2.resize(frame, (w, h)).transpose(2, 0, 1)[np.newaxis].astype(np.float32)
        
        rc.infer({0: test_img})
        cl = rc.get_output_tensor(1).data.copy().flatten()
        
        rnf.infer({0: test_img})
        nl = rnf.get_output_tensor(1).data.copy().flatten()
        
        for k in range(3):
            total_cpu[k] += int(np.sum(cl == k))
            total_npu[k] += int(np.sum(nl == k))
        frames += 1
    
    cap.release()
    print(f"Frames: {frames}")
    print(f"CPU: 0={total_cpu[0]:5d}, 1={total_cpu[1]:5d}, 2={total_cpu[2]:5d}")
    print(f"NPU: 0={total_npu[0]:5d}, 1={total_npu[1]:5d}, 2={total_npu[2]:5d}")
