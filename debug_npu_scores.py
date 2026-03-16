#!/usr/bin/env python3
"""
Investigate: what feeds the Multiply node before the argmax TopK?
The classification scores [1,3549,3] come from Sigmoid * ???
If the second factor involves I64 operations, that explains NPU scores being wrong.
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

# Find Multiply before TopK and trace its inputs
labels_out = None
for out in model_cpu.outputs:
    if "labels" in out.get_names():
        labels_out = out
        break

n = labels_out.get_node().input(0).get_source_output().get_node()
while n.get_type_name() != "TopK":
    n = n.input(0).get_source_output().get_node()
topk = n

multiply = topk.input(0).get_source_output().get_node()
print(f"Multiply: {multiply.get_type_name()} '{multiply.get_friendly_name()}' shape={multiply.get_output_partial_shape(0)}")
print(f"  Number of inputs: {multiply.get_input_size()}")

for i in range(multiply.get_input_size()):
    src = multiply.input(i).get_source_output()
    print(f"\n  Input {i}: {src.get_node().get_type_name()} '{src.get_node().get_friendly_name()}'")
    print(f"    type={src.get_element_type()} shape={src.get_partial_shape()}")

# Trace input 1 (second factor) deeper
print("\n=== Tracing Multiply input 1 (second factor) ===")
def trace(node, depth=0, max_depth=20, visited=None):
    if visited is None:
        visited = set()
    nid = id(node)
    if nid in visited or depth > max_depth:
        return
    visited.add(nid)
    
    out_types = [str(node.get_output_element_type(i)) for i in range(node.get_output_size())]
    out_shapes = []
    for i in range(node.get_output_size()):
        try:
            out_shapes.append(str(node.get_output_partial_shape(i)))
        except:
            out_shapes.append("?")
    
    prefix = "  " * depth
    is_i64 = any("i64" in t for t in out_types)
    marker = " *** I64 ***" if is_i64 else ""
    print(f"{prefix}{node.get_type_name()} '{node.get_friendly_name()}' types={out_types} shapes={out_shapes}{marker}")
    
    for i in range(node.get_input_size()):
        src_node = node.input(i).get_source_output().get_node()
        trace(src_node, depth + 1, max_depth, visited)

input1_node = multiply.input(1).get_source_output().get_node()
trace(input1_node, depth=0)

# Also trace input 0 briefly
print("\n=== Multiply input 0 (Sigmoid) ===")
input0_node = multiply.input(0).get_source_output().get_node()
trace(input0_node, depth=0, max_depth=3)

# ==========================================
# Add intermediate outputs to compare CPU vs NPU for the Multiply inputs
# ==========================================
print("\n\n=== Comparing Multiply inputs: CPU vs NPU ===")
from openvino._pyopenvino import op as ov_op

model_cmp = core.read_model(MODEL)
model_cmp.get_parameters()[0].set_layout(ov.Layout("NCHW"))
ov.set_batch(model_cmp, 1)

# Find Multiply
for out in model_cmp.outputs:
    if "labels" in out.get_names():
        n = out.get_node().input(0).get_source_output().get_node()
        while n.get_type_name() != "TopK":
            n = n.input(0).get_source_output().get_node()
        mul_node = n.input(0).get_source_output().get_node()
        break

# Add Multiply input 0 (Sigmoid output) as new result
sig_out = mul_node.input(0).get_source_output()
sig_result = ov_op.Result(sig_out)
sig_result.set_friendly_name("debug_sigmoid")

# Add Multiply input 1 as new result
mul_in1 = mul_node.input(1).get_source_output()
mul_in1_result = ov_op.Result(mul_in1)
mul_in1_result.set_friendly_name("debug_mul_factor")

# Add Multiply output as new result
mul_out = mul_node.output(0)
mul_result = ov_op.Result(mul_out)
mul_result.set_friendly_name("debug_mul_output")

model_cmp.add_results([sig_result, mul_in1_result, mul_result])
model_cmp.validate_nodes_and_infer_types()

# List outputs
print("Outputs:")
for i, out in enumerate(model_cmp.outputs):
    try:
        name = out.get_any_name()
    except:
        name = f"result_{i}"
    print(f"  [{i}] {name}: {out.get_partial_shape()} {out.get_element_type()}")

# CPU
cpu_cmp = core.compile_model(model_cmp, "CPU")
req_c = cpu_cmp.create_infer_request()
req_c.infer({0: img})

# NPU
npu_cmp = core.compile_model(model_cmp, "NPU")
req_n = npu_cmp.create_infer_request()
req_n.infer({0: img})

# Compare each output
for i in range(len(model_cmp.outputs)):
    cpu_data = req_c.get_output_tensor(i).data.copy()
    npu_data = req_n.get_output_tensor(i).data.copy()
    try:
        name = model_cmp.outputs[i].get_any_name()
    except:
        name = f"result_{i}"
    
    if "debug" in name or "boxes" in name or "labels" in name:
        diff = np.abs(cpu_data - npu_data)
        cpu_flat = cpu_data.flatten()
        npu_flat = npu_data.flatten()
        print(f"\n  {name}: shape={cpu_data.shape}")
        print(f"    CPU: min={cpu_flat.min():.6f} max={cpu_flat.max():.6f} mean={cpu_flat.mean():.6f} nonzero={np.count_nonzero(cpu_flat)}")
        print(f"    NPU: min={npu_flat.min():.6f} max={npu_flat.max():.6f} mean={npu_flat.mean():.6f} nonzero={np.count_nonzero(npu_flat)}")
        print(f"    Diff: max={diff.max():.6f} mean={diff.mean():.6f}")
        if len(cpu_flat) <= 100:
            print(f"    CPU first 10: {cpu_flat[:10]}")
            print(f"    NPU first 10: {npu_flat[:10]}")
        else:
            # Show a sample from the middle
            mid = len(cpu_flat) // 2
            print(f"    CPU[0:5]: {cpu_flat[:5]}")
            print(f"    NPU[0:5]: {npu_flat[:5]}")
            print(f"    CPU[{mid}:{mid+5}]: {cpu_flat[mid:mid+5]}")
            print(f"    NPU[{mid}:{mid+5}]: {npu_flat[mid:mid+5]}")
