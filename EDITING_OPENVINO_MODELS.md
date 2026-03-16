# Editing OpenVINO Model Graphs Before Compilation

## Overview

An OpenVINO model (`ov::Model` / `ov.Model`) is a mutable computational graph. After loading
with `read_model()` and before `compile_model()`, you can:

- **Replace** a node's input with a different tensor
- **Insert** new operations into the graph
- **Remove** operations by bypassing them
- **Add/remove** outputs (`Result` nodes)
- **Change** element types via `Convert` nodes

The model file on disk is never modified — all changes exist only in memory.

```
read_model("model.xml")  →  modify graph  →  compile_model(model, device)
         ↑                       ↑                      ↑
     load from disk       manipulate in memory     compile to device
```

---

## Key Concepts

### Graph structure
- **Nodes** = operations (`Sigmoid`, `Gather`, `TopK`, `Const`, ...)
- **Edges** = tensors flowing between nodes
- **Output ports** = a node produces tensors on output ports (`node.output(0)`, `node.output(1)`, ...)
- **Input ports** = a node consumes tensors on input ports (`node.input(0)`, `node.input(1)`, ...)
- **`Parameter`** nodes = model inputs
- **`Result`** nodes = model outputs

### Output ports (`ov::Output<ov::Node>`)

Throughout this document, variables like `tensor_a`, `sigmoid_output`, `gathered` etc.
are **not** raw data arrays — they are **output ports**: references to edges in the graph.

An output port (`ov::Output<ov::Node>` in C++ / `openvino.runtime.Output` in Python) represents
a point in the graph where a tensor will flow at inference time. When you write:

```python
a = ops.relu(x)          # a = output port of the new ReLU node
b = ops.sigmoid(y)       # b = output port of the new Sigmoid node
c = ops.add(a, b)        # c = output port of Add — connects ReLU and Sigmoid outputs
```

you are **describing computation structure**, not executing it. No data flows until `infer()`.

You get output ports from:
- **Creating new nodes:** `ops.relu(x)` returns the output port of the new ReLU
- **Accessing existing node outputs:** `node.output(0)` — the first output port of `node`
- **Tracing upstream connections:** `node.input(0).get_source_output()` — the output port
  that feeds into this node's input 0

All node constructors accept output ports as inputs, enabling chaining:
```python
result = ops.squeeze(ops.add(ops.multiply(a, b), ops.multiply(c, d)), axis)
```

### The fundamental operation: replace_source_output
Both Python and C++ use the same core mechanism to rewire the graph:

```
result_node.input(0).replace_source_output(new_tensor)
```

This says: "the tensor going into `result_node` at port 0 should now come from `new_tensor`
instead of whatever was connected before."

---

## Python API

### Imports

```python
import numpy as np
import openvino as ov
import openvino.opset13 as ops                    # node constructors
from openvino._pyopenvino import op as ov_op      # for Result

core = ov.Core()
model = core.read_model("model.xml")
```

### Navigating the graph

```python
# List all operations
for node in model.get_ordered_ops():
    print(f"{node.get_type_name():20s} {node.get_friendly_name()}")

# Find a node by type
for node in model.get_ordered_ops():
    if node.get_type_name() == "TopK":
        topk = node
        break

# Find a node by name
for node in model.get_ordered_ops():
    if "aten::sigmoid/Sigmoid" in node.get_friendly_name():
        sigmoid = node
        break

# Find output by name
for out in model.outputs:
    if "labels" in out.get_names():
        labels_result = out.get_node()    # the Result node
        break
```

### Walking backward (output → input)

```python
# Start from a Result and trace the data path
node = labels_result
while node.get_input_size() > 0:
    upstream_output = node.input(0).get_source_output()  # output port of previous node
    node = upstream_output.get_node()                     # the previous node itself
    print(f"{node.get_type_name()} — {node.get_friendly_name()}")
```

### Walking forward (input → output consumers)

```python
# See who consumes this node's output
for target_input in sigmoid.output(0).get_target_inputs():
    consumer = target_input.get_node()
    print(f"→ {consumer.get_type_name()} ({consumer.get_friendly_name()})")
```

### Reading node metadata

```python
node = topk
print(f"Type: {node.get_type_name()}")
print(f"Name: {node.get_friendly_name()}")
print(f"Inputs: {node.get_input_size()}")
print(f"Outputs: {len(node.outputs())}")

# Shape and type of each output port
for i, out in enumerate(node.outputs()):
    print(f"  output[{i}]: {out.get_partial_shape()} {out.get_element_type()}")
    print(f"    names: {out.get_names()}")

# Shape and type of each input port (from upstream node)
for i in range(node.get_input_size()):
    src = node.input(i).get_source_output()
    print(f"  input[{i}]: {src.get_partial_shape()} {src.get_element_type()}")
    print(f"    from: {src.get_node().get_type_name()}")
```

---

### Creating new nodes (Python)

All operations are created via `openvino.opset13` (or the appropriate opset):

```python
import openvino.opset13 as ops

# Constants
c_int = ops.constant(np.array([1, 2, 3], dtype=np.int32))
c_float = ops.constant(np.float32(1.0))
c_shape = ops.constant(np.array([3549, 3], dtype=np.int32))

# Unary operations (take one input)
relu_out = ops.relu(some_tensor)
sigmoid_out = ops.sigmoid(some_tensor)
squeeze_out = ops.squeeze(some_tensor, ops.constant(np.array([2], dtype=np.int32)))

# Binary operations (take two inputs)
add_out = ops.add(tensor_a, tensor_b)
mul_out = ops.multiply(tensor_a, tensor_b)
sub_out = ops.subtract(tensor_a, tensor_b)
greater_out = ops.greater(tensor_a, tensor_b)    # returns bool tensor
maximum_out = ops.maximum(tensor_a, tensor_b)

# Type conversion
converted = ops.convert(bool_tensor, ov.Type.f32)  # bool → float (0.0 / 1.0)

# Reshape
reshaped = ops.reshape(tensor, ops.constant(np.array([1, -1, 3], dtype=np.int32)),
                       special_zero=False)

# Gather
gathered = ops.gather(data_tensor, indices_tensor, axis_tensor)

# StridedSlice (slicing)
sliced = ops.strided_slice(
    data,
    ops.constant(np.array([0, 0, 1], dtype=np.int32)),   # begin
    ops.constant(np.array([1, 100, 2], dtype=np.int32)),  # end
    ops.constant(np.array([1, 1, 1], dtype=np.int32)),    # strides
    begin_mask=[0, 0, 0],
    end_mask=[0, 0, 0]
)
```

Each `ops.*` call returns an **output port** that can be used as input to the next operation,
enabling chaining: `ops.relu(ops.add(a, b))`.

---

### Edit patterns (Python)

#### Pattern 1: Replace a Result's input

```python
# Before: Result ← Convert ← Gather ← ...
# After:  Result ← new_subgraph

new_output = ops.add(tensor_a, tensor_b)   # build new computation
result_node.input(0).replace_source_output(new_output.output(0))
model.validate_nodes_and_infer_types()
```

#### Pattern 2: Insert a node in the middle of the graph

```python
# Before: NodeA → NodeB
# After:  NodeA → ReLU → NodeB

node_a_output = node_b.input(0).get_source_output()   # what currently feeds NodeB
relu = ops.relu(node_a_output)                          # insert ReLU after NodeA
node_b.input(0).replace_source_output(relu.output(0))  # reconnect NodeB to ReLU
model.validate_nodes_and_infer_types()
```

#### Pattern 3: Bypass (remove) a node

```python
# Before: NodeA → Convert → NodeB
# After:  NodeA → NodeB  (Convert removed)

original_input = convert_node.input(0).get_source_output()  # NodeA's output
node_b.input(0).replace_source_output(original_input)       # skip Convert
model.validate_nodes_and_infer_types()
```

#### Pattern 4: Add a new output

```python
debug_result = ov_op.Result(some_node.output(0))
model.add_results([debug_result])
model.validate_nodes_and_infer_types()
```

#### Pattern 5: Replace all consumers of a node's output

```python
# Replace everywhere node_old.output(0) is used
old_output = node_old.output(0)
new_output = ops.relu(old_output)

for target_input in list(old_output.get_target_inputs()):
    if target_input.get_node() != new_output.node:  # don't redirect the ReLU itself
        target_input.replace_source_output(new_output.output(0))

model.validate_nodes_and_infer_types()
```

---

### Complete Python example: Replace I64 argmax with FP32

```python
import numpy as np
import openvino as ov
import openvino.opset13 as ops

core = ov.Core()
model = core.read_model("model.xml")
model.get_parameters()[0].set_layout(ov.Layout("NCHW"))
ov.set_batch(model, 1)

# Find the labels Result
for out in model.outputs:
    if "labels" in out.get_names():
        labels_result = out.get_node()
        break

# Walk backward to find Gather and TopK
n = labels_result.input(0).get_source_output().get_node()  # Convert
while n.get_type_name() != "Gather":
    n = n.input(0).get_source_output().get_node()
gather = n

while n.get_type_name() != "TopK":
    n = n.input(0).get_source_output().get_node()
topk = n

# Get Sigmoid (input 0 of Multiply, which is input 0 of TopK)
multiply = topk.input(0).get_source_output().get_node()
sigmoid_output = multiply.input(0).get_source_output()  # [1, N, C] FP32

N = sigmoid_output.get_partial_shape()[1].get_length()   # e.g. 3549
C = sigmoid_output.get_partial_shape()[2].get_length()   # e.g. 3

# Reuse Gather indices and axis from original graph
gather_indices = gather.input(1).get_source_output()
gather_axis = gather.input(2).get_source_output()

# Build FP32 replacement subgraph
sig_2d = ops.reshape(sigmoid_output,
    ops.constant(np.array([N, C], dtype=np.int32)), special_zero=False)

sig_gathered = ops.gather(sig_2d, gather_indices, gather_axis)

sig_3d = ops.reshape(sig_gathered,
    ops.constant(np.array([1, -1, C], dtype=np.int32)), special_zero=False)

# Iterative argmax in FP32
def slice_class(data, c):
    return ops.strided_slice(data,
        ops.constant(np.array([0, 0, c], dtype=np.int32)),
        ops.constant(np.array([0, 0, c + 1], dtype=np.int32)),
        ops.constant(np.array([1, 1, 1], dtype=np.int32)),
        begin_mask=[1, 1, 0], end_mask=[1, 1, 0])

max_score = slice_class(sig_3d, 0)
argmax_val = ops.constant(np.float32(0.0))

for c in range(1, C):
    sc = slice_class(sig_3d, c)
    is_better = ops.convert(ops.greater(sc, max_score), ov.Type.f32)
    not_better = ops.subtract(ops.constant(np.float32(1.0)), is_better)
    argmax_val = ops.add(
        ops.multiply(not_better, argmax_val),
        ops.multiply(is_better, ops.constant(np.float32(c)))
    )
    max_score = ops.maximum(max_score, sc)

argmax_2d = ops.squeeze(argmax_val, ops.constant(np.array([2], dtype=np.int32)))

# Rewire the graph
labels_result.input(0).replace_source_output(argmax_2d.output(0))
model.validate_nodes_and_infer_types()

# Compile and run
compiled = core.compile_model(model, "NPU")
req = compiled.create_infer_request()
req.infer({0: input_data})
labels = req.get_output_tensor(1).data.copy()
```

---

## C++ API

### Includes

```cpp
#include <openvino/openvino.hpp>

// Specific op headers (include only what you use)
#include <openvino/op/constant.hpp>
#include <openvino/op/convert.hpp>
#include <openvino/op/gather.hpp>
#include <openvino/op/greater.hpp>
#include <openvino/op/maximum.hpp>
#include <openvino/op/multiply.hpp>
#include <openvino/op/reshape.hpp>
#include <openvino/op/squeeze.hpp>
#include <openvino/op/strided_slice.hpp>
#include <openvino/op/subtract.hpp>
#include <openvino/op/add.hpp>
#include <openvino/op/topk.hpp>
```

### Loading and navigating

```cpp
ov::Core core;
auto model = core.read_model("model.xml");

// Iterate all outputs
for (auto& output : model->outputs()) {
    auto name = output.get_any_name();
    auto shape = output.get_partial_shape();
    auto type = output.get_element_type();
    std::cout << name << ": " << shape << " " << type << std::endl;
}

// Iterate all operations
for (auto& node : model->get_ordered_ops()) {
    std::cout << node->get_type_info().name << " "
              << node->get_friendly_name() << std::endl;
}
```

### Walking backward

```cpp
// Get the Result node for a specific output
auto result_node = model->output(1).get_node_shared_ptr();  // e.g. labels

// Walk backward through input port 0
auto node = result_node->input_value(0).get_node_shared_ptr();
while (node->get_input_size() > 0) {
    std::cout << node->get_type_info().name << " "
              << node->get_friendly_name() << std::endl;
    node = node->input_value(0).get_node_shared_ptr();
}
```

### Checking node type

```cpp
// By type_info name (string comparison)
if (node->get_type_info().name == std::string("Gather")) { ... }

// By dynamic_pointer_cast (type-safe, gives access to op-specific methods)
auto convert_op = std::dynamic_pointer_cast<ov::op::v0::Convert>(node);
if (convert_op) {
    auto dst_type = convert_op->get_destination_type();
    auto src_type = convert_op->input_value(0).get_element_type();
}
```

### Reading shape and type info

```cpp
// Output port
auto output_port = node->output(0);
auto shape = output_port.get_partial_shape();   // ov::PartialShape
auto type = output_port.get_element_type();     // ov::element::Type

// Static dimensions
if (!shape[2].is_dynamic()) {
    auto num_classes = shape[2].get_length();    // int64_t
}

// Rank
auto rank = shape.rank().get_length();           // int64_t

// Input port — get upstream info
auto input_port = node->input_value(0);
auto upstream_shape = input_port.get_partial_shape();
auto upstream_type = input_port.get_element_type();
auto upstream_node = input_port.get_node_shared_ptr();
```

---

### Creating new nodes (C++)

```cpp
// Constants
auto c_int = ov::op::v0::Constant::create(
    ov::element::i32, {3}, std::vector<int32_t>{1, 2, 3});

auto c_float = ov::op::v0::Constant::create(
    ov::element::f32, {}, {1.0f});  // scalar

auto c_shape = ov::op::v0::Constant::create(
    ov::element::i32, {2}, std::vector<int32_t>{3549, 3});

// Reshape: [1, 3549, 3] → [3549, 3]
auto reshaped = std::make_shared<ov::op::v1::Reshape>(
    input_tensor,     // ov::Output<ov::Node>
    c_shape,          // target shape constant
    false);           // special_zero

// Gather: data[indices] along axis
auto gathered = std::make_shared<ov::op::v8::Gather>(
    data_tensor,      // ov::Output<ov::Node>
    indices_tensor,   // ov::Output<ov::Node>
    axis_constant);   // ov::Output<ov::Node>

// Greater: a > b → bool tensor
auto greater = std::make_shared<ov::op::v1::Greater>(tensor_a, tensor_b);

// Convert: bool → float32
auto converted = std::make_shared<ov::op::v0::Convert>(
    bool_tensor, ov::element::f32);

// Arithmetic
auto added = std::make_shared<ov::op::v1::Add>(tensor_a, tensor_b);
auto multiplied = std::make_shared<ov::op::v1::Multiply>(tensor_a, tensor_b);
auto subtracted = std::make_shared<ov::op::v1::Subtract>(tensor_a, tensor_b);
auto max_val = std::make_shared<ov::op::v1::Maximum>(tensor_a, tensor_b);

// Squeeze: remove dimension
auto axis = ov::op::v0::Constant::create(ov::element::i32, {1}, {2});
auto squeezed = std::make_shared<ov::op::v0::Squeeze>(tensor, axis);

// StridedSlice: tensor[:, :, c:c+1]
auto begin = ov::op::v0::Constant::create(
    ov::element::i32, {3}, std::vector<int32_t>{0, 0, c});
auto end = ov::op::v0::Constant::create(
    ov::element::i32, {3}, std::vector<int32_t>{0, 0, c + 1});
auto strides = ov::op::v0::Constant::create(
    ov::element::i32, {3}, std::vector<int32_t>{1, 1, 1});
auto sliced = std::make_shared<ov::op::v1::StridedSlice>(
    data, begin, end, strides,
    std::vector<int64_t>{1, 1, 0},   // begin_mask: 1 = ignore this dim's begin
    std::vector<int64_t>{1, 1, 0});  // end_mask: 1 = ignore this dim's end
```

Each `std::make_shared<Op>(...)` returns a `std::shared_ptr<ov::Node>`. Its output is
accessed via `node->output(0)` which returns `ov::Output<ov::Node>` — the type accepted
as input by other node constructors.

---

### Edit patterns (C++)

#### Pattern 1: Replace a Result's input

```cpp
// result_node->input currently connected to old_subgraph
// Connect it to new_subgraph instead
result_node->input(0).replace_source_output(new_node->output(0));
model->validate_nodes_and_infer_types();
```

#### Pattern 2: Insert a node between two existing nodes

```cpp
// Before: node_a → node_b
// After:  node_a → convert → node_b

auto original = node_b->input_value(0);  // node_a's output
auto convert = std::make_shared<ov::op::v0::Convert>(original, ov::element::f32);
node_b->input(0).replace_source_output(convert->output(0));
model->validate_nodes_and_infer_types();
```

#### Pattern 3: Bypass a node

```cpp
// Before: node_a → convert → node_b
// After:  node_a → node_b

auto upstream = convert_node->input_value(0);  // node_a's output
node_b->input(0).replace_source_output(upstream);
model->validate_nodes_and_infer_types();
```

#### Pattern 4: Find and replace a pattern

```cpp
for (auto& output : model->outputs()) {
    auto result = output.get_node_shared_ptr();
    auto producer = result->input_value(0).get_node_shared_ptr();

    // Check if this output matches our pattern
    auto convert = std::dynamic_pointer_cast<ov::op::v0::Convert>(producer);
    if (!convert)
        continue;
    if (convert->input_value(0).get_element_type() != ov::element::i64)
        continue;

    // Pattern matched — build replacement and rewire
    auto replacement = build_fp32_subgraph(convert->input_value(0));
    result->input(0).replace_source_output(replacement);
}
model->validate_nodes_and_infer_types();
```

---

### Complete C++ example: Replace I64 labels with FP32 argmax

```cpp
static void fix_labels(std::shared_ptr<ov::Model>& model) {
    for (auto& output : model->outputs()) {
        auto result_node = output.get_node_shared_ptr();
        auto producer = result_node->input_value(0).get_node_shared_ptr();

        // Match: Result ← Convert(I64→FP32)
        auto convert_op = std::dynamic_pointer_cast<ov::op::v0::Convert>(producer);
        if (!convert_op)
            continue;
        if (convert_op->input_value(0).get_element_type() != ov::element::i64)
            continue;

        // Walk backward to find Gather
        auto node = convert_op->input_value(0).get_node_shared_ptr();
        while (node && node->get_type_info().name != std::string("Gather")) {
            if (node->get_input_size() == 0) break;
            node = node->input_value(0).get_node_shared_ptr();
        }
        if (!node || node->get_type_info().name != std::string("Gather"))
            continue;

        auto gather_indices = node->input_value(1);
        auto gather_axis = node->input_value(2);

        // Walk to TopK
        node = node->input_value(0).get_node_shared_ptr();
        while (node && node->get_type_info().name != std::string("TopK")) {
            if (node->get_input_size() == 0) break;
            node = node->input_value(0).get_node_shared_ptr();
        }
        if (!node || node->get_type_info().name != std::string("TopK"))
            continue;

        // TopK input → Multiply → input 0 = Sigmoid
        auto multiply = node->input_value(0).get_node_shared_ptr();
        auto sigmoid_out = multiply->input_value(0);  // [1, N, C]

        auto shape = sigmoid_out.get_partial_shape();
        auto N = shape[1].get_length();
        auto C = shape[2].get_length();

        // Reshape [1, N, C] → [N, C]
        auto shape_2d = ov::op::v0::Constant::create(
            ov::element::i32, {2},
            std::vector<int32_t>{(int32_t)N, (int32_t)C});
        auto sig_2d = std::make_shared<ov::op::v1::Reshape>(
            sigmoid_out, shape_2d, false);

        // Gather selected detections
        auto sig_gathered = std::make_shared<ov::op::v8::Gather>(
            sig_2d, gather_indices, gather_axis);

        // Reshape to [1, K, C]
        auto shape_3d = ov::op::v0::Constant::create(
            ov::element::i32, {3},
            std::vector<int32_t>{1, -1, (int32_t)C});
        auto sig_3d = std::make_shared<ov::op::v1::Reshape>(
            sig_gathered, shape_3d, false);

        // Slice helper: extract class c → [1, K, 1]
        auto slice_class = [&](ov::Output<ov::Node> data,
                               int c) -> ov::Output<ov::Node> {
            auto b = ov::op::v0::Constant::create(
                ov::element::i32, {3}, std::vector<int32_t>{0, 0, c});
            auto e = ov::op::v0::Constant::create(
                ov::element::i32, {3}, std::vector<int32_t>{0, 0, c + 1});
            auto s = ov::op::v0::Constant::create(
                ov::element::i32, {3}, std::vector<int32_t>{1, 1, 1});
            return std::make_shared<ov::op::v1::StridedSlice>(
                data, b, e, s,
                std::vector<int64_t>{1, 1, 0},
                std::vector<int64_t>{1, 1, 0});
        };

        // Iterative argmax
        auto one = ov::op::v0::Constant::create(ov::element::f32, {}, {1.0f});
        ov::Output<ov::Node> max_score = slice_class(sig_3d, 0);
        ov::Output<ov::Node> argmax = ov::op::v0::Constant::create(
            ov::element::f32, {1, 1, 1}, {0.0f});

        for (int64_t c = 1; c < C; ++c) {
            auto sc = slice_class(sig_3d, (int)c);
            auto better = std::make_shared<ov::op::v1::Greater>(sc, max_score);
            auto better_f = std::make_shared<ov::op::v0::Convert>(
                better, ov::element::f32);
            auto not_better = std::make_shared<ov::op::v1::Subtract>(one, better_f);
            auto c_val = ov::op::v0::Constant::create(
                ov::element::f32, {}, {(float)c});

            argmax = std::make_shared<ov::op::v1::Add>(
                std::make_shared<ov::op::v1::Multiply>(not_better, argmax),
                std::make_shared<ov::op::v1::Multiply>(better_f, c_val));
            max_score = std::make_shared<ov::op::v1::Maximum>(max_score, sc);
        }

        // Squeeze [1, K, 1] → [1, K]
        auto axis = ov::op::v0::Constant::create(ov::element::i32, {1}, {2});
        auto result = std::make_shared<ov::op::v0::Squeeze>(argmax, axis);

        // Rewire
        result_node->input(0).replace_source_output(result);
    }

    model->validate_nodes_and_infer_types();
}
```

Usage:
```cpp
auto model = core.read_model("model.xml");
fix_labels(model);
auto compiled = core.compile_model(model, "NPU");
```

---

## Python ↔ C++ API Correspondence

| Operation | Python | C++ |
|---|---|---|
| Read model | `core.read_model("m.xml")` | `core.read_model("m.xml")` |
| Get outputs | `model.outputs` | `model->outputs()` |
| Output name | `out.get_names()` / `out.get_any_name()` | `output.get_names()` / `output.get_any_name()` |
| Result node | `out.get_node()` | `output.get_node_shared_ptr()` |
| Input value | `node.input(i).get_source_output()` | `node->input_value(i)` |
| Upstream node | `.get_node()` | `.get_node_shared_ptr()` |
| Output port | `node.output(0)` | `node->output(0)` |
| Node type | `node.get_type_name()` | `node->get_type_info().name` |
| Node name | `node.get_friendly_name()` | `node->get_friendly_name()` |
| Shape | `port.get_partial_shape()` | `port.get_partial_shape()` |
| Element type | `port.get_element_type()` | `port.get_element_type()` |
| Static dim | `shape[i].get_length()` | `shape[i].get_length()` |
| Dynamic check | `shape[i].is_dynamic` | `shape[i].is_dynamic()` |
| Rewire | `node.input(0).replace_source_output(x.output(0))` | `node->input(0).replace_source_output(x)` |
| Validate | `model.validate_nodes_and_infer_types()` | `model->validate_nodes_and_infer_types()` |
| Constant | `ops.constant(np.array(...))` | `ov::op::v0::Constant::create(type, shape, values)` |
| Reshape | `ops.reshape(x, shape, special_zero=False)` | `std::make_shared<ov::op::v1::Reshape>(x, shape, false)` |
| Gather | `ops.gather(data, idx, axis)` | `std::make_shared<ov::op::v8::Gather>(data, idx, axis)` |
| Greater | `ops.greater(a, b)` | `std::make_shared<ov::op::v1::Greater>(a, b)` |
| Convert | `ops.convert(x, ov.Type.f32)` | `std::make_shared<ov::op::v0::Convert>(x, ov::element::f32)` |
| Add | `ops.add(a, b)` | `std::make_shared<ov::op::v1::Add>(a, b)` |
| Multiply | `ops.multiply(a, b)` | `std::make_shared<ov::op::v1::Multiply>(a, b)` |
| Subtract | `ops.subtract(a, b)` | `std::make_shared<ov::op::v1::Subtract>(a, b)` |
| Maximum | `ops.maximum(a, b)` | `std::make_shared<ov::op::v1::Maximum>(a, b)` |
| Squeeze | `ops.squeeze(x, axis)` | `std::make_shared<ov::op::v0::Squeeze>(x, axis)` |
| StridedSlice | `ops.strided_slice(d, b, e, s, ...)` | `std::make_shared<ov::op::v1::StridedSlice>(d, b, e, s, ...)` |
| Add Result | `model.add_results([ov_op.Result(x)])` | N/A (add to `model->get_results()`) |

---

## Important Notes

1. **Always call `validate_nodes_and_infer_types()`** after modifying the graph.
   This propagates shapes and types through the modified paths and catches errors
   (e.g. shape mismatches) before compilation.

2. **Tensor names may be lost** when rewiring. If downstream code relies on specific
   output names (e.g. `"labels"`), the name is bound to the output port, not the Result.
   After replacing a Result's input, the name typically stays, but verify with
   `output.get_any_name()`.

3. **Unused nodes are automatically pruned.** If you disconnect a subgraph by replacing
   its consumer's input, the orphaned nodes won't be compiled — no need to manually delete them.

4. **Modifications are in-memory only.** The original `.xml` and `.bin` files are never touched.
   Reloading with `read_model()` gives back the original graph.

5. **Prototype in Python, deploy in C++.** The Python API is faster for experimentation
   (no build step). Once the transformation works, translate to C++ for production integration.
