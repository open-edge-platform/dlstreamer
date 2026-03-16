#!/usr/bin/env python3
"""
Deep diagnostic: check raw I64 bit patterns from NPU to understand
how NPU stores integer values. Then try multiple fix strategies.
"""
import numpy as np
import openvino as ov
import openvino.opset13 as ops
import cv2
import struct

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
print(f"CPU labels: unique={np.unique(cpu_labels)}, dist: 0={np.sum(cpu_labels==0)}, 1={np.sum(cpu_labels==1)}, 2={np.sum(cpu_labels==2)}")
print(f"CPU labels first 20: {cpu_labels[:20]}")

# ==========================================
# Strategy 1: Remove final Convert, get raw I64 from NPU, examine bit patterns
# ==========================================
print("\n=== Strategy 1: Examine raw I64 bit patterns from NPU ===")
model_raw = core.read_model(MODEL)
model_raw.get_parameters()[0].set_layout(ov.Layout("NCHW"))
ov.set_batch(model_raw, 1)

# Find labels Result → Convert → Reshape
labels_result = None
for res in model_raw.get_results():
    for name in res.output(0).get_names():
        if "labels" in name:
            labels_result = res
            break

# The Convert(I64→FP32) right before Result
convert_node = labels_result.input(0).get_source_output().get_node()
print(f"Convert node: {convert_node.get_type_name()} '{convert_node.get_friendly_name()}'")
print(f"  input type: {convert_node.input(0).get_source_output().get_element_type()}")
print(f"  output type: {convert_node.get_output_element_type(0)}")

# Replace Result's input: bypass Convert, connect directly to its I64 source
i64_source = convert_node.input(0).get_source_output()
labels_result.input(0).replace_source_output(i64_source)
model_raw.validate_nodes_and_infer_types()

for out in model_raw.outputs:
    print(f"  Output '{out.get_any_name()}': shape={out.get_partial_shape()}, type={out.get_element_type()}")

npu_raw_compiled = core.compile_model(model_raw, "NPU")
req_raw = npu_raw_compiled.create_infer_request()
req_raw.infer({0: img})

# Get the raw I64 tensor
raw_tensor = req_raw.get_output_tensor(1)
print(f"  Raw tensor shape: {raw_tensor.shape}, type: {raw_tensor.element_type}")

# Get data as I64
raw_i64 = raw_tensor.data.copy().flatten()
print(f"  Raw I64 values first 20: {raw_i64[:20]}")
print(f"  Raw I64 unique: {np.unique(raw_i64)}")

# Check if lower 32 bits contain FP32 bit patterns
print("\n  -- Checking if lower 32 bits contain FP32 bit patterns --")
for i in range(min(20, len(raw_i64))):
    val_i64 = int(raw_i64[i])
    lower32 = val_i64 & 0xFFFFFFFF
    upper32 = (val_i64 >> 32) & 0xFFFFFFFF
    # Interpret lower 32 bits as FP32
    fp32_from_lower = struct.unpack('f', struct.pack('I', lower32))[0]
    # Interpret the full I64 bytes as two FP32 values
    bytes_i64 = struct.pack('q', val_i64)
    fp32_pair = struct.unpack('ff', bytes_i64)
    print(f"  [{i:3d}] I64={val_i64:20d}  hex=0x{val_i64 & 0xFFFFFFFFFFFFFFFF:016X}"
          f"  lower32_as_f32={fp32_from_lower:.6f}  as_2xf32={fp32_pair}"
          f"  CPU_label={cpu_labels[i]:.0f}")

# Also try raw bytes
raw_bytes = bytes(raw_tensor.data.tobytes())
print(f"\n  Raw bytes length: {len(raw_bytes)}")
# Try reading as FP32 (100 values * 4 bytes = 400 bytes) from start
if len(raw_bytes) >= 400:
    fp32_from_bytes = np.frombuffer(raw_bytes[:400], dtype=np.float32)
    print(f"  First 400 bytes as FP32 ({len(fp32_from_bytes)} values): {fp32_from_bytes[:20]}")
# Try reading as FP32 skipping every other 4 bytes (8-byte I64 stride)
fp32_from_stride = np.frombuffer(raw_bytes, dtype=np.float32)[::2]  # every other float
print(f"  I64 lower halves as FP32 ({len(fp32_from_stride)} values): {fp32_from_stride[:20]}")
fp32_from_stride_upper = np.frombuffer(raw_bytes, dtype=np.float32)[1::2]  # upper halves
print(f"  I64 upper halves as FP32 ({len(fp32_from_stride_upper)} values): {fp32_from_stride_upper[:20]}")

# ==========================================
# Strategy 2: Convert I64→FP32 at TopK output (before Squeeze)
# ==========================================
print("\n\n=== Strategy 2: Convert at TopK output (before Squeeze) ===")
model_fix2 = core.read_model(MODEL)
model_fix2.get_parameters()[0].set_layout(ov.Layout("NCHW"))
ov.set_batch(model_fix2, 1)

# Find labels path: Result → Convert → Reshape → Gather → Reshape → Squeeze → TopK
labels_out = None
for out in model_fix2.outputs:
    if "labels" in out.get_names():
        labels_out = out
        break

result_n = labels_out.get_node()
convert_n = result_n.input(0).get_source_output().get_node()
reshape_n = convert_n.input(0).get_source_output().get_node()

# Find Gather
current = reshape_n
while current.get_type_name() != "Gather":
    current = current.input(0).get_source_output().get_node()
gather_n = current

# Gather data input → Reshape → Squeeze → TopK
data_reshape = gather_n.input(0).get_source_output().get_node()
print(f"Gather data source: {data_reshape.get_type_name()} '{data_reshape.get_friendly_name()}'")

squeeze_n = data_reshape.input(0).get_source_output().get_node()
print(f"  Squeeze: {squeeze_n.get_type_name()} '{squeeze_n.get_friendly_name()}'")

topk_out = squeeze_n.input(0).get_source_output()
topk_n = topk_out.get_node()
topk_port = topk_out.get_index()
print(f"  TopK: {topk_n.get_type_name()} '{topk_n.get_friendly_name()}' output_port={topk_port}")
print(f"  TopK output type: {topk_out.get_element_type()}")

# Insert Convert(I64→FP32) right after TopK output 1 (before Squeeze)
convert_after_topk = ops.convert(topk_out, ov.Type.f32)
squeeze_n.input(0).replace_source_output(convert_after_topk.output(0))
print(f"  -> Inserted Convert(I64→FP32) between TopK and Squeeze")

model_fix2.validate_nodes_and_infer_types()

for out in model_fix2.outputs:
    print(f"  Output '{out.get_any_name()}': {out.get_partial_shape()}, type: {out.get_element_type()}")

npu_fix2 = core.compile_model(model_fix2, "NPU")
req_fix2 = npu_fix2.create_infer_request()
req_fix2.infer({0: img})
fix2_labels = req_fix2.get_output_tensor(1).data.copy().flatten()
print(f"\nNPU (fix2-topk) labels unique: {np.unique(fix2_labels)}")
print(f"NPU (fix2-topk) dist: 0={np.sum(fix2_labels==0)}, 1={np.sum(fix2_labels==1)}, 2={np.sum(fix2_labels==2)}")
print(f"NPU (fix2-topk) first 20: {fix2_labels[:20]}")

match2 = np.sum(cpu_labels == fix2_labels)
print(f"Match with CPU: {match2}/{len(cpu_labels)} ({100*match2/len(cpu_labels):.1f}%)")

# ==========================================
# Strategy 3: Convert ALL I64 outputs in the model to FP32
# ==========================================
print("\n\n=== Strategy 3: Convert ALL I64 nodes' outputs to FP32 ===")
model_fix3 = core.read_model(MODEL)
model_fix3.get_parameters()[0].set_layout(ov.Layout("NCHW"))
ov.set_batch(model_fix3, 1)

# Find all nodes that produce I64 and have consumers (not Result nodes)
i64_nodes_fixed = 0
for op in model_fix3.get_ordered_ops():
    if op.get_type_name() in ("Result", "Constant", "Parameter"):
        continue
    for i in range(op.get_output_size()):
        if op.get_output_element_type(i) in (ov.Type.i64,):
            out_port = op.output(i)
            # Insert Convert(I64→FP32) for all consumers
            consumers = list(out_port.get_target_inputs())
            if consumers:
                cvt = ops.convert(out_port, ov.Type.f32)
                for consumer in consumers:
                    # Don't convert if consumer is Convert that already goes to a smaller type
                    consumer.replace_source_output(cvt.output(0))
                i64_nodes_fixed += 1
                print(f"  Converted: {op.get_type_name()} '{op.get_friendly_name()}' output {i}")

print(f"\nTotal I64 outputs converted: {i64_nodes_fixed}")

try:
    model_fix3.validate_nodes_and_infer_types()
    for out in model_fix3.outputs:
        print(f"  Output '{out.get_any_name()}': {out.get_partial_shape()}, type: {out.get_element_type()}")

    npu_fix3 = core.compile_model(model_fix3, "NPU")
    req_fix3 = npu_fix3.create_infer_request()
    req_fix3.infer({0: img})
    fix3_labels = req_fix3.get_output_tensor(1).data.copy().flatten()
    print(f"\nNPU (fix3-all-i64) labels unique: {np.unique(fix3_labels)}")
    print(f"NPU (fix3-all-i64) dist: 0={np.sum(fix3_labels==0)}, 1={np.sum(fix3_labels==1)}, 2={np.sum(fix3_labels==2)}")
    print(f"NPU (fix3-all-i64) first 20: {fix3_labels[:20]}")
    match3 = np.sum(cpu_labels == fix3_labels)
    print(f"Match with CPU: {match3}/{len(cpu_labels)} ({100*match3/len(cpu_labels):.1f}%)")
except Exception as e:
    print(f"Strategy 3 failed: {e}")

# ==========================================
# Strategy 4: Reinterpret - remove Convert, get raw I64, reinterpret lower 32 bits as FP32
# ==========================================
print("\n\n=== Strategy 4: Reinterpret raw I64 lower-32-bits as FP32 ===")
# Use raw_i64 from Strategy 1
if len(raw_i64) > 0:
    # Reinterpret: each I64 value has FP32 bit pattern in lower 32 bits
    raw_bytes_all = raw_i64.astype(np.int64).tobytes()
    # Extract lower 32 bits of each I64 as uint32, then view as float32
    raw_u64 = raw_i64.view(np.uint64)
    lower32 = (raw_u64 & 0xFFFFFFFF).astype(np.uint32)
    reinterpreted = lower32.view(np.float32)
    
    print(f"Reinterpreted labels first 20: {reinterpreted[:20]}")
    print(f"Reinterpreted labels unique: {np.unique(reinterpreted)}")
    print(f"Reinterpreted dist: 0={np.sum(reinterpreted==0)}, 1={np.sum(reinterpreted==1)}, 2={np.sum(reinterpreted==2)}")
    
    match4 = np.sum(cpu_labels == reinterpreted)
    print(f"Match with CPU: {match4}/{len(cpu_labels)} ({100*match4/len(cpu_labels):.1f}%)")
