#!/usr/bin/env python3
"""Compare NPU vs CPU inference outputs for the optimized_model."""
import numpy as np
import openvino as ov
import cv2
import struct
 
MODEL = "/home/dlstreamer/dlstreamer/dynamic_batch_models/optimized_model.xml"
VIDEO = "/home/dlstreamer/video-examples/warehouse.avi"
 
core = ov.Core()
model = core.read_model(MODEL)
model.get_parameters()[0].set_layout(ov.Layout("NCHW"))
ov.set_batch(model, 1)
 
# Compile for CPU
cpu_compiled = core.compile_model(model, "CPU")
cpu_req = cpu_compiled.create_infer_request()
 
# Compile for NPU
npu_compiled = core.compile_model(model, "NPU")
npu_req = npu_compiled.create_infer_request()
 
# Read one frame
cap = cv2.VideoCapture(VIDEO)
for _ in range(60):  # skip to frame 60 where we have detections
    ret, frame = cap.read()
cap.release()
 
# Prepare input (simple resize, ignore preprocessing details for debugging)
input_shape = model.input().get_partial_shape()
h, w = input_shape[2].get_length(), input_shape[3].get_length()
img = cv2.resize(frame, (w, h))
img = img.transpose(2, 0, 1)  # HWC -> CHW
img = np.expand_dims(img, 0).astype(np.float32)
 
print(f"Input shape: {img.shape}, dtype: {img.dtype}")
print(f"Model input: {model.input().get_partial_shape()}, type: {model.input().get_element_type()}")
for out in model.outputs:
    print(f"Model output '{out.get_any_name()}': {out.get_partial_shape()}, type: {out.get_element_type()}")
 
# CPU inference
cpu_req.infer({0: img})
cpu_boxes = cpu_req.get_output_tensor(0).data.copy()
cpu_labels = cpu_req.get_output_tensor(1).data.copy()
 
# NPU inference
npu_req.infer({0: img})
npu_boxes = npu_req.get_output_tensor(0).data.copy()
npu_labels = npu_req.get_output_tensor(1).data.copy()
 
print(f"\n=== CPU outputs ===")
print(f"boxes shape: {cpu_boxes.shape}, dtype: {cpu_boxes.dtype}")
print(f"labels shape: {cpu_labels.shape}, dtype: {cpu_labels.dtype}")
print(f"labels first 30: {cpu_labels.flatten()[:30]}")
print(f"labels unique: {np.unique(cpu_labels)}")
 
print(f"\n=== NPU outputs ===")
print(f"boxes shape: {npu_boxes.shape}, dtype: {npu_boxes.dtype}")
print(f"labels shape: {npu_labels.shape}, dtype: {npu_labels.dtype}")
print(f"labels first 30: {npu_labels.flatten()[:30]}")
print(f"labels unique: {np.unique(npu_labels)}")
 
# Check if NPU labels contain float bit patterns in FP32 output
print(f"\n=== NPU labels raw analysis ===")
labels_flat = npu_labels.flatten()
for i in range(min(30, len(labels_flat))):
    val = labels_flat[i]
    # Interpret as raw bytes
    raw_bytes = struct.pack('f', val)
    as_int = struct.unpack('I', raw_bytes)[0]
    print(f"  [{i:3d}] float={val:20.6f} | raw_bits=0x{as_int:08X} | cpu_label={cpu_labels.flatten()[i]:.1f}")
 
# Compare boxes (first 5 detections)
print(f"\n=== Boxes comparison (first 5 detections) ===")
for i in range(5):
    cpu_box = cpu_boxes[0, i]
    npu_box = npu_boxes[0, i]
    print(f"  [{i}] CPU: {cpu_box}, NPU: {npu_box}")
