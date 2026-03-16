#!/usr/bin/env python3
"""
Verify fix: move Convert(I64→FP32) BEFORE Gather in labels path.
NPU handles FP32 Gather correctly but breaks on I64 Gather.
"""
import numpy as np
import openvino as ov
import cv2

MODEL = "/home/dlstreamer/dlstreamer/dynamic_batch_models/optimized_model.xml"
VIDEO = "/home/dlstreamer/video-examples/warehouse.avi"

core = ov.Core()

# Read a frame with detections
cap = cv2.VideoCapture(VIDEO)
for _ in range(60):
    ret, frame = cap.read()
cap.release()

h, w = 416, 416
img = cv2.resize(frame, (w, h)).transpose(2, 0, 1)[np.newaxis].astype(np.float32)

# === CPU reference ===
model_cpu = core.read_model(MODEL)
model_cpu.get_parameters()[0].set_layout(ov.Layout("NCHW"))
ov.set_batch(model_cpu, 1)
cpu_compiled = core.compile_model(model_cpu, "CPU")
cpu_req = cpu_compiled.create_infer_request()
cpu_req.infer({0: img})
cpu_labels = cpu_req.get_output_tensor(1).data.copy().flatten()
print(f"CPU labels unique: {np.unique(cpu_labels)}")
print(f"CPU labels distribution: 0={np.sum(cpu_labels==0)}, 1={np.sum(cpu_labels==1)}, 2={np.sum(cpu_labels==2)}")
print(f"CPU labels first 20: {cpu_labels[:20]}")

# === NPU with graph fix: move Convert(I64->FP32) before Gather ===
print("\n" + "=" * 60)
print("NPU with fix: insert Convert(I64->FP32) before Gather data inputs")
model_fix = core.read_model(MODEL)
model_fix.get_parameters()[0].set_layout(ov.Layout("NCHW"))
ov.set_batch(model_fix, 1)

# Walk the graph and find Gather nodes whose data input is I64.
# Insert Convert(I64→FP32) on the data input so Gather runs on FP32.
from openvino import Type
import openvino.opset13 as ops

modified = False
for node in model_fix.get_ordered_ops():
    type_name = node.get_type_name()
    
    # Find Gather nodes
    if type_name == "Gather":
        # Input 0 is data, input 1 is indices, input 2 is axis
        data_input = node.input(0).get_source_output()
        data_type = data_input.get_element_type()
        
        if data_type == Type.i64 or data_type == Type.i32:
            print(f"  Found Gather '{node.get_friendly_name()}' with {data_type} data input")
            print(f"    Data source: {data_input.get_node().get_friendly_name()} ({data_input.get_node().get_type_name()})")
            
            # Insert Convert(I64→FP32) on the data input
            convert_node = ops.convert(data_input, Type.f32)
            convert_node.set_friendly_name(node.get_friendly_name() + "_data_to_fp32")
            node.input(0).replace_source_output(convert_node.output(0))
            modified = True
            print(f"    Inserted Convert({data_type}→FP32) before Gather data input")

# Also find Convert(I64→FP32) at the end of labels path and remove them
# since data is now already FP32 after Gather
for output in model_fix.outputs:
    result_node = output.get_node()
    producer = result_node.input(0).get_source_output()
    producer_node = producer.get_node()
    
    if producer_node.get_type_name() == "Convert":
        src_type = producer_node.input(0).get_source_output().get_element_type()
        dst_type = producer_node.get_output_element_type(0)
        if dst_type == Type.f32 and src_type == Type.f32:
            # This Convert is now FP32→FP32, redundant — remove it
            original_names = output.get_names()
            result_node.input(0).replace_source_output(producer_node.input(0).get_source_output())
            result_node.input(0).get_source_output().set_names(original_names)
            print(f"  Removed redundant Convert(FP32→FP32) from output '{output.get_any_name()}'")

if modified:
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
print(f"NPU (fixed) labels distribution: 0={np.sum(fix_labels==0)}, 1={np.sum(fix_labels==1)}, 2={np.sum(fix_labels==2)}")
print(f"NPU (fixed) labels first 20: {fix_labels[:20]}")

# Compare
match = np.sum(cpu_labels == fix_labels)
total = len(cpu_labels)
print(f"\nMatch with CPU: {match}/{total} ({100*match/total:.1f}%)")
