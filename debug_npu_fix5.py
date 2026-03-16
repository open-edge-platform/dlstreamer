#!/usr/bin/env python3
"""
Fix NPU labels: Strategy 7 (FP32 argmax replacement) is the most promising.
Also properly validate by checking label variety, not 1:1 match with CPU.
"""
import numpy as np
import openvino as ov
import openvino.opset13 as ops
from openvino._pyopenvino.passes import Manager as PassManager, ConstantFolding
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
cpu_boxes = cpu_req.get_output_tensor(0).data.copy().reshape(-1, 5)
print(f"CPU labels: unique={np.unique(cpu_labels)}, dist: 0={np.sum(cpu_labels==0)}, 1={np.sum(cpu_labels==1)}, 2={np.sum(cpu_labels==2)}")
print(f"CPU boxes first 3:\n{cpu_boxes[:3]}")

# NPU baseline
model_npu0 = core.read_model(MODEL)
model_npu0.get_parameters()[0].set_layout(ov.Layout("NCHW"))
ov.set_batch(model_npu0, 1)
npu0 = core.compile_model(model_npu0, "NPU")
req0 = npu0.create_infer_request()
req0.infer({0: img})
npu_labels0 = req0.get_output_tensor(1).data.copy().flatten()
npu_boxes0 = req0.get_output_tensor(0).data.copy().reshape(-1, 5)
print(f"\nNPU baseline labels: unique={np.unique(npu_labels0)}, dist: 0={np.sum(npu_labels0==0)}, 1={np.sum(npu_labels0==1)}, 2={np.sum(npu_labels0==2)}")
print(f"NPU baseline boxes first 3:\n{npu_boxes0[:3]}")

# ==========================================
# Strategy 6: Constant folding + Convert I64→FP32 
# ==========================================
print("\n\n=== Strategy 6: Constant folding + Convert I64→FP32 on labels path ===")
model_fix6 = core.read_model(MODEL)
model_fix6.get_parameters()[0].set_layout(ov.Layout("NCHW"))
ov.set_batch(model_fix6, 1)

# Apply constant folding
pass_mgr = PassManager()
pass_mgr.register_pass(ConstantFolding())
pass_mgr.run_passes(model_fix6)

# Find labels path and convert I64 data inputs
labels_out6 = None
for out in model_fix6.outputs:
    if "labels" in out.get_names():
        labels_out6 = out
        break

# Trace from Result backward, convert all I64 outputs to FP32
def convert_i64_subgraph_to_fp32(result_output, model):
    """Walk backward from result, find all I64 nodes, insert Convert(I64→FP32)"""
    result_node = result_output.get_node()
    
    # Walk for labels path specifically
    node = result_node.input(0).get_source_output().get_node()
    
    # Find Gather by walking through Convert/Reshape
    while node.get_type_name() not in ("Gather", "Parameter"):
        node = node.input(0).get_source_output().get_node()
    
    if node.get_type_name() == "Gather":
        gather = node
        # Convert Gather data input (input 0)
        data_src = gather.input(0).get_source_output()
        if data_src.get_element_type() in (ov.Type.i64, ov.Type.i32):
            cvt = ops.convert(data_src, ov.Type.f32)
            gather.input(0).replace_source_output(cvt.output(0))
            print(f"  Converted Gather data: {data_src.get_element_type()} → FP32")
        
        # Also check: trace further to TopK and convert there too
        # Gather data → Reshape → Squeeze → TopK
        data_node = data_src.get_node()
        while data_node.get_type_name() not in ("TopK", "Parameter", "Constant"):
            for i in range(data_node.get_input_size()):
                inp = data_node.input(i).get_source_output()
                if inp.get_element_type() in (ov.Type.i64,):
                    cvt2 = ops.convert(inp, ov.Type.f32)
                    data_node.input(i).replace_source_output(cvt2.output(0))
                    print(f"  Also converted: {data_node.get_friendly_name()} input {i}")
            data_node = data_node.input(0).get_source_output().get_node()
        
        if data_node.get_type_name() == "TopK":
            print(f"  Found TopK: {data_node.get_friendly_name()}")
            # Convert TopK output 1 from I64→FP32 for all consumers  
            # But we already converted downstream, so this should be handled
    
    return True

convert_i64_subgraph_to_fp32(labels_out6, model_fix6)

try:
    model_fix6.validate_nodes_and_infer_types()
    for out in model_fix6.outputs:
        print(f"  Output '{out.get_any_name()}': {out.get_partial_shape()}, type: {out.get_element_type()}")

    npu_fix6 = core.compile_model(model_fix6, "NPU")
    req_fix6 = npu_fix6.create_infer_request()
    req_fix6.infer({0: img})
    fix6_labels = req_fix6.get_output_tensor(1).data.copy().flatten()
    fix6_boxes = req_fix6.get_output_tensor(0).data.copy().reshape(-1, 5)
    print(f"\nNPU (fix6) labels unique: {np.unique(fix6_labels)}")
    print(f"NPU (fix6) dist: 0={np.sum(fix6_labels==0)}, 1={np.sum(fix6_labels==1)}, 2={np.sum(fix6_labels==2)}")
    print(f"NPU (fix6) first 20: {fix6_labels[:20]}")
    
    # Compare boxes with NPU baseline
    box_diff6 = np.abs(fix6_boxes - npu_boxes0)
    print(f"NPU fix6 vs baseline boxes max diff: {box_diff6.max():.4f}")
except Exception as e:
    print(f"Strategy 6 failed: {e}")
    import traceback
    traceback.print_exc()

# ==========================================
# Strategy 7: Replace entire labels I64 subgraph with FP32 argmax
# ==========================================
print("\n\n=== Strategy 7: Replace I64 argmax with pure FP32 argmax ===")
model_fix7 = core.read_model(MODEL)
model_fix7.get_parameters()[0].set_layout(ov.Layout("NCHW"))
ov.set_batch(model_fix7, 1)

# Find labels path
labels_out7 = None
for out in model_fix7.outputs:
    if "labels" in out.get_names():
        labels_out7 = out
        break

result7 = labels_out7.get_node()
convert7 = result7.input(0).get_source_output().get_node()
print(f"Convert: {convert7.get_type_name()} '{convert7.get_friendly_name()}'")

# Find Gather
node7 = convert7
while node7.get_type_name() != "Gather":
    node7 = node7.input(0).get_source_output().get_node()
gather7 = node7

# Trace to TopK
data_reshape7 = gather7.input(0).get_source_output().get_node()
node7 = data_reshape7
while node7.get_type_name() != "TopK":
    node7 = node7.input(0).get_source_output().get_node()
topk7 = node7

print(f"TopK: '{topk7.get_friendly_name()}'")
cls_scores = topk7.input(0).get_source_output()
print(f"  Classification scores: shape={cls_scores.get_partial_shape()} type={cls_scores.get_element_type()}")

# Get shape info
scores_shape = cls_scores.get_partial_shape()
# Should be [1, 3549, 3]
num_anchors = scores_shape[1].get_length()
num_classes = scores_shape[2].get_length()
print(f"  num_anchors={num_anchors}, num_classes={num_classes}")

# Create FP32 argmax: 
# scores [1, N, C] → argmax per anchor
# Method: class_indices * one_hot(argmax) 
# Simpler: ReduceMax to find max, compare, multiply by [0,1,2], sum

# Create class index tensor [0, 1, 2] as FP32, shape [1, 1, C]
class_idx_arr = np.arange(num_classes, dtype=np.float32).reshape(1, 1, num_classes)
class_indices = ops.constant(class_idx_arr)

# ReduceMax along axis=2, keepdims=True
axis_const = ops.constant(np.array([2], dtype=np.int32))
max_scores = ops.reduce_max(cls_scores, axis_const, keep_dims=True)

# Mask where score == max_score
epsilon = ops.constant(np.float32(1e-6))
diff = ops.abs(ops.subtract(cls_scores, max_scores))
mask = ops.convert(ops.less(diff, epsilon), ov.Type.f32)  # [1, N, C] float mask

# Multiply mask by class indices → [1, N, C] with nonzero only at argmax position
weighted = ops.multiply(mask, class_indices)  # [1, N, C]

# ReduceMax to get argmax value (handles ties by taking highest class index)
argmax_f32 = ops.reduce_max(weighted, axis_const, keep_dims=True)  # [1, N, 1]

print(f"  FP32 argmax output shape: [1, {num_anchors}, 1]")

# Now: the original path was:
# TopK output 1 [1,N,1] → Squeeze [1,N] → Reshape [N,1] → Gather → Reshape → Convert → Result
#
# We need to connect our argmax_f32 [1,N,1] to replace TopK output 1
# The Squeeze takes TopK output 1 as input. We replace Squeeze's input with our FP32 argmax.

# Find the Squeeze connected to TopK output 1
squeeze7 = None
for target_input in topk7.output(1).get_target_inputs():
    target_node = target_input.get_node()
    if target_node.get_type_name() == "Squeeze":
        squeeze7 = target_node
        break

if squeeze7:
    print(f"  Found Squeeze: {squeeze7.get_friendly_name()}")
    squeeze7.input(0).replace_source_output(argmax_f32.output(0))
    print(f"  -> Replaced Squeeze input with FP32 argmax")
else:
    print(f"  WARNING: Squeeze not found, trying direct replacement")
    # Try replacing Gather data input directly
    new_shape = ops.constant(np.array([num_anchors, 1], dtype=np.int32))
    argmax_squeezed = ops.squeeze(argmax_f32, ops.constant(np.array([0], dtype=np.int32)))
    argmax_reshaped = ops.reshape(argmax_squeezed, new_shape, special_zero=False)
    gather7.input(0).replace_source_output(argmax_reshaped.output(0))

try:
    model_fix7.validate_nodes_and_infer_types()
    for out in model_fix7.outputs:
        print(f"  Output '{out.get_any_name()}': {out.get_partial_shape()}, type: {out.get_element_type()}")

    # Test on CPU first
    cpu_fix7 = core.compile_model(model_fix7, "CPU")
    req_cpu7 = cpu_fix7.create_infer_request()
    req_cpu7.infer({0: img})
    fix7_cpu_labels = req_cpu7.get_output_tensor(1).data.copy().flatten()
    print(f"\nCPU (fix7) labels: unique={np.unique(fix7_cpu_labels)}")
    print(f"CPU (fix7) dist: 0={np.sum(fix7_cpu_labels==0)}, 1={np.sum(fix7_cpu_labels==1)}, 2={np.sum(fix7_cpu_labels==2)}")
    print(f"CPU (fix7) first 20: {fix7_cpu_labels[:20]}")
    
    # Validate fix7 on CPU matches original CPU
    match7_cpu = np.sum(cpu_labels == fix7_cpu_labels)
    print(f"CPU fix7 vs CPU original match: {match7_cpu}/{len(cpu_labels)} ({100*match7_cpu/len(cpu_labels):.1f}%)")

    # Test on NPU  
    npu_fix7 = core.compile_model(model_fix7, "NPU")
    req_fix7 = npu_fix7.create_infer_request()
    req_fix7.infer({0: img})
    fix7_labels = req_fix7.get_output_tensor(1).data.copy().flatten()
    fix7_boxes = req_fix7.get_output_tensor(0).data.copy().reshape(-1, 5)
    print(f"\nNPU (fix7-fp32-argmax) labels unique: {np.unique(fix7_labels)}")
    print(f"NPU (fix7-fp32-argmax) dist: 0={np.sum(fix7_labels==0)}, 1={np.sum(fix7_labels==1)}, 2={np.sum(fix7_labels==2)}")
    print(f"NPU (fix7-fp32-argmax) first 20: {fix7_labels[:20]}")
    
    # Compare boxes with NPU baseline (should be identical since we only changed labels path)
    box_diff7 = np.abs(fix7_boxes - npu_boxes0)
    print(f"NPU fix7 vs baseline boxes max diff: {box_diff7.max():.4f}")
    
except Exception as e:
    print(f"Strategy 7 failed: {e}")
    import traceback
    traceback.print_exc()

# ==========================================
# Strategy 10: Multi-frame validation
# ==========================================
print("\n\n=== Strategy 10: Multi-frame validation of best fix ===")
# Test the best working strategy across multiple frames

def apply_best_fix(model_path):
    """Apply the FP32 argmax replacement"""
    model = core.read_model(model_path)
    model.get_parameters()[0].set_layout(ov.Layout("NCHW"))
    ov.set_batch(model, 1)
    
    # Find labels path
    labels_out = None
    for out in model.outputs:
        if "labels" in out.get_names():
            labels_out = out
            break
    if not labels_out:
        return model
    
    result_n = labels_out.get_node()
    convert_n = result_n.input(0).get_source_output().get_node()
    
    # Find Gather
    node = convert_n
    while node.get_type_name() != "Gather":
        node = node.input(0).get_source_output().get_node()
    gather = node
    
    # Find TopK (argmax)
    node = gather.input(0).get_source_output().get_node()
    while node.get_type_name() != "TopK":
        node = node.input(0).get_source_output().get_node()
    topk = node
    
    cls_scores = topk.input(0).get_source_output()
    scores_shape = cls_scores.get_partial_shape()
    num_classes = scores_shape[2].get_length()
    
    # FP32 argmax
    class_idx_arr = np.arange(num_classes, dtype=np.float32).reshape(1, 1, num_classes)
    class_indices = ops.constant(class_idx_arr)
    axis_const = ops.constant(np.array([2], dtype=np.int32))
    max_scores = ops.reduce_max(cls_scores, axis_const, keep_dims=True)
    epsilon = ops.constant(np.float32(1e-6))
    diff = ops.abs(ops.subtract(cls_scores, max_scores))
    mask = ops.convert(ops.less(diff, epsilon), ov.Type.f32)
    weighted = ops.multiply(mask, class_indices)
    argmax_f32 = ops.reduce_max(weighted, axis_const, keep_dims=True)
    
    # Replace Squeeze input
    for target_input in topk.output(1).get_target_inputs():
        target_node = target_input.get_node()
        if target_node.get_type_name() == "Squeeze":
            target_input.replace_source_output(argmax_f32.output(0))
            break
    
    model.validate_nodes_and_infer_types()
    return model

try:
    cap = cv2.VideoCapture(VIDEO)
    model_multi_cpu = core.read_model(MODEL)
    model_multi_cpu.get_parameters()[0].set_layout(ov.Layout("NCHW"))
    ov.set_batch(model_multi_cpu, 1)
    multi_cpu = core.compile_model(model_multi_cpu, "CPU")
    
    model_multi_npu = apply_best_fix(MODEL)
    multi_npu = core.compile_model(model_multi_npu, "NPU")
    
    frames_tested = 0
    total_cpu_labels = []
    total_npu_labels = []
    
    for frame_idx in range(0, 200, 20):
        cap.set(cv2.CAP_PROP_POS_FRAMES, frame_idx)
        ret, frame = cap.read()
        if not ret:
            break
        
        img = cv2.resize(frame, (w, h)).transpose(2, 0, 1)[np.newaxis].astype(np.float32)
        
        req_c = multi_cpu.create_infer_request()
        req_c.infer({0: img})
        c_labels = req_c.get_output_tensor(1).data.copy().flatten()
        
        req_n = multi_npu.create_infer_request()
        req_n.infer({0: img})
        n_labels = req_n.get_output_tensor(1).data.copy().flatten()
        
        total_cpu_labels.extend(c_labels.tolist())
        total_npu_labels.extend(n_labels.tolist())
        frames_tested += 1
    
    cap.release()
    
    total_cpu_labels = np.array(total_cpu_labels)
    total_npu_labels = np.array(total_npu_labels)
    
    print(f"Tested {frames_tested} frames")
    print(f"CPU total: 0={np.sum(total_cpu_labels==0)}, 1={np.sum(total_cpu_labels==1)}, 2={np.sum(total_cpu_labels==2)}")
    print(f"NPU total: 0={np.sum(total_npu_labels==0)}, 1={np.sum(total_npu_labels==1)}, 2={np.sum(total_npu_labels==2)}")
    
except Exception as e:
    print(f"Strategy 10 failed: {e}")
    import traceback
    traceback.print_exc()
