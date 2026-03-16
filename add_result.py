import openvino as ov
from openvino._pyopenvino import op as ov_op

core = ov.Core()
model = core.read_model("model.xml")

# Znajdź dowolny węzeł w grafie, np. Sigmoid
for node in model.get_ordered_ops():
    if node.get_type_name() == "Sigmoid":
        # Dodaj jego output jako nowy Result
        debug_result = ov_op.Result(node.output(0))
        model.add_results([debug_result])
        break

model.validate_nodes_and_infer_types()

# Teraz model ma dodatkowy output — możesz go odczytać jak każdy inny
compiled = core.compile_model(model, "CPU")  # lub "NPU"
req = compiled.create_infer_request()
req.infer({0: input_data})

# Oryginalne outputy: tensor 0 (boxes), tensor 1 (labels)
# Nowy debug output: tensor 2 (Sigmoid)
intermediate = req.get_output_tensor(2).data.copy()
print(f"Sigmoid shape: {intermediate.shape}, values: {intermediate}")