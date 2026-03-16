#!/usr/bin/env python3
"""
Targeted diagnostics: is the problem in Gather, its indices, or something else?
Tests:
E: FP32 argmax, skip Gather, take first 100 directly → isolates Gather
F: FP32 argmax, new Gather with hardcoded indices [0..99] → isolates index source
G: FP32 argmax, new Gather with original indices → standard fix
H: Direct from TopK output 0 scores (FP32, already works) through same Gather
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
print(f"CPU reference: unique={np.unique(cpu_labels)}, dist: 0={np.sum(cpu_labels==0)}, 1={np.sum(cpu_labels==1)}, 2={np.sum(cpu_labels==2)}")

def get_model_pieces(model):
    """Extract key model nodes for the labels path"""
    labels_out = None
    for out in model.outputs:
        if "labels" in out.get_names():
            labels_out = out
            break
    
    result = labels_out.get_node()
    convert = result.input(0).get_source_output().get_node()
    
    # Find Gather
    node = convert
    while node.get_type_name() != "Gather":
        node = node.input(0).get_source_output().get_node()
    gather = node
    
    # Find TopK
    node = gather.input(0).get_source_output().get_node()
    while node.get_type_name() != "TopK":
        node = node.input(0).get_source_output().get_node()
    topk = node
    
    return result, convert, gather, topk

def build_fp32_argmax(cls_scores_output, num_classes):
    """Build FP32 argmax from classification scores"""
    class_idx = ops.constant(np.arange(num_classes, dtype=np.float32).reshape(1, 1, num_classes))
    axis = ops.constant(np.array([2], dtype=np.int32))
    max_sc = ops.reduce_max(cls_scores_output, axis, keep_dims=True)
    eps = ops.constant(np.float32(1e-6))
    diff = ops.abs(ops.subtract(cls_scores_output, max_sc))
    mask_bool = ops.less(diff, eps)
    large_val = ops.constant(np.float32(999.0))
    weighted = ops.select(mask_bool, class_idx, large_val)
    return ops.reduce_min(weighted, axis, keep_dims=False)  # [1, N]


# ==========================================
# Test E: FP32 argmax, skip Gather, take first 100 elements
# ==========================================
print("\n=== Test E: FP32 argmax, slice first 100, no Gather ===")
model_e = core.read_model(MODEL)
model_e.get_parameters()[0].set_layout(ov.Layout("NCHW"))
ov.set_batch(model_e, 1)

result_e, convert_e, gather_e, topk_e = get_model_pieces(model_e)
cls_scores_e = topk_e.input(0).get_source_output()
nc = cls_scores_e.get_partial_shape()[2].get_length()

argmax_e = build_fp32_argmax(cls_scores_e, nc)  # [1, N]

# Slice first 100: [1, 100]
begin_e = ops.constant(np.array([0, 0], dtype=np.int32))
end_e = ops.constant(np.array([1, 100], dtype=np.int32))
strides_e = ops.constant(np.array([1, 1], dtype=np.int32))
sliced_e = ops.strided_slice(argmax_e, begin_e, end_e, strides_e,
                              begin_mask=[0,0], end_mask=[0,0])

result_e.input(0).replace_source_output(sliced_e.output(0))
model_e.validate_nodes_and_infer_types()

# CPU check
cpu_e = core.compile_model(model_e, "CPU")
req_e_c = cpu_e.create_infer_request()
req_e_c.infer({0: img})
cpu_e_labels = req_e_c.get_output_tensor(1).data.copy().flatten()
print(f"  CPU: unique={np.unique(cpu_e_labels)}, dist: 0={np.sum(cpu_e_labels==0)}, 1={np.sum(cpu_e_labels==1)}, 2={np.sum(cpu_e_labels==2)}")

# NPU
npu_e = core.compile_model(model_e, "NPU")
req_e_n = npu_e.create_infer_request()
req_e_n.infer({0: img})
npu_e_labels = req_e_n.get_output_tensor(1).data.copy().flatten()
print(f"  NPU: unique={np.unique(npu_e_labels)}, dist: 0={np.sum(npu_e_labels==0)}, 1={np.sum(npu_e_labels==1)}, 2={np.sum(npu_e_labels==2)}")
print(f"  NPU first 20: {npu_e_labels[:20]}")


# ==========================================
# Test F: FP32 argmax + Gather with hardcoded indices [0..99]
# ==========================================
print("\n=== Test F: FP32 argmax + Gather with indices [0..99] ===")
model_f = core.read_model(MODEL)
model_f.get_parameters()[0].set_layout(ov.Layout("NCHW"))
ov.set_batch(model_f, 1)

result_f, convert_f, gather_f, topk_f = get_model_pieces(model_f)
cls_scores_f = topk_f.input(0).get_source_output()
nc_f = cls_scores_f.get_partial_shape()[2].get_length()
na_f = cls_scores_f.get_partial_shape()[1].get_length()

argmax_f = build_fp32_argmax(cls_scores_f, nc_f)  # [1, N]

# Reshape to [N, 1]
reshape_f = ops.reshape(argmax_f, ops.constant(np.array([na_f, 1], dtype=np.int32)), special_zero=False)

# Create hardcoded indices [0, 1, 2, ..., 99] as I32
fixed_indices = ops.constant(np.arange(100, dtype=np.int32))
axis_0 = ops.constant(np.int32(0))

new_gather_f = ops.gather(reshape_f, fixed_indices, axis_0)  # [100, 1]
new_reshape_f = ops.reshape(new_gather_f, ops.constant(np.array([1, 100], dtype=np.int32)), special_zero=False)

result_f.input(0).replace_source_output(new_reshape_f.output(0))
model_f.validate_nodes_and_infer_types()

# CPU
cpu_f = core.compile_model(model_f, "CPU")
req_f_c = cpu_f.create_infer_request()
req_f_c.infer({0: img})
cpu_f_labels = req_f_c.get_output_tensor(1).data.copy().flatten()
print(f"  CPU: unique={np.unique(cpu_f_labels)}, dist: 0={np.sum(cpu_f_labels==0)}, 1={np.sum(cpu_f_labels==1)}, 2={np.sum(cpu_f_labels==2)}")

# NPU
npu_f = core.compile_model(model_f, "NPU")
req_f_n = npu_f.create_infer_request()
req_f_n.infer({0: img})
npu_f_labels = req_f_n.get_output_tensor(1).data.copy().flatten()
print(f"  NPU: unique={np.unique(npu_f_labels)}, dist: 0={np.sum(npu_f_labels==0)}, 1={np.sum(npu_f_labels==1)}, 2={np.sum(npu_f_labels==2)}")
print(f"  NPU first 20: {npu_f_labels[:20]}")


# ==========================================
# Test G: FP32 argmax + Gather with original indices 
# ==========================================
print("\n=== Test G: FP32 argmax + Gather with original top-100 indices ===")
model_g = core.read_model(MODEL)
model_g.get_parameters()[0].set_layout(ov.Layout("NCHW"))
ov.set_batch(model_g, 1)

result_g, convert_g, gather_g, topk_g = get_model_pieces(model_g)
cls_scores_g = topk_g.input(0).get_source_output()
nc_g = cls_scores_g.get_partial_shape()[2].get_length()
na_g = cls_scores_g.get_partial_shape()[1].get_length()

argmax_g = build_fp32_argmax(cls_scores_g, nc_g)

# Reshape to [N, 1]
reshape_g = ops.reshape(argmax_g, ops.constant(np.array([na_g, 1], dtype=np.int32)), special_zero=False)

# Get original Gather's indices and axis
orig_indices = gather_g.input(1).get_source_output()
orig_axis = gather_g.input(2).get_source_output()
print(f"  Original indices: {orig_indices.get_node().get_friendly_name()} type={orig_indices.get_element_type()} shape={orig_indices.get_partial_shape()}")
print(f"  Original axis: {orig_axis.get_node().get_friendly_name()}")

# Create new Gather with FP32 data + original indices
new_gather_g = ops.gather(reshape_g, orig_indices, orig_axis)
new_reshape_g = ops.reshape(new_gather_g, ops.constant(np.array([1, 100], dtype=np.int32)), special_zero=False)

result_g.input(0).replace_source_output(new_reshape_g.output(0))
model_g.validate_nodes_and_infer_types()

# CPU
cpu_g = core.compile_model(model_g, "CPU")
req_g_c = cpu_g.create_infer_request()
req_g_c.infer({0: img})
cpu_g_labels = req_g_c.get_output_tensor(1).data.copy().flatten()
print(f"  CPU: unique={np.unique(cpu_g_labels)}, dist: 0={np.sum(cpu_g_labels==0)}, 1={np.sum(cpu_g_labels==1)}, 2={np.sum(cpu_g_labels==2)}")

# NPU
npu_g = core.compile_model(model_g, "NPU")
req_g_n = npu_g.create_infer_request()
req_g_n.infer({0: img})
npu_g_labels = req_g_n.get_output_tensor(1).data.copy().flatten()
print(f"  NPU: unique={np.unique(npu_g_labels)}, dist: 0={np.sum(npu_g_labels==0)}, 1={np.sum(npu_g_labels==1)}, 2={np.sum(npu_g_labels==2)}")
print(f"  NPU first 20: {npu_g_labels[:20]}")


# ==========================================
# Test H: Compare Test E NPU vs CPU (should both have same first 100 argmax)
# ==========================================
print("\n=== Summary ===")
print(f"Test E (no Gather, first 100): CPU dist 0={np.sum(cpu_e_labels==0)},1={np.sum(cpu_e_labels==1)},2={np.sum(cpu_e_labels==2)} | NPU dist 0={np.sum(npu_e_labels==0)},1={np.sum(npu_e_labels==1)},2={np.sum(npu_e_labels==2)}")
print(f"Test F (Gather, fixed idx): CPU dist 0={np.sum(cpu_f_labels==0)},1={np.sum(cpu_f_labels==1)},2={np.sum(cpu_f_labels==2)} | NPU dist 0={np.sum(npu_f_labels==0)},1={np.sum(npu_f_labels==1)},2={np.sum(npu_f_labels==2)}")
print(f"Test G (Gather, orig idx):  CPU dist 0={np.sum(cpu_g_labels==0)},1={np.sum(cpu_g_labels==1)},2={np.sum(cpu_g_labels==2)} | NPU dist 0={np.sum(npu_g_labels==0)},1={np.sum(npu_g_labels==1)},2={np.sum(npu_g_labels==2)}")
