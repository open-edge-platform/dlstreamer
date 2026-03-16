#!/usr/bin/env python3
"""
Diagnostic: check if NPU outputs ANY non-zero labels by:
1. Replacing labels output with a known constant
2. Replacing labels with direct copy of boxes confidence
3. Adding intermediate outputs to trace where data becomes zero
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

# ==========================================
# Test A: Replace labels with constant
# ==========================================
print("=== Test A: Labels = constant [1,2,0,1,2,...] ===")
model_a = core.read_model(MODEL)
model_a.get_parameters()[0].set_layout(ov.Layout("NCHW"))
ov.set_batch(model_a, 1)

for res in model_a.get_results():
    for name in res.output(0).get_names():
        if "labels" in name:
            const_val = np.array([[i % 3 for i in range(100)]], dtype=np.float32)
            const_node = ops.constant(const_val)
            res.input(0).replace_source_output(const_node.output(0))
            print(f"  Replaced labels Result input with constant")
            break

model_a.validate_nodes_and_infer_types()
npu_a = core.compile_model(model_a, "NPU")
req_a = npu_a.create_infer_request()
req_a.infer({0: img})
labels_a = req_a.get_output_tensor(1).data.copy().flatten()
print(f"  NPU labels: unique={np.unique(labels_a)}, first 10: {labels_a[:10]}")

# ==========================================
# Test B: Labels = boxes confidence (5th column)
# ==========================================
print("\n=== Test B: Labels = slice of boxes output (confidence) ===")
model_b = core.read_model(MODEL)
model_b.get_parameters()[0].set_layout(ov.Layout("NCHW"))
ov.set_batch(model_b, 1)

# Find boxes result and labels result
boxes_result = None
labels_result = None
for res in model_b.get_results():
    for name in res.output(0).get_names():
        if "boxes" in name:
            boxes_result = res
        if "labels" in name:
            labels_result = res

# Connect labels to boxes output → slice to get just 1 column
boxes_source = boxes_result.input(0).get_source_output()
print(f"  Boxes source: {boxes_source.get_node().get_friendly_name()} shape={boxes_source.get_partial_shape()} type={boxes_source.get_element_type()}")

# Slice boxes to get [1,100,1] then reshape to [1,100]
# StridedSlice to get last column (confidence): boxes[:,:,4:5]
begin = ops.constant(np.array([0, 0, 4], dtype=np.int32))
end = ops.constant(np.array([1, 100, 5], dtype=np.int32))
strides = ops.constant(np.array([1, 1, 1], dtype=np.int32))
slice_node = ops.strided_slice(boxes_source, begin, end, strides, 
                                begin_mask=[0,0,0], end_mask=[0,0,0])
# Reshape [1,100,1] → [1,100]
new_shape = ops.constant(np.array([1, 100], dtype=np.int32))
reshaped = ops.reshape(slice_node, new_shape, special_zero=False)

labels_result.input(0).replace_source_output(reshaped.output(0))
model_b.validate_nodes_and_infer_types()

npu_b = core.compile_model(model_b, "NPU")
req_b = npu_b.create_infer_request()
req_b.infer({0: img})
labels_b = req_b.get_output_tensor(1).data.copy().flatten()
boxes_b = req_b.get_output_tensor(0).data.copy().reshape(-1, 5)
print(f"  NPU labels (should be confidence): first 10: {labels_b[:10]}")
print(f"  NPU boxes confidence column first 10: {boxes_b[:10, 4]}")
print(f"  Match: {np.allclose(labels_b, boxes_b[:, 4])}")

# ==========================================
# Test C: Add intermediate Result to see what TopK argmax produces
# ==========================================
print("\n=== Test C: Add output for TopK argmax path (FP32 version) ===")
model_c = core.read_model(MODEL)
model_c.get_parameters()[0].set_layout(ov.Layout("NCHW"))
ov.set_batch(model_c, 1)

# Find the argmax TopK and add FP32 argmax as a new output
labels_out_c = None
for out in model_c.outputs:
    if "labels" in out.get_names():
        labels_out_c = out
        break

result_c = labels_out_c.get_node()
convert_c = result_c.input(0).get_source_output().get_node()
node_c = convert_c
while node_c.get_type_name() != "Gather":
    node_c = node_c.input(0).get_source_output().get_node()
gather_c = node_c

node_c = gather_c.input(0).get_source_output().get_node()
while node_c.get_type_name() != "TopK":
    node_c = node_c.input(0).get_source_output().get_node()
topk_c = node_c

# Get cls_scores (TopK input)
cls_scores_c = topk_c.input(0).get_source_output()
scores_shape = cls_scores_c.get_partial_shape()
num_classes = scores_shape[2].get_length()
print(f"  Classification scores: {cls_scores_c.get_partial_shape()}")

# Create FP32 argmax computation and add it as NEW output (don't modify labels path)
class_idx = ops.constant(np.arange(num_classes, dtype=np.float32).reshape(1, 1, num_classes))
axis_c = ops.constant(np.array([2], dtype=np.int32))
max_sc = ops.reduce_max(cls_scores_c, axis_c, keep_dims=True)
eps = ops.constant(np.float32(1e-6))
diff_c = ops.abs(ops.subtract(cls_scores_c, max_sc))
mask_c = ops.convert(ops.less(diff_c, eps), ov.Type.f32)
weighted_c = ops.multiply(mask_c, class_idx)
argmax_c = ops.reduce_max(weighted_c, axis_c, keep_dims=False)  # [1, 3549]

# Also get the TopK output 0 (max scores) - this is FP32
topk_scores = topk_c.output(0)  # [1, 3549, 1] FP32

# Add argmax as new Result
from openvino._pyopenvino import op as ov_op
argmax_result = ov_op.Result(argmax_c.output(0))
argmax_result.set_friendly_name("argmax_result")

# Also add TopK scores as result (squeeze to [1, 3549])
squeeze_scores = ops.squeeze(topk_scores, ops.constant(np.array([2], dtype=np.int32)))
scores_result = ov_op.Result(squeeze_scores.output(0))
scores_result.set_friendly_name("scores_result")

model_c.add_results([argmax_result, scores_result])
model_c.validate_nodes_and_infer_types()

print(f"  Outputs: {[out.get_any_name() for out in model_c.outputs]}")

# CPU test
cpu_c = core.compile_model(model_c, "CPU")
req_cpu_c = cpu_c.create_infer_request()
req_cpu_c.infer({0: img})

# Find output indices
output_names = [out.get_any_name() for out in model_c.outputs]
print(f"  Output names: {output_names}")

# Get results
for i, out in enumerate(model_c.outputs):
    data = req_cpu_c.get_output_tensor(i).data.copy()
    name = out.get_any_name()
    if "argmax" in name:
        d = data.flatten()
        print(f"  CPU {name}: unique={np.unique(d)}, dist: 0={np.sum(d==0)}, 1={np.sum(d==1)}, 2={np.sum(d==2)}")
    elif "scores_result" in name:
        d = data.flatten()
        print(f"  CPU {name}: min={d.min():.6f}, max={d.max():.6f}, >0.01={np.sum(d>0.01)}")

# NPU test
npu_c = core.compile_model(model_c, "NPU")
req_npu_c = npu_c.create_infer_request()
req_npu_c.infer({0: img})

for i, out in enumerate(model_c.outputs):
    data = req_npu_c.get_output_tensor(i).data.copy()
    name = out.get_any_name()
    if "argmax" in name:
        d = data.flatten()
        print(f"  NPU {name}: unique={np.unique(d[:20])}, first 10: {d[:10]}")
    elif "scores_result" in name:
        d = data.flatten()
        print(f"  NPU {name}: min={d.min():.6f}, max={d.max():.6f}, >0.01={np.sum(d>0.01)}")
    elif "labels" in name:
        d = data.flatten()
        print(f"  NPU {name}: unique={np.unique(d)}, dist: 0={np.sum(d==0)}, 1={np.sum(d==1)}, 2={np.sum(d==2)}")
    elif "boxes" in name:
        pass  # skip boxes

# ==========================================
# Test D: Replace ENTIRE labels path with simple pass-through from argmax
# Just connect FP32 argmax → Gather → Result (bypass Squeeze/Reshape/Convert chain)
# ==========================================
print("\n=== Test D: Replace entire labels output with direct FP32 argmax + Gather ===")
model_d = core.read_model(MODEL)
model_d.get_parameters()[0].set_layout(ov.Layout("NCHW"))
ov.set_batch(model_d, 1)

# Find labels Result
labels_result_d = None
for res in model_d.get_results():
    for name in res.output(0).get_names():
        if "labels" in name:
            labels_result_d = res
            break

# Find TopK (argmax) 
labels_out_d = None
for out in model_d.outputs:
    if "labels" in out.get_names():
        labels_out_d = out
        break

node_d = labels_out_d.get_node().input(0).get_source_output().get_node()
while node_d.get_type_name() != "Gather":
    node_d = node_d.input(0).get_source_output().get_node()
gather_d = node_d

# Get Gather's indices input (input 1) - these are the top-100 selection
gather_indices = gather_d.input(1).get_source_output()
gather_axis = gather_d.input(2).get_source_output()
print(f"  Gather indices: {gather_indices.get_node().get_friendly_name()} type={gather_indices.get_element_type()} shape={gather_indices.get_partial_shape()}")
print(f"  Gather axis: {gather_axis.get_node().get_friendly_name()}")

# Find TopK
node_d = gather_d.input(0).get_source_output().get_node()
while node_d.get_type_name() != "TopK":
    node_d = node_d.input(0).get_source_output().get_node()
topk_d = node_d

cls_scores_d = topk_d.input(0).get_source_output()
nd = cls_scores_d.get_partial_shape()[2].get_length()

# Build FP32 argmax 
class_idx_d = ops.constant(np.arange(nd, dtype=np.float32).reshape(1, 1, nd))
axis_d = ops.constant(np.array([2], dtype=np.int32))
max_d = ops.reduce_max(cls_scores_d, axis_d, keep_dims=True)
eps_d = ops.constant(np.float32(1e-6))
diff_d = ops.abs(ops.subtract(cls_scores_d, max_d))
# Use ReduceMin approach for correct tie-breaking:
# where diff < eps: class_index, else: 999
mask_bool = ops.less(diff_d, eps_d)
large_val = ops.constant(np.float32(999.0))
weighted_d = ops.select(mask_bool, class_idx_d, large_val)
argmax_d = ops.reduce_min(weighted_d, axis_d, keep_dims=False)  # [1, N]

# Reshape argmax to [N, 1] for Gather (matching original data shape)
num_anchors = cls_scores_d.get_partial_shape()[1].get_length()
reshape_target = ops.constant(np.array([num_anchors, 1], dtype=np.int32))
argmax_reshaped = ops.reshape(argmax_d, reshape_target, special_zero=False)

# Create new Gather: gather top-100 from argmax
# But use same indices and axis as original
new_gather = ops.gather(argmax_reshaped, gather_indices, gather_axis)
# Reshape to [1, 100]
out_shape = ops.constant(np.array([1, 100], dtype=np.int32))
new_labels = ops.reshape(new_gather, out_shape, special_zero=False)

# Replace labels Result input directly
labels_result_d.input(0).replace_source_output(new_labels.output(0))

model_d.validate_nodes_and_infer_types()
for out in model_d.outputs:
    print(f"  Output '{out.get_any_name()}': {out.get_partial_shape()}, type: {out.get_element_type()}")

# CPU test
cpu_d = core.compile_model(model_d, "CPU")
req_cpu_d = cpu_d.create_infer_request()
req_cpu_d.infer({0: img})
cpu_d_labels = req_cpu_d.get_output_tensor(1).data.copy().flatten()
cpu_ref = req_cpu_c.get_output_tensor(1).data.copy().flatten()
print(f"  CPU (test D) labels: unique={np.unique(cpu_d_labels)}, dist: 0={np.sum(cpu_d_labels==0)}, 1={np.sum(cpu_d_labels==1)}, 2={np.sum(cpu_d_labels==2)}")
print(f"  CPU (original) labels: unique={np.unique(cpu_ref)}, dist: 0={np.sum(cpu_ref==0)}, 1={np.sum(cpu_ref==1)}, 2={np.sum(cpu_ref==2)}")
cpu_match_d = np.sum(cpu_d_labels == cpu_ref)
print(f"  CPU match: {cpu_match_d}/{len(cpu_ref)} ({100*cpu_match_d/len(cpu_ref):.1f}%)")

# NPU test
npu_d = core.compile_model(model_d, "NPU")
req_npu_d = npu_d.create_infer_request()
req_npu_d.infer({0: img})
npu_d_labels = req_npu_d.get_output_tensor(1).data.copy().flatten()
print(f"  NPU (test D) labels: unique={np.unique(npu_d_labels)}")
print(f"  NPU (test D) dist: 0={np.sum(npu_d_labels==0)}, 1={np.sum(npu_d_labels==1)}, 2={np.sum(npu_d_labels==2)}")
print(f"  NPU (test D) first 20: {npu_d_labels[:20]}")
