# gvaclassify zero-shot classification with OpenCLIP

## Summary

`gvaclassify` gains open-vocabulary (zero-shot) image classification. Rather than a model with a
fixed classification head, it runs a CLIP **image** encoder (vision tower + visual projection) and a
new post-processing converter, `zeroshot_openclip`, scores the resulting image embedding by cosine
similarity against precomputed **text-label embeddings**. The label set is supplied at runtime as a
`.safetensors` file, so classes can be added or changed by regenerating that file — no retraining.

## Pipeline

```
image ─▶ gvaclassify(model = CLIP image-encoder IR) ─▶ image embedding [1, D]
                                                          │
                                       zeroshot_openclip converter (host/CPU):
                                         cosine(image, class_k)  for each class
                                         softmax(logit_scale · cosine)
                                         top-k  →  {label, label_id, confidence, rank}
```

Only the CLIP vision tower runs on the inference device. The similarity, temperature scaling and
top-k are cheap host-side operations, which keeps the device graph static-shape (important for NPU).

## Selecting zero-shot mode

Either is sufficient:

- Set `gvaclassify zeroshot-embeddings-file=<labels>.safetensors`. With no `model-proc` converter
  named, the to-tensor default becomes `zeroshot_openclip` automatically.
- Name the converter explicitly in `model-proc`: `"output_postproc": [{ "converter": "zeroshot_openclip" }]`.

Element properties:

| Property | Meaning | Default |
|---|---|---|
| `zeroshot-embeddings-file` | Path to the `.safetensors` class embeddings. Presence enables zero-shot. | unset |
| `zeroshot-topk` | Number of ranked classes to attach per region. | 1 |

## Embeddings artifact (`.safetensors`)

A single 2-D tensor named `embeddings` (also accepted: `label_embeddings`, `text_embeddings`) of
shape `[num_classes, embedding_dim]`, dtype `F32` or `F16`, rows aligned to the configured labels.
Optional file metadata:

- `logit_scale` — the model's CLIP temperature (`logit_scale.exp()`), used to calibrate the softmax.
- `model`, `labels`, `prompt` — informational.

The converter reads this format natively (no Python/PyTorch dependency at runtime). The reader is a
small, dependency-light parser (`safetensors_reader.h`) using the in-tree `nlohmann_json` for the
header. Choosing `.safetensors` over a pickled `.pth` avoids arbitrary code execution when loading a
file that, per threat model, is treated as untrusted.

## Preprocessing

CLIP requires specific normalization. The sample `model_proc` declares image input precision `U8`,
`range [0,1]`, CLIP `mean`/`std`, `RGB`, aspect-ratio resize and central crop. DL Streamer composes
these into the input affine transform. The exported IR input is pinned to a static `[1,3,224,224]`.

## Calibration and the unknown class

- **logit_scale.** CLIP scores are `logit_scale · cosine` (the learned temperature is ~100). Without
  it, a softmax over raw cosine in `[-1, 1]` is nearly flat and the confidences, while correctly
  ordered, are not meaningful. The converter applies `logit_scale` (from the file metadata, or a
  `model_proc` `logit_scale` param) before the softmax; if neither is present it falls back to `1.0`
  and warns.
- **unknown_threshold.** An optional `model_proc` output param. If the top-1 cosine similarity is
  below it, the result is labelled `unknown` (`label_id = -1`) rather than forced to the nearest
  class. Thresholding on the raw cosine keeps the decision independent of `logit_scale`. Omitted or
  negative disables the check.

## Output metadata

Each emitted classification carries the usual `label`, `label_id`, `confidence`, `rank`, plus
`zs_mode` (true), `zs_unknown` (bool), and `zs_model` (the model name).

## Tooling and sample

See `samples/gstreamer/gst_launch/zero_shot_classification/`: `export_clip_vision_ov.py` (CLIP image
encoder → OpenVINO IR), `gen_label_embeddings.py` (`labels.txt` → `labels.safetensors` with
`logit_scale` metadata), a `model_proc`, and an end-to-end script.
