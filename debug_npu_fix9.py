#!/usr/bin/env python3
"""
Test pairwise-comparison FP32 argmax that avoids epsilon comparisons.
For C=3 classes: use Greater to determine argmax via pairwise class comparisons.
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
cpu = core.compile_model(model_cpu, "CPU")
req_cpu = cpu.create_infer_request()
req_cpu.infer({0: img})
cpu_labels = req_cpu.get_output_tensor(1).data.copy().flatten()
print(f"CPU ref labels: unique={np.unique(cpu_labels)}, dist: 0={np.sum(cpu_labels==0)}, 1={np.sum(cpu_labels==1)}, 2={np.sum(cpu_labels==2)}")

# Build model with pairwise FP32 argmax
model_fix = core.read_model(MODEL)
model_fix.get_parameters()[0].set_layout(ov.Layout("NCHW"))
ov.set_batch(model_fix, 1)

# Find labels path
labels_out = None
for out in model_fix.outputs:
    if "labels" in out.get_names():
        labels_out = out
        break

# Trace to TopK
result_n = labels_out.get_node()
n = result_n.input(0).get_source_output().get_node()  # Convert
while n.get_type_name() != "Gather":
    n = n.input(0).get_source_output().get_node()
gather_n = n
n = gather_n.input(0).get_source_output().get_node()
while n.get_type_name() != "TopK":
    n = n.input(0).get_source_output().get_node()
topk_n = n

cls_scores = topk_n.input(0).get_source_output()  # [1, 3549, 3]
scores_shape = cls_scores.get_partial_shape()
num_anchors = scores_shape[1].get_length()
num_classes = scores_shape[2].get_length()
print(f"Cls scores: shape={scores_shape}, classes={num_classes}")

# Pairwise argmax for C=3:
# Extract per-class scores via StridedSlice
def slice_class(scores, c, na):
    """Extract scores for class c: [1, N, 1]"""
    begin = ops.constant(np.array([0, 0, c], dtype=np.int32))
    end = ops.constant(np.array([1, na, c + 1], dtype=np.int32))
    strides = ops.constant(np.array([1, 1, 1], dtype=np.int32))
    return ops.strided_slice(scores, begin, end, strides,
                              begin_mask=[0, 0, 0], end_mask=[0, 0, 0])

s0 = slice_class(cls_scores, 0, num_anchors)  # [1, N, 1]
s1 = slice_class(cls_scores, 1, num_anchors)
s2 = slice_class(cls_scores, 2, num_anchors)

# Pairwise Greater comparisons (strict >)
c1_beats_0 = ops.convert(ops.greater(s1, s0), ov.Type.f32)  # 1 if s1>s0
c2_beats_0 = ops.convert(ops.greater(s2, s0), ov.Type.f32)  # 1 if s2>s0  
c2_beats_1 = ops.convert(ops.greater(s2, s1), ov.Type.f32)  # 1 if s2>s1

# c2 wins if it beats both c0 and c1
c2_wins = ops.multiply(c2_beats_0, c2_beats_1)  # AND as multiply

# c1 wins if it beats c0 AND c2 doesn't win
one = ops.constant(np.float32(1.0))
c1_wins = ops.multiply(c1_beats_0, ops.subtract(one, c2_wins))

# argmax = 1*c1_wins + 2*c2_wins
two = ops.constant(np.float32(2.0))
argmax_fp32 = ops.add(
    ops.multiply(c1_wins, one),
    ops.multiply(c2_wins, two)
)  # [1, N, 1]

# Squeeze to [1, N]  
squeeze_axis = ops.constant(np.array([2], dtype=np.int32))
argmax_squeezed = ops.squeeze(argmax_fp32, squeeze_axis)  # [1, N]

# === Test 1: Direct slice (no Gather), first 100 elements ===
print("\n=== Test 1: Pairwise argmax, first 100 (no Gather) ===")
model_1 = core.read_model(MODEL)
model_1.get_parameters()[0].set_layout(ov.Layout("NCHW"))
ov.set_batch(model_1, 1)

# Find labels result
for res in model_1.get_results():
    for name in res.output(0).get_names():
        if "labels" in name:
            res1 = res
            break

# Find TopK
n1 = res1
for _ in range(20):  # trace back
    n1 = n1.input(0).get_source_output().get_node()
    if n1.get_type_name() == "TopK":
        break
topk1 = n1
cs1 = topk1.input(0).get_source_output()
na1 = cs1.get_partial_shape()[1].get_length()

# Build pairwise argmax
s0_1 = slice_class(cs1, 0, na1)
s1_1 = slice_class(cs1, 1, na1)
s2_1 = slice_class(cs1, 2, na1)
c1b0 = ops.convert(ops.greater(s1_1, s0_1), ov.Type.f32)
c2b0 = ops.convert(ops.greater(s2_1, s0_1), ov.Type.f32)
c2b1 = ops.convert(ops.greater(s2_1, s1_1), ov.Type.f32)
c2w = ops.multiply(c2b0, c2b1)
c1w = ops.multiply(c1b0, ops.subtract(ops.constant(np.float32(1.0)), c2w))
am = ops.add(ops.multiply(c1w, ops.constant(np.float32(1.0))),
             ops.multiply(c2w, ops.constant(np.float32(2.0))))
am_sq = ops.squeeze(am, ops.constant(np.array([2], dtype=np.int32)))

# Slice first 100
begin_1 = ops.constant(np.array([0, 0], dtype=np.int32))
end_1 = ops.constant(np.array([1, 100], dtype=np.int32))
strides_1 = ops.constant(np.array([1, 1], dtype=np.int32))
sliced_1 = ops.strided_slice(am_sq, begin_1, end_1, strides_1,
                              begin_mask=[0, 0], end_mask=[0, 0])

res1.input(0).replace_source_output(sliced_1.output(0))
model_1.validate_nodes_and_infer_types()

# CPU
cpu_1 = core.compile_model(model_1, "CPU")
r_1c = cpu_1.create_infer_request()
r_1c.infer({0: img})
l_1c = r_1c.get_output_tensor(1).data.copy().flatten()
print(f"CPU: unique={np.unique(l_1c)}, dist: 0={np.sum(l_1c==0)}, 1={np.sum(l_1c==1)}, 2={np.sum(l_1c==2)}")

# NPU
npu_1 = core.compile_model(model_1, "NPU")
r_1n = npu_1.create_infer_request()
r_1n.infer({0: img})
l_1n = r_1n.get_output_tensor(1).data.copy().flatten()
print(f"NPU: unique={np.unique(l_1n)}, dist: 0={np.sum(l_1n==0)}, 1={np.sum(l_1n==1)}, 2={np.sum(l_1n==2)}")
print(f"NPU first 20: {l_1n[:20]}")

match_1 = np.sum(l_1c == l_1n)
print(f"CPU vs NPU match: {match_1}/{len(l_1c)} ({100*match_1/len(l_1c):.1f}%)")

# === Test 2: Pairwise argmax → original Gather with original indices ===
print("\n=== Test 2: Pairwise argmax + original Gather ===")
model_2 = core.read_model(MODEL)
model_2.get_parameters()[0].set_layout(ov.Layout("NCHW"))
ov.set_batch(model_2, 1)

# Find labels path pieces
for out in model_2.outputs:
    if "labels" in out.get_names():
        labels_out2 = out
        break
res2 = labels_out2.get_node()
n2 = res2.input(0).get_source_output().get_node()
while n2.get_type_name() != "Gather":
    n2 = n2.input(0).get_source_output().get_node()
gather2 = n2
n2 = gather2.input(0).get_source_output().get_node()
while n2.get_type_name() != "TopK":
    n2 = n2.input(0).get_source_output().get_node()
topk2 = n2

cs2 = topk2.input(0).get_source_output()
na2 = cs2.get_partial_shape()[1].get_length()

# Build pairwise argmax
s0_2 = slice_class(cs2, 0, na2)
s1_2 = slice_class(cs2, 1, na2)
s2_2 = slice_class(cs2, 2, na2)
c1b0_2 = ops.convert(ops.greater(s1_2, s0_2), ov.Type.f32)
c2b0_2 = ops.convert(ops.greater(s2_2, s0_2), ov.Type.f32)
c2b1_2 = ops.convert(ops.greater(s2_2, s1_2), ov.Type.f32)
c2w_2 = ops.multiply(c2b0_2, c2b1_2)
c1w_2 = ops.multiply(c1b0_2, ops.subtract(ops.constant(np.float32(1.0)), c2w_2))
am_2 = ops.add(ops.multiply(c1w_2, ops.constant(np.float32(1.0))),
               ops.multiply(c2w_2, ops.constant(np.float32(2.0))))
# am_2: [1, N, 1]

# Connect to the Squeeze that goes to Gather (replace TopK output 1)
for ti in topk2.output(1).get_target_inputs():
    tn = ti.get_node()
    if tn.get_type_name() == "Squeeze":
        ti.replace_source_output(am_2.output(0))
        print(f"Replaced Squeeze '{tn.get_friendly_name()}' input")
        break

model_2.validate_nodes_and_infer_types()
for out in model_2.outputs:
    print(f"  Output '{out.get_any_name()}': {out.get_partial_shape()}, type: {out.get_element_type()}")

# CPU
cpu_2 = core.compile_model(model_2, "CPU")
r_2c = cpu_2.create_infer_request()
r_2c.infer({0: img})
l_2c = r_2c.get_output_tensor(1).data.copy().flatten()
print(f"CPU: unique={np.unique(l_2c)}, dist: 0={np.sum(l_2c==0)}, 1={np.sum(l_2c==1)}, 2={np.sum(l_2c==2)}")

# NPU
npu_2 = core.compile_model(model_2, "NPU")
r_2n = npu_2.create_infer_request()
r_2n.infer({0: img})
l_2n = r_2n.get_output_tensor(1).data.copy().flatten()
print(f"NPU: unique={np.unique(l_2n)}, dist: 0={np.sum(l_2n==0)}, 1={np.sum(l_2n==1)}, 2={np.sum(l_2n==2)}")
print(f"NPU first 20: {l_2n[:20]}")

# === Test 3: Direct replacement - connect right to Result ===
print("\n=== Test 3: Complete bypass - new argmax + new Gather → Result ===")
model_3 = core.read_model(MODEL)
model_3.get_parameters()[0].set_layout(ov.Layout("NCHW"))
ov.set_batch(model_3, 1)

for out in model_3.outputs:
    if "labels" in out.get_names():
        labels_out3 = out
        break
res3 = labels_out3.get_node()
n3 = res3.input(0).get_source_output().get_node()
while n3.get_type_name() != "Gather":
    n3 = n3.input(0).get_source_output().get_node()
gather3 = n3
n3 = gather3.input(0).get_source_output().get_node()
while n3.get_type_name() != "TopK":
    n3 = n3.input(0).get_source_output().get_node()
topk3 = n3

cs3 = topk3.input(0).get_source_output()
na3 = cs3.get_partial_shape()[1].get_length()

# Build pairwise argmax [1, N, 1]
s0_3 = slice_class(cs3, 0, na3)
s1_3 = slice_class(cs3, 1, na3)
s2_3 = slice_class(cs3, 2, na3)
c1b0_3 = ops.convert(ops.greater(s1_3, s0_3), ov.Type.f32)
c2b0_3 = ops.convert(ops.greater(s2_3, s0_3), ov.Type.f32)
c2b1_3 = ops.convert(ops.greater(s2_3, s1_3), ov.Type.f32)
c2w_3 = ops.multiply(c2b0_3, c2b1_3)
c1w_3 = ops.multiply(c1b0_3, ops.subtract(ops.constant(np.float32(1.0)), c2w_3))
am_3 = ops.add(ops.multiply(c1w_3, ops.constant(np.float32(1.0))),
               ops.multiply(c2w_3, ops.constant(np.float32(2.0))))
am_3_sq = ops.squeeze(am_3, ops.constant(np.array([2], dtype=np.int32)))  # [1, N]

# Reshape to [N, 1] for Gather
am_3_r = ops.reshape(am_3_sq, ops.constant(np.array([na3, 1], dtype=np.int32)), special_zero=False)

# New Gather using original indices
orig_idx3 = gather3.input(1).get_source_output()
orig_ax3 = gather3.input(2).get_source_output()
new_g3 = ops.gather(am_3_r, orig_idx3, orig_ax3)

# Reshape to [1, 100]
new_labels3 = ops.reshape(new_g3, ops.constant(np.array([1, 100], dtype=np.int32)), special_zero=False)

# Connect directly to Result (bypass entire old path)
res3.input(0).replace_source_output(new_labels3.output(0))
model_3.validate_nodes_and_infer_types()

# CPU
cpu_3 = core.compile_model(model_3, "CPU")
r_3c = cpu_3.create_infer_request()
r_3c.infer({0: img})
l_3c = r_3c.get_output_tensor(1).data.copy().flatten()
print(f"CPU: unique={np.unique(l_3c)}, dist: 0={np.sum(l_3c==0)}, 1={np.sum(l_3c==1)}, 2={np.sum(l_3c==2)}")

# NPU
npu_3 = core.compile_model(model_3, "NPU")
r_3n = npu_3.create_infer_request()
r_3n.infer({0: img})
l_3n = r_3n.get_output_tensor(1).data.copy().flatten()
print(f"NPU: unique={np.unique(l_3n)}, dist: 0={np.sum(l_3n==0)}, 1={np.sum(l_3n==1)}, 2={np.sum(l_3n==2)}")
print(f"NPU first 20: {l_3n[:20]}")
