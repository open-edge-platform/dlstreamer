# Model Sourcing & Conversion

Used during **planning (step 4)** of [`convert-app.prompt.md`](../../../prompts/convert-app.prompt.md).

Convert all AI models to OpenVINO IR format **before** implementing the C++
pipeline.

## Precision preference

Prefer **FP16** over FP32 for all models unless the model explicitly requires
FP32 precision (e.g. output tensors with integer indices that lose meaning under
half-precision quantization, or documented accuracy degradation). FP16 halves
memory bandwidth, enables GPU inference at full throughput on Intel GPUs (which
natively execute FP16), and reduces model load time — with negligible accuracy
loss for the vast majority of detection and classification models.

When both FP16 and FP32 variants are available (e.g. from `omz_downloader` or
HuggingFace), always select FP16. Document any exception (model kept at FP32)
with a one-line justification in the README's model substitution table.

For detailed `ovc` usage, framework-specific recipes (ONNX, PyTorch, TensorRT,
PaddlePaddle, HuggingFace), precision options (FP32 / FP16 / INT8), and
ready-to-use export script templates, follow the
[Model Preparation Reference](../references/model-preparation.md).

## Model Sourcing Strategy

The agent MUST find an Intel-compatible model for **every** model used by the
source application. Apply this strategy in order:

1. **Direct conversion** — if the original model is available in an open
   framework format (ONNX, PyTorch `.pt`, TensorFlow `.pb`, PaddlePaddle),
   convert it directly with `ovc`. Preserve original weights and architecture.

2. **Functional equivalent from a curated source** — if the original model
   cannot be converted (encrypted, proprietary, no open weights), search for a
   **functionally equivalent** pre-converted model in this priority order (skip
   any source flagged by the per-run deprecation discovery):
   1. [Hugging Face Hub](https://huggingface.co/) — use `optimum-cli export openvino`
   2. [Ultralytics models](https://github.com/ultralytics/ultralytics) — YOLO family, use `download_ultralytics_models.py`
   3. DL Streamer sample models — see `samples/download_public_models.sh`

3. **Document the substitution** — in the README, list every model used by the
   source app and the chosen Intel-compatible replacement, including:
   - Original model name + framework + task
   - Replacement model name + source URL + license
   - Functional equivalence rationale (same task, similar accuracy class, same
     input/output schema)
   - Any accuracy or capability trade-offs the user should be aware of

## Detector model — domain alignment with input content (mandatory)

Functional task match (e.g. "this is a license-plate detector") is **not
sufficient** when picking a detector model. The agent MUST also verify that
the model's **training-domain distribution** matches the input video the
converted app will actually receive. A model trained on a narrow scenario
(e.g. frontal barrier/toll-booth view) silently fails on out-of-distribution
content (parking surveillance, dashcam, traffic) — the pipeline runs, reports
detections, even draws bounding boxes for the vehicles it does find, but
recall on the secondary task collapses (typical observation: <10 % vs the
trained scenario's >80 %).

This is **not the same** as the model–inference-mode A/B test in
[`pipeline-implementation.md` §3](./pipeline-implementation.md) (which only
decides between `full-frame` and `roi-list`). Domain mismatch cannot be fixed
by toggling `inference-region` — both modes degrade together because the input
distribution itself is out-of-domain.

### Discovery procedure (apply before committing to a detector model)

1. **Read the model card / OMZ description** for any scenario-restrictive
   wording. Reject defaults if the description contains any of:
   - `"barrier"`, `"gate"`, `"toll"`, `"checkpoint"` — frontal close-up only
   - `"surveillance"`, `"ceiling"`, `"top-down"` — high mounting angle only
   - `"dashcam"`, `"in-vehicle"`, `"driver-facing"` — vehicle-mounted POV only
   - `"document"`, `"scan"` — paginated/scanned input only

   Cross-check against the input video's actual scenario. A model labelled
   e.g. `"optimized for license-plate recognition at a barrier"` is the
   wrong default for an out-of-domain input clip (e.g. a parking-lot
   surveillance recording).

2. **Check the model's input resolution vs the input video resolution.** For
   a detector with input 300×300 used full-frame on 1920×1080, every object
   smaller than ~50×50 px in the source becomes ≤ 8×8 px at the network input
   — below the minimum receptive field of most SSD/YOLO detection heads.
   Concretely: target object pixel size on the input network MUST be ≥ 16×16
   for SSD-MobileNet, ≥ 20×20 for YOLOv5/8 nano. If not, either:
   - Pick a higher-input-resolution model (e.g. YOLO at 640×640), OR
   - Switch to a true cropped-cascade in a probe (extract upstream ROI,
     up-scale, re-inject as appsrc) — document the added complexity.

3. **Run a quick recall sanity check** at a deliberately low threshold
   (≥ 0.01) on a representative input clip:
   ```bash
   gst-launch-1.0 -q filesrc location=<input> ! decodebin ! videoconvert ! \
       gvadetect model=<candidate> threshold=0.01 ! \
       gvametaconvert format=json ! \
       gvametapublish file-format=json-lines file-path=/tmp/probe.jsonl ! \
       fakesink
   # Bucket by confidence
   python3 -c "import json,collections; b=collections.Counter(); \
       [b.update([round(o['detection']['confidence'],1)]) for l in open('/tmp/probe.jsonl') \
        if l.startswith('{') for o in json.loads(l).get('objects',[]) \
        if o['detection']['label_id']==<target_class_id>]; \
       print(sorted(b.items()))"
   ```
   **Decision rule** — if ≥ 70 % of detections fall in the `[0.0, 0.1)` bucket
   AND fewer than 5 % land at `≥ 0.5`, the model is out-of-domain for this
   input. Pick a different model before continuing.

4. **Preferred fallbacks for detector-domain mismatch** (priority order):
   1. **Generic high-recall detector that already covers the target class as
      a sub-class.** For any detector whose source-app role is to find a
      broad super-category (e.g. "vehicle", "person", "animal"), the first
      fallback is a public detector trained on a large, diverse, multi-class
      dataset that includes the target class (e.g. a COCO-trained YOLO
      variant). Map the relevant fine-grained sub-classes to the source
      app's umbrella label via the `labels-file` (e.g. for a "vehicle"
      umbrella, remap COCO classes `car` / `bus` / `truck` → `vehicle`).
      This is almost always a better default than a scenario-locked detector
      whose training set was restricted to a single mounting angle / camera
      view.
   2. **Task-specific community detectors** from curated hubs (e.g. Hugging
      Face), exportable via `optimum-cli export openvino`, that were trained
      on mixed-angle / mixed-scale datasets.
   3. **Public fine-tunes** of the same architecture family on broader
      public datasets — e.g. for YOLO variants, `download_ultralytics_models.py`
      then `ovc`.
   4. **Original source-app model** if it is the only domain-matching option
      AND it is exportable to ONNX/OpenVINO — preserve the original weights
      with `ovc`, only swap the inference runtime.

   **Mandatory A/B before defaulting to a narrow-domain model** — whenever
   any of the scenario-restrictive keywords listed in step 1 of the
   *Discovery procedure* above (e.g. `barrier`, `gate`, `toll`,
   `surveillance`, `dashcam`, …) appears in the source app's chosen
   detector's name or description, the agent MUST run the confidence-bucket
   sanity check from step 3 of the *Discovery procedure* on **both** the
   source's default model AND the generic fallback from priority 1, and pick
   the higher-recall option. Record both numbers in the README's model
   substitution table.

5. **README documentation requirement** — when the chosen model's training
   domain does not fully match the input video, the README's model
   substitution table MUST include a `Domain match` column with one of
   `match` / `partial` / `mismatch (accepted)`. For `partial` and
   `mismatch (accepted)`, the Observed Output section MUST include the
   confidence-bucket histogram from step 3 and the *Conversion Notes* MUST
   list at least one suggested alternative model from step 4 that would
   close the gap. Column semantics and table layout are specified in
   [`documentation-spec.md`](./documentation-spec.md) §3.1.

## Default language for OCR / text-recognition models

For any model that performs **OCR, text recognition, license-plate recognition
(LPR), scene-text recognition, or any other character-classification task with
a language-specific character set**, the default target language is **English
(Latin alphabet, ASCII letters A–Z + digits 0–9)** — unless the user explicitly
requests a different language.

This rule overrides naïve "closest match by task" model selection. Concretely:

1. **Reject models trained on non-Latin character sets by default.** For
   example, `license-plate-recognition-barrier-0007` from Intel OMZ is trained
   on **Chinese** plates (its dictionary contains 31 Chinese province name tags
   like `<Liaoning>`, `<Hunan>` in addition to Latin letters, and the model
   architecturally expects a Chinese province prefix in the output). It will
   produce nonsense (hallucinated Chinese tags) on European, US, or other
   Latin-alphabet plates. Do NOT use it as the default LPR model just because
   it is the only Intel-curated LPR model — pick a Latin-alphabet alternative
   instead.

2. **Inspect every candidate OCR model's character dictionary before selecting
   it.** The dictionary file (whether shipped with the model, embedded in
   `model-proc`, or documented on the model's source page) is the authoritative
   source of truth about which languages/scripts the model can produce. If the
   dictionary contains non-Latin characters (CJK, Cyrillic, Arabic, Devanagari,
   etc.) and there is no way to restrict output to Latin only, pick a different
   model.

3. **Preferred sources for English/Latin OCR models** — apply the
   [*Model Sourcing Strategy*](#model-sourcing-strategy) priority list above
   (HF Hub via `optimum-cli export openvino`, then Ultralytics / DL Streamer
   sample models). For OCR additionally prefer Intel OMZ Latin-only models
   (e.g. `text-recognition-0012`, `text-recognition-0014`,
   `text-recognition-resnet-fc` — verify the dictionary excludes CJK) and
   PaddleOCR English recognizers (via `paddle2onnx` → `ovc`) over
   general-purpose hubs when available.

4. **When the user explicitly requests a different language** (e.g. "use a
   Chinese LPR model", "this is for Polish plates with diacritics"), document
   the choice in the README's model substitution table together with the
   dictionary contents and the explicit user request that justified the
   deviation from the default.

README documentation of the OCR model's character set / language is
mandatory and specified in
[`documentation-spec.md`](./documentation-spec.md) §3.1
(`Character set / language` column).

## Inference Device Mapping

When mapping inference device from the source app:

| Source                  | DL Streamer / OpenVINO |
|-------------------------|------------------------|
| `cuda` / `gpu` (NVIDIA) | `GPU` (Intel)          |
| `cpu`                   | `CPU`                  |
| —                       | `NPU` (if available)   |

For DeepStream-specific element mapping and conversion examples, see the
[Converting DeepStream to DL Streamer Guide](../../../../docs/user-guide/dev_guide/converting_deepstream_to_dlstreamer.md).
For mixed NVIDIA + Intel hardware deployments, see
[DL Streamer and DeepStream Coexistence](../../../../docs/user-guide/dev_guide/dlstreamer-deepstream-coexistence.md).

## Blockers requiring user action

Stop and ask before proceeding when:

- **Encrypted model files** (e.g. NVIDIA TAO `.etlt`, TensorRT `.engine` from
  unknown source) — first attempt to find a functional equivalent (see
  *Model Sourcing Strategy*). If no equivalent exists in any curated source,
  request the unencrypted ONNX export or original framework weights from the
  user.
- **Custom CUDA kernels or proprietary closed-source plugins without an
  OpenVINO equivalent** — document the gap and ask the user how to proceed
  (re-implement in OpenVINO / drop the feature / keep as a TODO).
