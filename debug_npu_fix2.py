#!/usr/bin/env python3
"""
Targeted fix: convert ONLY the labels-path argmax data to FP32
before the Gather, while keeping other Gathers untouched.
Also inspect the full labels subgraph.
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
cpu_boxes = cpu_req.get_output_tensor(0).data.copy()
print(f"CPU labels: unique={np.unique(cpu_labels)}, dist: 0={np.sum(cpu_labels==0)}, 1={np.sum(cpu_labels==1)}, 2={np.sum(cpu_labels==2)}")

# ---- Analyze labels subgraph ----
print("\n=== Analyzing labels subgraph ===")
model_a = core.read_model(MODEL)
model_a.get_parameters()[0].set_layout(ov.Layout("NCHW"))
ov.set_batch(model_a, 1)

# Find labels Result node and trace backward
labels_result = None
for out in model_a.outputs:
    if "labels" in out.get_names():
        labels_result = out.get_node()
        break

if labels_result:
    print(f"Labels Result: {labels_result.get_friendly_name()}")
    # Trace backward
    def trace_backward(node, depth=0, visited=None):
        if visited is None:
            visited = set()
        node_id = id(node)
        if node_id in visited:
            print(f"{'  ' * depth}[already visited] {node.get_friendly_name()}")
            return
        visited.add(node_id)
        
        out_types = [node.get_output_element_type(i) for i in range(node.get_output_size())]
        out_shapes = []
        for i in range(node.get_output_size()):
            try:
                out_shapes.append(str(node.get_output_partial_shape(i)))
            except:
                out_shapes.append("?")
        print(f"{'  ' * depth}{node.get_type_name()} '{node.get_friendly_name()}' out_types={out_types} out_shapes={out_shapes}")
        
        for i in range(node.get_input_size()):
            src = node.input(i).get_source_output().get_node()
            trace_backward(src, depth + 1, visited)
    
    trace_backward(labels_result, depth=0)

# ---- Strategy: trace from labels Result, find the argmax Gather,
# and convert ONLY its I64 data input to FP32 ----
print("\n\n=== Applying targeted fix ===")
model_fix = core.read_model(MODEL)
model_fix.get_parameters()[0].set_layout(ov.Layout("NCHW"))
ov.set_batch(model_fix, 1)

# Find the labels path and the specific Gather that indexes argmax results
labels_output = None
for out in model_fix.outputs:
    if "labels" in out.get_names():
        labels_output = out

# Trace backward from labels Result to find the Gather that gets argmax data
result_node = labels_output.get_node()
# labels Result -> Convert(I64->FP32) -> Reshape -> Gather
convert_node = result_node.input(0).get_source_output().get_node()
print(f"Step 1 (from Result): {convert_node.get_type_name()} '{convert_node.get_friendly_name()}'")

reshape_node = convert_node.input(0).get_source_output().get_node()
print(f"Step 2: {reshape_node.get_type_name()} '{reshape_node.get_friendly_name()}'")

# Could be Gather directly, or could have intermediate nodes
current = reshape_node
while current.get_type_name() != "Gather":
    current = current.input(0).get_source_output().get_node()
    print(f"Step: {current.get_type_name()} '{current.get_friendly_name()}'")

gather_node = current
print(f"\nTarget Gather: '{gather_node.get_friendly_name()}'")

# Gather input 0 = data (I64 argmax), input 1 = indices, input 2 = axis
data_src = gather_node.input(0).get_source_output()
indices_src = gather_node.input(1).get_source_output()
print(f"  Data source: {data_src.get_node().get_friendly_name()} type={data_src.get_element_type()} shape={data_src.get_partial_shape()}")
print(f"  Indices source: {indices_src.get_node().get_friendly_name()} type={indices_src.get_element_type()}")

# Insert Convert(I64→FP32) on data input
if data_src.get_element_type() in (ov.Type.i64, ov.Type.i32):
    convert_to_fp32 = ops.convert(data_src, ov.Type.f32)
    gather_node.input(0).replace_source_output(convert_to_fp32.output(0))
    print(f"  -> Inserted Convert({data_src.get_element_type()}→FP32) before Gather data input")

model_fix.validate_nodes_and_infer_types()

# Print output types
for out in model_fix.outputs:
    print(f"  Output '{out.get_any_name()}': {out.get_partial_shape()}, type: {out.get_element_type()}")

# Compile and test
npu_fix = core.compile_model(model_fix, "NPU")
req_fix = npu_fix.create_infer_request()
req_fix.infer({0: img})
fix_labels = req_fix.get_output_tensor(1).data.copy().flatten()
print(f"\nNPU (fixed) labels unique: {np.unique(fix_labels)}")
print(f"NPU (fixed) labels dist: 0={np.sum(fix_labels==0)}, 1={np.sum(fix_labels==1)}, 2={np.sum(fix_labels==2)}")
print(f"NPU (fixed) labels first 20: {fix_labels[:20]}")
print(f"\nCPU labels first 20: {cpu_labels[:20]}")

match = np.sum(cpu_labels == fix_labels)
total = len(cpu_labels)
print(f"\nMatch with CPU: {match}/{total} ({100*match/total:.1f}%)")
