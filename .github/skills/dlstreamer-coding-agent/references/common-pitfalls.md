# Common Pitfalls Reference

Known failure modes encountered during DLStreamer application development. Read this
before writing code to avoid wasting time on issues that have known solutions.

---

## Pitfall 1: Assuming HuggingFace Model Filenames

**Symptom:** `404 Not Found` when downloading model weights, or `FileNotFoundError`.

**Cause:** Guessing filenames like `best.pt`, `model.pt`, or `model.pdmodel` without
checking what actually exists in the repository.

**Fix:** Always list repository contents before writing download code:

```python
from huggingface_hub import list_repo_files
files = list_repo_files("owner/model-name")
for f in files:
    print(f)
```

**Example:** The repo `morsetechlab/yolov11-license-plate-detection` does NOT contain
`best.pt` — it has `license-plate-finetune-v1s.pt`, `license-plate-finetune-v1m.pt`, etc.

---

## Pitfall 2: Using `ovc` Directly on PaddlePaddle PIR Models

**Symptom:** `ovc inference.json` fails with `Cannot recognize input model`.

**Cause:** PaddlePaddle v3+ uses PIR format (`.json` + `.pdiparams`), not the legacy
`.pdmodel` format. `ovc` cannot read PIR directly.

**Fix:** Use the two-step conversion: `paddle2onnx` → `ovc`:

```bash
# Step 1: PIR → ONNX
paddle2onnx --model_dir ./model_dir \
    --model_filename inference.json \
    --params_filename inference.pdiparams \
    --save_file model.onnx --opset_version 14

# Step 2: ONNX → OpenVINO IR
ovc model.onnx --output_model model.xml
```

**Requirements:** `pip install paddlepaddle paddle2onnx`

---

## Pitfall 3: Accessing `.shape` on Dynamic OpenVINO Models

**Symptom:** `RuntimeError: to_shape was called on a dynamic shape` when accessing
`compiled_model.input(0).shape`.

**Cause:** Models converted from PaddlePaddle or ONNX often have dynamic dimensions
(e.g. `[?,3,48,?]` for OCR). The `.shape` property requires static dimensions.

**Fix:** Use `partial_shape` to check, then `model.reshape()` to set static dims
**before** compilation:

```python
core = ov.Core()
model = core.read_model("model.xml")

input_info = model.input(0)
if input_info.partial_shape.is_dynamic:
    # Set static shape: [batch, channels, height, width]
    model.reshape({input_info.any_name: [1, 3, 48, 320]})

compiled = core.compile_model(model, "CPU")
```

**Key rule:** Check `partial_shape.is_dynamic` BEFORE calling `core.compile_model()`.
Reshaping after compilation has no effect.

---

## Pitfall 4: Custom Element Cannot Access Frame Pixels

**Symptom:** `buffer.map(Gst.MapFlags.READ)` returns `False`, or mapped data is
nonsensical garbage (wrong stride, GPU memory pointers).

**Cause:** Frames are in GPU memory (VA surfaces) or an unsupported YUV format.
Custom Python elements cannot directly access VA memory.

**Fix:** Insert a format conversion element BEFORE your custom element:

```
gvadetect ... ! queue !
videoconvertscale ! video/x-raw,format=BGRx !
my_custom_element_py ! ...
```

**Key rule:** If your custom element reads pixels, it needs `video/x-raw,format=BGRx`
(or another CPU-accessible format) on its input. If it only reads metadata (bounding
boxes, labels), keep `Gst.Caps.new_any()` and skip the conversion.

---

## Pitfall 5: GStreamer Registry Cache Doesn't Pick Up Element Changes

**Symptom:** After editing `plugins/python/my_element.py`, the old version still runs.
Or a new element is not found after adding the file.

**Cause:** GStreamer caches plugin information in a binary registry file.

**Fix:** Delete the registry cache and restart:

```bash
rm ~/.cache/gstreamer-1.0/registry.*.bin
```

---

## Pitfall 6: Ultralytics Export Output Directory Name Varies

**Symptom:** Model export succeeds but the code can't find the `.xml` file at the
expected path.

**Cause:** Ultralytics names the output directory based on the input `.pt` filename and
export options. For example:
- `yolo11n.pt` + `int8=True` → `yolo11n_int8_openvino_model/yolo11n.xml`
- `best.pt` + `int8=True` → `best_int8_openvino_model/best.xml`
- `license-plate-finetune-v1s.pt` → `license-plate-finetune-v1s_int8_openvino_model/...`

**Fix:** After export, search for `.xml` files rather than hardcoding paths:

```python
xml_files = list(model_dir.glob("*.xml"))
if xml_files:
    model_path = xml_files[0]
```

Or use the shared download script (`scripts/download_models/download_ultralytics_models.py`)
which normalizes the output location.

---

## Pitfall 7: PaddleOCR Character Dictionary Not in a `.txt` File

**Symptom:** Looking for `ppocr_keys_v1.txt` or `en_dict.txt` in the model download
directory, but finding nothing.

**Cause:** PP-OCRv5 models embed the character dictionary directly in `config.json`
under `PostProcess.character_dict`, as a JSON array of 18383 unicode characters.

**Fix:** Extract from `config.json`:

```python
import json
with open("model_dir/config.json") as f:
    config = json.load(f)
chars = config["PostProcess"]["character_dict"]
# Write to text file for your element:
with open("dict.txt", "w") as f:
    f.write("\n".join(chars) + "\n")
```

---

## Pitfall 8: Low OCR Confidence Despite Correct Text

**Symptom:** OCR reads `9MRM624` (correct) but reports confidence `0.0001`.

**Cause:** The model's CTC output has 18385 classes (18383 characters + blank + special).
Softmax distributes probability across all classes, so per-character probabilities are
naturally very small (even when the argmax is correct).

**Fix:** This is expected behavior for large-vocabulary CTC models. The argmax decoded
text is reliable even when raw softmax confidences appear low. If you need a more
meaningful confidence metric, consider using the logit values directly (pre-softmax)
rather than post-softmax probabilities.

---

## Pitfall 9: Subprocess Model Export Fails Silently

**Symptom:** `subprocess.run(...)` returns non-zero exit code but no useful error
message in the main process.

**Cause:** Using `check=False` (correct for non-critical fallbacks) but not capturing
or reporting stderr.

**Fix:** Capture output for debugging:

```python
result = subprocess.run(
    [sys.executable, "-c", export_script],
    check=False,
    capture_output=True,
    text=True,
)
if result.returncode != 0:
    print(f"STDERR: {result.stderr}")
    raise RuntimeError(f"Export failed: {result.stderr[-500:]}")
```
