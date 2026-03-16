#!/usr/bin/env python3
"""
Test additional strategies to fix NPU labels:
- Strategy 5: Change TopK index_element_type to I32
- Strategy 6: Constant folding + Convert I64→FP32
- Strategy 7: Force TopK to use FP32 for indices (creative workaround)
- Strategy 8: Compare boxes between CPU and NPU
"""
import numpy as np
import openvino as ov
import openvino.opset13 as ops
from openvino._pyopenvino.passes import Manager as PassManager
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

# NPU baseline
model_npu = core.read_model(MODEL)
model_npu.get_parameters()[0].set_layout(ov.Layout("NCHW"))
ov.set_batch(model_npu, 1)
npu0 = core.compile_model(model_npu, "NPU")
req0 = npu0.create_infer_request()
req0.infer({0: img})
npu_labels0 = req0.get_output_tensor(1).data.copy().flatten()
npu_boxes0 = req0.get_output_tensor(0).data.copy().reshape(-1, 5)

# ==========================================
# Strategy 8: Compare boxes between CPU and NPU
# ==========================================
print("\n=== Strategy 8: Compare boxes CPU vs NPU baseline ===")
box_diff = np.abs(cpu_boxes - npu_boxes0)
print(f"Box max diff: {box_diff.max():.6f}")
print(f"Box mean diff: {box_diff.mean():.6f}")
print(f"CPU boxes first 5:\n{cpu_boxes[:5]}")
print(f"NPU boxes first 5:\n{npu_boxes0[:5]}")
# Check if same boxes are selected (by comparing first box coords)
box_match = np.sum(np.all(np.abs(cpu_boxes - npu_boxes0) < 1.0, axis=1))
print(f"Boxes matching (within 1.0): {box_match}/100")

# ==========================================
# Strategy 5: Change TopK to use I32 index type
# ==========================================
print("\n\n=== Strategy 5: Replace TopK nodes with I32 index type ===")
model_fix5 = core.read_model(MODEL)
model_fix5.get_parameters()[0].set_layout(ov.Layout("NCHW"))
ov.set_batch(model_fix5, 1)

topk_count = 0
for op in list(model_fix5.get_ordered_ops()):
    if op.get_type_name() == "TopK":
        name = op.get_friendly_name()
        # TopK has: input 0 = data, input 1 = k
        data_in = op.input(0).get_source_output()
        k_in = op.input(1).get_source_output()
        
        # Get TopK attributes
        attrs = {}
        # Get axis, mode, sort from the node
        # TopK in opset1/11 has axis, mode, sort_type attributes
        out0_type = op.get_output_element_type(0)
        out1_type = op.get_output_element_type(1)
        out0_shape = op.get_output_partial_shape(0)
        out1_shape = op.get_output_partial_shape(1)
        
        print(f"  TopK: '{name}' data_shape={data_in.get_partial_shape()}")
        print(f"    out0: type={out0_type} shape={out0_shape}")
        print(f"    out1: type={out1_type} shape={out1_shape}")
        
        if out1_type == ov.Type.i64:
            # Insert Convert(I64→I32) after output 1 
            consumers_out1 = list(op.output(1).get_target_inputs())
            cvt = ops.convert(op.output(1), ov.Type.i32)
            for consumer in consumers_out1:
                consumer.replace_source_output(cvt.output(0))
            topk_count += 1
            print(f"    -> Inserted Convert(I64→I32) after output 1")

print(f"TopK nodes with I64→I32 conversion: {topk_count}")

try:
    model_fix5.validate_nodes_and_infer_types()
    for out in model_fix5.outputs:
        print(f"  Output '{out.get_any_name()}': {out.get_partial_shape()}, type: {out.get_element_type()}")

    npu_fix5 = core.compile_model(model_fix5, "NPU")
    req_fix5 = npu_fix5.create_infer_request()
    req_fix5.infer({0: img})
    fix5_labels = req_fix5.get_output_tensor(1).data.copy().flatten()
    print(f"\nNPU (fix5-topk-i32) labels unique: {np.unique(fix5_labels)}")
    print(f"NPU (fix5-topk-i32) dist: 0={np.sum(fix5_labels==0)}, 1={np.sum(fix5_labels==1)}, 2={np.sum(fix5_labels==2)}")
    print(f"NPU (fix5-topk-i32) first 20: {fix5_labels[:20]}")
    match5 = np.sum(cpu_labels == fix5_labels)
    print(f"Match with CPU: {match5}/{len(cpu_labels)} ({100*match5/len(cpu_labels):.1f}%)")
except Exception as e:
    print(f"Strategy 5 failed: {e}")

# ==========================================
# Strategy 6: Constant folding + Convert I64→FP32 on labels path
# ==========================================
print("\n\n=== Strategy 6: Constant folding + Convert at Gather data ===")
model_fix6 = core.read_model(MODEL)
model_fix6.get_parameters()[0].set_layout(ov.Layout("NCHW"))
ov.set_batch(model_fix6, 1)

# Apply constant folding first
pass_mgr = PassManager()
pass_mgr.register_pass("ConstantFolding")
pass_mgr.run_passes(model_fix6)

# Now find the labels path and convert I64 data input
labels_out6 = None
for out in model_fix6.outputs:
    if "labels" in out.get_names():
        labels_out6 = out
        break

result6 = labels_out6.get_node()
# Trace back to Gather
node = result6.input(0).get_source_output().get_node()  # Convert or other
while node.get_type_name() not in ("Gather", "Parameter"):
    print(f"  Tracing: {node.get_type_name()} '{node.get_friendly_name()}' type={node.get_output_element_type(0)}")
    node = node.input(0).get_source_output().get_node()

if node.get_type_name() == "Gather":
    print(f"  Found Gather: '{node.get_friendly_name()}'")
    data_src = node.input(0).get_source_output()
    print(f"    Data: {data_src.get_node().get_friendly_name()} type={data_src.get_element_type()}")
    
    if data_src.get_element_type() in (ov.Type.i64, ov.Type.i32):
        cvt = ops.convert(data_src, ov.Type.f32)
        node.input(0).replace_source_output(cvt.output(0))
        print(f"    -> Converted data input to FP32")

try:
    model_fix6.validate_nodes_and_infer_types()
    for out in model_fix6.outputs:
        print(f"  Output '{out.get_any_name()}': {out.get_partial_shape()}, type: {out.get_element_type()}")

    npu_fix6 = core.compile_model(model_fix6, "NPU")
    req_fix6 = npu_fix6.create_infer_request()
    req_fix6.infer({0: img})
    fix6_labels = req_fix6.get_output_tensor(1).data.copy().flatten()
    print(f"\nNPU (fix6-cf+convert) labels unique: {np.unique(fix6_labels)}")
    print(f"NPU (fix6-cf+convert) dist: 0={np.sum(fix6_labels==0)}, 1={np.sum(fix6_labels==1)}, 2={np.sum(fix6_labels==2)}")
    print(f"NPU (fix6-cf+convert) first 20: {fix6_labels[:20]}")
    match6 = np.sum(cpu_labels == fix6_labels)
    print(f"Match with CPU: {match6}/{len(cpu_labels)} ({100*match6/len(cpu_labels):.1f}%)")
except Exception as e:
    print(f"Strategy 6 failed: {e}")

# ==========================================
# Strategy 7: Replace entire labels I64 subgraph with FP32 argmax equivalent
# Find classification scores, compute argmax as FP32, then Gather
# ==========================================
print("\n\n=== Strategy 7: Replace TopK argmax with FP32 argmax ===")
model_fix7 = core.read_model(MODEL)
model_fix7.get_parameters()[0].set_layout(ov.Layout("NCHW"))
ov.set_batch(model_fix7, 1)

# Find the argmax TopK (aten::max/TopK) and its input
labels_out7 = None
for out in model_fix7.outputs:
    if "labels" in out.get_names():
        labels_out7 = out
        break

result7 = labels_out7.get_node()
convert7 = result7.input(0).get_source_output().get_node()
reshape7 = convert7.input(0).get_source_output().get_node()

# Find Gather
gather7 = reshape7
while gather7.get_type_name() != "Gather":
    gather7 = gather7.input(0).get_source_output().get_node()

# Gather data → Reshape → Squeeze → TopK
data_reshape7 = gather7.input(0).get_source_output().get_node()
squeeze7 = data_reshape7.input(0).get_source_output().get_node()
topk_src = squeeze7.input(0).get_source_output()
topk7 = topk_src.get_node()

print(f"  TopK: '{topk7.get_friendly_name()}'")
print(f"    Input: {topk7.input(0).get_source_output().get_node().get_friendly_name()} shape={topk7.input(0).get_source_output().get_partial_shape()}")
print(f"    Output 0 (values): type={topk7.get_output_element_type(0)} shape={topk7.get_output_partial_shape(0)}")
print(f"    Output 1 (indices): type={topk7.get_output_element_type(1)} shape={topk7.get_output_partial_shape(1)}")

# Get the classification scores input to TopK
cls_scores = topk7.input(0).get_source_output()
print(f"  Classification scores: shape={cls_scores.get_partial_shape()} type={cls_scores.get_element_type()}")

# Create FP32 argmax alternative:
# scores: [1, 3549, 3] → we need argmax along axis 2
# argmax as FP32: use weighted sum trick
# class_weights = [0.0, 1.0, 2.0]
# softamx_like = one_hot where max → multiply by weights → reduce_sum
# Actually simpler: use TopK output 1 with Convert(I64→FP32) right at TopK,
# but make it so the entire I64 chain is replaced

# Alternative approach: instead of TopK, compute argmax manually in FP32
# argmax = reduce_max -> compare -> multiply by indices -> sum
num_classes = 3  # from the model

# Create class index tensor [0, 1, 2] as FP32
class_indices = ops.constant(np.array([0.0, 1.0, 2.0], dtype=np.float32).reshape(1, 1, 3))

# Get max value per anchor: ReduceMax(scores, axis=2, keepdims=True)
axis_const = ops.constant(np.array([2], dtype=np.int32))
max_scores = ops.reduce_max(cls_scores, axis_const, keep_dims=True)

# Create mask where score == max_score (handles ties by taking first)
# Use approximate equal: abs(scores - max) < epsilon
epsilon = ops.constant(np.float32(1e-6))
diff = ops.abs(ops.subtract(cls_scores, max_scores))
mask = ops.less(diff, epsilon)  # boolean mask
mask_f32 = ops.convert(mask, ov.Type.f32)

# Multiply mask by class indices and sum → gives argmax as float
weighted = ops.multiply(mask_f32, class_indices)
argmax_f32 = ops.reduce_max(weighted, axis_const, keep_dims=False)  # [1, 3549]
# ReduceMax instead of sum to handle ties: if multiple classes match, take highest index
# Actually we want the FIRST match. Let's use ReduceSum which works if only one class matches

print(f"  argmax_f32 shape should be [1, 3549]")

# Now create Reshape to match what comes after: [3549, 1]
new_shape = ops.constant(np.array([3549, 1], dtype=np.int32))
argmax_reshaped = ops.reshape(argmax_f32, new_shape, special_zero=False)

# Replace the Gather's data input with our FP32 argmax
gather7.input(0).replace_source_output(argmax_reshaped.output(0))
print(f"  -> Replaced Gather data with FP32 argmax computation")

try:
    model_fix7.validate_nodes_and_infer_types()
    for out in model_fix7.outputs:
        print(f"  Output '{out.get_any_name()}': {out.get_partial_shape()}, type: {out.get_element_type()}")

    # First test on CPU to verify correctness
    cpu_fix7 = core.compile_model(model_fix7, "CPU")
    req_cpu7 = cpu_fix7.create_infer_request()
    req_cpu7.infer({0: img})
    fix7_cpu_labels = req_cpu7.get_output_tensor(1).data.copy().flatten()
    print(f"\nCPU (fix7) labels: unique={np.unique(fix7_cpu_labels)}")
    print(f"CPU (fix7) dist: 0={np.sum(fix7_cpu_labels==0)}, 1={np.sum(fix7_cpu_labels==1)}, 2={np.sum(fix7_cpu_labels==2)}")
    match7_cpu = np.sum(cpu_labels == fix7_cpu_labels)
    print(f"CPU fix7 vs CPU original: {match7_cpu}/{len(cpu_labels)} ({100*match7_cpu/len(cpu_labels):.1f}%)")
    
    # Then test on NPU
    npu_fix7 = core.compile_model(model_fix7, "NPU")
    req_fix7 = npu_fix7.create_infer_request()
    req_fix7.infer({0: img})
    fix7_labels = req_fix7.get_output_tensor(1).data.copy().flatten()
    print(f"\nNPU (fix7-fp32-argmax) labels unique: {np.unique(fix7_labels)}")
    print(f"NPU (fix7-fp32-argmax) dist: 0={np.sum(fix7_labels==0)}, 1={np.sum(fix7_labels==1)}, 2={np.sum(fix7_labels==2)}")
    print(f"NPU (fix7-fp32-argmax) first 20: {fix7_labels[:20]}")
    match7 = np.sum(cpu_labels == fix7_labels)
    print(f"Match with CPU original: {match7}/{len(cpu_labels)} ({100*match7/len(cpu_labels):.1f}%)")
except Exception as e:
    print(f"Strategy 7 failed: {e}")
    import traceback
    traceback.print_exc()

# ==========================================
# Strategy 9: Use NPU with optimization disabled
# ==========================================
print("\n\n=== Strategy 9: NPU with minimal optimization ===")
model_fix9 = core.read_model(MODEL)
model_fix9.get_parameters()[0].set_layout(ov.Layout("NCHW"))
ov.set_batch(model_fix9, 1)

try:
    # Try various NPU configs
    configs_to_try = [
        {"NPU_COMPILATION_MODE_PARAMS": "optimization-level=0"},
        {"PERFORMANCE_HINT": "LATENCY"},
    ]
    for cfg in configs_to_try:
        try:
            print(f"  Trying config: {cfg}")
            npu9 = core.compile_model(model_fix9, "NPU", cfg)
            req9 = npu9.create_infer_request()
            req9.infer({0: img})
            fix9_labels = req9.get_output_tensor(1).data.copy().flatten()
            print(f"    labels unique: {np.unique(fix9_labels)}")
            print(f"    dist: 0={np.sum(fix9_labels==0)}, 1={np.sum(fix9_labels==1)}, 2={np.sum(fix9_labels==2)}")
            match9 = np.sum(cpu_labels == fix9_labels)
            print(f"    Match with CPU: {match9}/{len(cpu_labels)} ({100*match9/len(cpu_labels):.1f}%)")
        except Exception as e:
            print(f"    Config failed: {e}")
except Exception as e:
    print(f"Strategy 9 failed: {e}")
