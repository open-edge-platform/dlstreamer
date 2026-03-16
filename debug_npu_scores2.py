#!/usr/bin/env python3
"""
Compare Multiply inputs between CPU and NPU.
Also test: does adding extra Result nodes fix the all-zeros issue?
"""
import numpy as np
import openvino as ov
import openvino.opset13 as ops
from openvino._pyopenvino import op as ov_op
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
r_cpu = cpu.create_infer_request()
r_cpu.infer({0: img})
cpu_labels = r_cpu.get_output_tensor(1).data.copy().flatten()
print(f"CPU ref labels: unique={np.unique(cpu_labels)}, dist: 0={np.sum(cpu_labels==0)}, 1={np.sum(cpu_labels==1)}, 2={np.sum(cpu_labels==2)}")

# ==========================================
# Model with debug outputs for Multiply inputs
# ==========================================
print("\n=== Model with debug intermediate outputs ===")
model_dbg = core.read_model(MODEL)
model_dbg.get_parameters()[0].set_layout(ov.Layout("NCHW"))
ov.set_batch(model_dbg, 1)

# Find labels path
for out in model_dbg.outputs:
    if "labels" in out.get_names():
        n = out.get_node().input(0).get_source_output().get_node()
        while n.get_type_name() != "TopK":
            n = n.input(0).get_source_output().get_node()
        mul_node = n.input(0).get_source_output().get_node()
        break

# Add Sigmoid output (input 0 of Multiply) as result
sig_out = mul_node.input(0).get_source_output()
r0 = ov_op.Result(sig_out)

# Add factor (input 1 of Multiply) as result  
factor_out = mul_node.input(1).get_source_output()
r1 = ov_op.Result(factor_out)

# Add Multiply output as result
mul_out = mul_node.output(0)
r2 = ov_op.Result(mul_out)

model_dbg.add_results([r0, r1, r2])
model_dbg.validate_nodes_and_infer_types()

# CPU
cpu_dbg = core.compile_model(model_dbg, "CPU")
rc = cpu_dbg.create_infer_request()
rc.infer({0: img})

# NPU
npu_dbg = core.compile_model(model_dbg, "NPU")
rn = npu_dbg.create_infer_request()
rn.infer({0: img})

output_labels = ["boxes", "labels", "sigmoid(cls)", "factor(obj)", "multiply(cls*obj)"]
for i in range(5):
    cd = rc.get_output_tensor(i).data.copy()
    nd = rn.get_output_tensor(i).data.copy()
    cf = cd.flatten()
    nf = nd.flatten()
    diff = np.abs(cd - nd)
    
    print(f"\n  [{i}] {output_labels[i]}:  shape={cd.shape}")
    print(f"    CPU: min={cf.min():.6f}  max={cf.max():.6f}  mean={cf.mean():.6f}  nonzero={np.count_nonzero(cf)}")
    print(f"    NPU: min={nf.min():.6f}  max={nf.max():.6f}  mean={nf.mean():.6f}  nonzero={np.count_nonzero(nf)}")
    print(f"    Diff: max={diff.max():.6f} mean={diff.mean():.6f}")
    
    # Show sample values
    if len(cf) <= 300:
        print(f"    CPU first 10: {cf[:10]}")
        print(f"    NPU first 10: {nf[:10]}")
    else:
        # Show from different regions
        for start in [0, len(cf)//3, 2*len(cf)//3]:
            print(f"    CPU[{start}:{start+5}]: {cf[start:start+5]}")
            print(f"    NPU[{start}:{start+5}]: {nf[start:start+5]}")

# ==========================================
# Key question: with debug outputs added, are NPU labels still all zeros?
# ==========================================
npu_labels_dbg = rn.get_output_tensor(1).data.copy().flatten()
print(f"\n\nNPU labels WITH debug outputs: unique values count = {len(np.unique(npu_labels_dbg))}")
print(f"NPU labels first 10: {npu_labels_dbg[:10]}")
any_nonzero = np.any(npu_labels_dbg != 0)
print(f"Any non-zero labels? {any_nonzero}")

# ==========================================
# Test: add JUST the Multiply output as extra result → does it fix labels?
# ==========================================
print("\n\n=== Test: add ONLY Multiply output as extra Result ===")
model_min = core.read_model(MODEL)
model_min.get_parameters()[0].set_layout(ov.Layout("NCHW"))
ov.set_batch(model_min, 1)

for out in model_min.outputs:
    if "labels" in out.get_names():
        nm = out.get_node().input(0).get_source_output().get_node()
        while nm.get_type_name() != "TopK":
            nm = nm.input(0).get_source_output().get_node()
        mul_min = nm.input(0).get_source_output().get_node()
        break

r_mul = ov_op.Result(mul_min.output(0))
model_min.add_results([r_mul])
model_min.validate_nodes_and_infer_types()

npu_min = core.compile_model(model_min, "NPU")
rn_min = npu_min.create_infer_request()
rn_min.infer({0: img})
npu_labels_min = rn_min.get_output_tensor(1).data.copy().flatten()
print(f"NPU labels: unique values = {np.unique(npu_labels_min)[:5]}")
print(f"NPU labels dist: 0={np.sum(npu_labels_min==0)}, nonzero={np.count_nonzero(npu_labels_min)}")

# ==========================================
# Test: add TopK output 0 (FP32 max scores) as extra result → does it fix labels?
# ==========================================  
print("\n\n=== Test: add TopK output 0 (max scores) as extra Result ===")
model_tk = core.read_model(MODEL)
model_tk.get_parameters()[0].set_layout(ov.Layout("NCHW"))
ov.set_batch(model_tk, 1)

for out in model_tk.outputs:
    if "labels" in out.get_names():
        ntk = out.get_node().input(0).get_source_output().get_node()
        while ntk.get_type_name() != "TopK":
            ntk = ntk.input(0).get_source_output().get_node()
        topk_tk = ntk
        break

# TopK output 0 = max values [1, 3549, 1]
r_topk0 = ov_op.Result(topk_tk.output(0))
model_tk.add_results([r_topk0])
model_tk.validate_nodes_and_infer_types()

npu_tk = core.compile_model(model_tk, "NPU")
rn_tk = npu_tk.create_infer_request()
rn_tk.infer({0: img})
npu_labels_tk = rn_tk.get_output_tensor(1).data.copy().flatten()
print(f"NPU labels: unique values = {np.unique(npu_labels_tk)[:5]}")
print(f"NPU labels dist: 0={np.sum(npu_labels_tk==0)}, nonzero={np.count_nonzero(npu_labels_tk)}")
