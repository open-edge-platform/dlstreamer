# Reading Intermediate Tensors from OpenVINO Models

## Overview

OpenVINO models are computational graphs (DAGs) where nodes are operations and edges are tensors.
By default, only tensors connected to `Result` nodes are accessible after inference.

To read a tensor from **any point inside the model**, you add a temporary `Result` node
to that point. This exposes it as an additional model output without changing any computations.

```
Before:                          After:
  Conv → ReLU → Pool → Result     Conv → ReLU → Pool → Result
                                          ↓
                                     Result (debug)   ← new output
```

---

## Prerequisites

```python
import numpy as np
import openvino as ov
from openvino._pyopenvino import op as ov_op

core = ov.Core()
```

---

## 1. Adding a Result Node to an Intermediate Layer

### Step 1 — Load the model

```python
model = core.read_model("model.xml")
```

### Step 2 — Find the node you want to inspect

**By operation type:**
```python
target_node = None
for node in model.get_ordered_ops():
    if node.get_type_name() == "Sigmoid":
        target_node = node
        break
```

**By layer name (as shown in the XML):**
```python
target_node = None
for node in model.get_ordered_ops():
    if "aten::sigmoid/Sigmoid" in node.get_friendly_name():
        target_node = node
        break
```

**By output tensor name:**
```python
target_node = None
for node in model.get_ordered_ops():
    for output in node.outputs():
        if "cls_scores" in output.get_names():
            target_node = node
            break
    if target_node:
        break
```

### Step 3 — Create a Result node and attach it

```python
# Attach to the node's output port (port 0 by default)
debug_result = ov_op.Result(target_node.output(0))
model.add_results([debug_result])
model.validate_nodes_and_infer_types()
```

### Step 4 — Compile and run inference

```python
compiled = core.compile_model(model, "CPU")
req = compiled.create_infer_request()
req.infer({0: input_data})
```

### Step 5 — Read the intermediate tensor

The new output is appended after all existing outputs:

```python
num_original_outputs = 2  # e.g. boxes + labels
debug_tensor = req.get_output_tensor(num_original_outputs).data.copy()
print(f"Shape: {debug_tensor.shape}")
print(f"Dtype: {debug_tensor.dtype}")
print(f"Values: {debug_tensor}")
```

---

## 2. Inspecting Multiple Intermediate Points

You can add multiple Result nodes at once:

```python
model = core.read_model("model.xml")

results_to_add = []
debug_names = []

for node in model.get_ordered_ops():
    name = node.get_friendly_name()

    if "aten::mul/Multiply" in name:
        # Input 0 of Multiply (e.g. Sigmoid output)
        results_to_add.append(ov_op.Result(node.input(0).get_source_output()))
        debug_names.append("multiply_input_0")

        # Input 1 of Multiply (e.g. objectness)
        results_to_add.append(ov_op.Result(node.input(1).get_source_output()))
        debug_names.append("multiply_input_1")

        # Output of Multiply
        results_to_add.append(ov_op.Result(node.output(0)))
        debug_names.append("multiply_output")
        break

model.add_results(results_to_add)
model.validate_nodes_and_infer_types()

compiled = core.compile_model(model, "CPU")
req = compiled.create_infer_request()
req.infer({0: input_data})

num_original = 2  # boxes + labels
for i, name in enumerate(debug_names):
    tensor = req.get_output_tensor(num_original + i).data.copy()
    print(f"{name}: shape={tensor.shape}, min={tensor.min():.4f}, "
          f"max={tensor.max():.4f}, nonzero={np.count_nonzero(tensor)}")
```

---

## 3. Comparing CPU vs NPU at an Intermediate Layer

This is the primary use case for debugging device-specific issues:

```python
model_cpu = core.read_model("model.xml")
model_npu = core.read_model("model.xml")

# Add the same debug Result to both models
for m in [model_cpu, model_npu]:
    for node in m.get_ordered_ops():
        if node.get_type_name() == "Sigmoid":
            m.add_results([ov_op.Result(node.output(0))])
            break
    m.validate_nodes_and_infer_types()

cpu_compiled = core.compile_model(model_cpu, "CPU")
npu_compiled = core.compile_model(model_npu, "NPU")

rc = cpu_compiled.create_infer_request()
rn = npu_compiled.create_infer_request()
rc.infer({0: input_data})
rn.infer({0: input_data})

cpu_tensor = rc.get_output_tensor(2).data.copy()  # debug output at index 2
npu_tensor = rn.get_output_tensor(2).data.copy()

print(f"CPU: shape={cpu_tensor.shape}, nonzero={np.count_nonzero(cpu_tensor)}")
print(f"NPU: shape={npu_tensor.shape}, nonzero={np.count_nonzero(npu_tensor)}")
print(f"Max abs diff: {np.abs(cpu_tensor - npu_tensor).max():.6f}")
print(f"Mean abs diff: {np.abs(cpu_tensor - npu_tensor).mean():.6f}")
```

---

## 4. Listing All Nodes in a Model (Reconnaissance)

Before adding debug outputs, you may want to see what's in the graph:

```python
model = core.read_model("model.xml")

for node in model.get_ordered_ops():
    outputs = []
    for o in node.outputs():
        names = o.get_names()
        name_str = f" ({', '.join(names)})" if names else ""
        outputs.append(f"{o.get_partial_shape()} {o.get_element_type()}{name_str}")

    print(f"{node.get_type_name():20s}  {node.get_friendly_name():50s}  "
          f"→ {', '.join(outputs)}")
```

Example output:
```
Parameter             inputs                                              → [1,3,416,416] f32 (image)
Const                 __module.model.backbone.stem/prim::ListConstruct    → [6] i64
Reshape               __module.model.backbone.stem/aten::reshape/Reshape  → [1,3,208,2,208,2] f32
Sigmoid               aten::sigmoid/Sigmoid                               → [1,3549,3] f32 (cls_scores)
Multiply              aten::mul/Multiply                                  → [1,3549,3] f32 (scores)
TopK                  aten::max/TopK                                      → [1,3549,1] f32, [1,3549,1] i64
```

---

## 5. Nodes with Multiple Output Ports

Some operations have more than one output. For example, `TopK` has two:
- Port 0: top-k **values** (FP32)
- Port 1: top-k **indices** (I64)

```python
for node in model.get_ordered_ops():
    if node.get_type_name() == "TopK":
        # Read values (port 0)
        model.add_results([ov_op.Result(node.output(0))])
        # Read indices (port 1)
        model.add_results([ov_op.Result(node.output(1))])
        break
```

---

## 6. Reading a Node's Input (What Feeds Into It)

To see what tensor arrives at a specific input port of a node:

```python
for node in model.get_ordered_ops():
    if "aten::index/Gather" in node.get_friendly_name():
        # Input 0 = data tensor
        data_tensor = node.input(0).get_source_output()
        # Input 1 = indices tensor
        indices_tensor = node.input(1).get_source_output()
        # Input 2 = axis
        axis_tensor = node.input(2).get_source_output()

        model.add_results([
            ov_op.Result(data_tensor),
            ov_op.Result(indices_tensor),
            ov_op.Result(axis_tensor),
        ])
        break
```

`node.input(i).get_source_output()` returns the output port of the **upstream** node
that is connected to this input — i.e., the tensor flowing into this point.

---

## 7. Walking the Graph (Tracing Paths)

### Tracing backward (from output toward input)

```python
# Start from a Result node and walk backward
labels_output = None
for out in model.outputs:
    if "labels" in out.get_names():
        labels_output = out
        break

node = labels_output.get_node()  # Result node
while node.get_input_size() > 0:
    # Follow input 0 (main data path)
    upstream = node.input(0).get_source_output()
    node = upstream.get_node()
    print(f"{node.get_type_name():20s}  {node.get_friendly_name()}")
    if node.get_type_name() == "Parameter":
        break
```

### Tracing forward (from a node to its consumers)

```python
for node in model.get_ordered_ops():
    if node.get_type_name() == "Sigmoid":
        for target_input in node.output(0).get_target_inputs():
            consumer = target_input.get_node()
            print(f"Sigmoid output goes to: {consumer.get_type_name()} "
                  f"({consumer.get_friendly_name()})")
        break
```

---

## Notes

- **Adding Result nodes does not change computations.** It only exposes existing tensors
  as additional outputs. The model computes exactly the same values.

- **Works on any device:** CPU, GPU, NPU. You can add the same debug outputs and compare
  tensor values across devices to find where they diverge.

- **Performance:** Each additional Result may slightly increase memory usage (the runtime
  must keep that tensor alive until you read it). For debugging this is negligible.

- **The original model file is not modified.** Changes exist only in memory. Reloading
  the model with `core.read_model()` gives back the original graph.

- **Output tensor indexing:** Original outputs keep their indices (0, 1, …). Debug outputs
  are appended at the end in the order you added them.
