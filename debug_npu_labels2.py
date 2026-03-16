#!/usr/bin/env python3
"""Compare NPU vs CPU with constant folding applied."""
import numpy as np
import openvino as ov
import cv2
import struct
from openvino.passes import Manager, ConstantFolding

MODEL = "/home/dlstreamer/dlstreamer/dynamic_batch_models/optimized_model.xml"
VIDEO = "/home/dlstreamer/video-examples/warehouse.avi"

core = ov.Core()

# Read frame first
cap = cv2.VideoCapture(VIDEO)
for _ in range(60):
    ret, frame = cap.read()
cap.release()

# -- Approach 1: NPU baseline (set_batch only) --
print("=" * 60)
print("Approach 1: NPU with set_batch only")
model1 = core.read_model(MODEL)
model1.get_parameters()[0].set_layout(ov.Layout("NCHW"))
ov.set_batch(model1, 1)
npu1 = core.compile_model(model1, "NPU")
h, w = 416, 416
img = cv2.resize(frame, (w, h)).transpose(2, 0, 1)[np.newaxis].astype(np.float32)
req1 = npu1.create_infer_request()
req1.infer({0: img})
labels1 = req1.get_output_tensor(1).data.copy().flatten()
print(f"  labels unique: {np.unique(labels1)}")
print(f"  labels nonzero count: {np.count_nonzero(labels1)}")
print(f"  labels first 20: {labels1[:20]}")

# -- Approach 2: NPU with constant folding --
print("\n" + "=" * 60)
print("Approach 2: NPU with set_batch + constant folding")
model2 = core.read_model(MODEL)
model2.get_parameters()[0].set_layout(ov.Layout("NCHW"))
ov.set_batch(model2, 1)
manager = Manager()
manager.register_pass(ConstantFolding())
manager.run_passes(model2)
# Check output types after CF
for out in model2.outputs:
    print(f"  Output '{out.get_any_name()}': {out.get_partial_shape()}, type: {out.get_element_type()}")
npu2 = core.compile_model(model2, "NPU")
req2 = npu2.create_infer_request()
req2.infer({0: img})
labels2 = req2.get_output_tensor(1).data.copy().flatten()
print(f"  labels unique: {np.unique(labels2)}")
print(f"  labels nonzero count: {np.count_nonzero(labels2)}")
print(f"  labels first 20: {labels2[:20]}")

# Check if nonzero values are float bit patterns
nonzero_idxs = np.where(labels2 != 0)[0]
if len(nonzero_idxs) > 0:
    print(f"  Nonzero positions: {nonzero_idxs}")
    for idx in nonzero_idxs[:10]:
        val = labels2[idx]
        raw = struct.pack('f', val)
        bits = struct.unpack('I', raw)[0]
        # Reinterpret as float
        print(f"    [{idx}] float={val:.6f}, raw=0x{bits:08X}")

# -- Approach 3: CPU for reference --
print("\n" + "=" * 60)
print("Approach 3: CPU reference")
model3 = core.read_model(MODEL)
model3.get_parameters()[0].set_layout(ov.Layout("NCHW"))
ov.set_batch(model3, 1)
cpu3 = core.compile_model(model3, "CPU")
req3 = cpu3.create_infer_request()
req3.infer({0: img})
labels3 = req3.get_output_tensor(1).data.copy().flatten()
print(f"  labels unique: {np.unique(labels3)}")
print(f"  labels distribution: 0={np.sum(labels3==0)}, 1={np.sum(labels3==1)}, 2={np.sum(labels3==2)}")
print(f"  labels first 20: {labels3[:20]}")

# -- Approach 4: NPU with CF + I64->I32 conversion --
print("\n" + "=" * 60)
print("Approach 4: NPU with CF + I64 constants -> I32")
from openvino.runtime import opset13 as ops
model4 = core.read_model(MODEL)
model4.get_parameters()[0].set_layout(ov.Layout("NCHW"))
ov.set_batch(model4, 1)

# Convert I64 constants to I32 and change ShapeOf/Range output types
for node in model4.get_ordered_ops():
    type_name = node.get_type_name()
    if type_name == "Constant":
        if node.get_element_type() == ov.Type.i64:
            data = node.get_data().astype(np.int32)
            new_const = ops.constant(data)
            new_const.set_friendly_name(node.get_friendly_name())
            for target_input in list(node.output(0).get_target_inputs()):
                target_input.replace_source_output(new_const.output(0))
    elif type_name == "ShapeOf":
        if node.get_output_element_type(0) == ov.Type.i64:
            node.set_output_type(0, ov.Type.i32)
    elif type_name == "Range":
        if node.get_output_element_type(0) == ov.Type.i64:
            node.set_output_type(0, ov.Type.i32)

model4.validate_nodes_and_infer_types()
manager4 = Manager()
manager4.register_pass(ConstantFolding())
manager4.run_passes(model4)

for out in model4.outputs:
    print(f"  Output '{out.get_any_name()}': {out.get_partial_shape()}, type: {out.get_element_type()}")

npu4 = core.compile_model(model4, "NPU")
req4 = npu4.create_infer_request()
req4.infer({0: img})
labels4 = req4.get_output_tensor(1).data.copy().flatten()
print(f"  labels unique: {np.unique(labels4)}")
print(f"  labels nonzero count: {np.count_nonzero(labels4)}")
print(f"  labels first 20: {labels4[:20]}")

nonzero_idxs4 = np.where(labels4 != 0)[0]
if len(nonzero_idxs4) > 0:
    print(f"  Nonzero positions ({len(nonzero_idxs4)}): {nonzero_idxs4[:20]}")
    for idx in nonzero_idxs4[:10]:
        val = labels4[idx]
        raw = struct.pack('f', val)
        bits = struct.unpack('I', raw)[0]
        print(f"    [{idx}] float={val:.6f}, raw=0x{bits:08X}")
