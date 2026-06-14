# Zero-shot classification with OpenCLIP (gvaclassify)

This sample adds open-vocabulary image classification to `gvaclassify`. Instead of a fixed
softmax head, `gvaclassify` runs a CLIP **image** encoder (vision tower + visual projection) and
the `zeroshot_openclip` converter scores the resulting image embedding by cosine similarity against
**text-label embeddings** computed once and stored in a `.safetensors` file. Because the label set
lives outside the model, classes are changed by regenerating the embeddings file — no retraining,
and the model graph stays exactly the vision tower (so it stays static-shape friendly for NPU).

## How it works

```
image ─▶ gvaclassify(model = CLIP vision IR) ─▶ image embedding ─▶ zeroshot_openclip converter
                                                                     │  cosine vs. label embeddings
                                                                     │  softmax(logit_scale · cos)
                                                                     ▼
                                                          top-k {label, label_id, confidence}
```

The converter loads the embeddings natively from `.safetensors` (no Python/PyTorch at runtime) and
applies the CLIP `logit_scale` stored in the file metadata so confidences are calibrated.

## 1. Build the artifacts (one-time)

In a Python virtual environment with `open_clip_torch`, `openvino`, and `safetensors`:

```bash
python3 tools/export_clip_vision_ov.py --model ViT-B-32 --pretrained openai --out clip_vision.xml
python3 tools/gen_label_embeddings.py  --model ViT-B-32 --pretrained openai \
        --labels labels.txt --out labels.safetensors
```

Use the **same** `--model`/`--pretrained` for both so the image and text towers share one
embedding space. `gen_label_embeddings.py` writes the model's `logit_scale` into the file metadata.

## 2. Run

```bash
./zero_shot_classification.sh images/zebra.jpg          # CPU, image -> JSON
./zero_shot_classification.sh rtsp://... GPU            # video -> on-screen labels
```

Edit `labels.txt` to change the classes, then regenerate `labels.safetensors` (the IR and
`model_proc` stay the same).

## Files

- `model_proc/clip_zeroshot.json` — selects the `zeroshot_openclip` converter and applies CLIP
  preprocessing (U8 input, range `[0,1]`, CLIP mean/std, RGB, aspect-ratio resize + central crop).
- `labels.txt` — one class per line; row order must match the embeddings file.
- `tools/export_clip_vision_ov.py` — CLIP image encoder → OpenVINO IR (static `[1,3,224,224]`).
- `tools/gen_label_embeddings.py` — `labels.txt` → `labels.safetensors` (+ `logit_scale` metadata).
- `zero_shot_classification.sh` — end-to-end pipeline.

## Relevant `gvaclassify` properties

- `zeroshot-embeddings-file` — path to `labels.safetensors`. Setting it selects zero-shot mode.
- `zeroshot-topk` — number of ranked classes to attach (default 1).

## Optional `model_proc` output params

- `unknown_threshold` (double): if the top-1 cosine similarity is below this value, the result is
  labelled `unknown` (`label_id = -1`) instead of being forced to the nearest class. Omitted ⇒ off.
- `logit_scale` (double): provides/overrides the softmax temperature if the embeddings file has none.

## Notes

- The `.safetensors` tensor holding the embeddings should be named `embeddings` (also accepts
  `label_embeddings` / `text_embeddings`), shape `[num_classes, embedding_dim]`, dtype `F32` or `F16`.
- Output metadata adds `zs_mode`, `zs_unknown`, and `zs_model` alongside `label`/`label_id`/
  `confidence`/`rank`.
- Larger CLIP models (e.g. `ViT-H-14-378-quickgelu` / DFN5B) improve accuracy at higher cost; pass
  them via `--model`/`--pretrained` to both tools.
