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
- `unknown_threshold` — optional; top-1 cosine similarity below which a result is labelled `unknown`.
- `model`, `labels`, `prompt` — informational.

The converter reads this format natively (no Python/PyTorch dependency at runtime). The reader is a
small, dependency-light parser (`safetensors_reader.h`) using the in-tree `nlohmann_json` for the
header. Choosing `.safetensors` over a pickled `.pth` avoids arbitrary code execution when loading a
file that, per threat model, is treated as untrusted.

## Preprocessing

CLIP requires specific normalization. The exported IR carries this in the `model_info` section of
`model.xml` (`mean_values` and `scale_values` are the CLIP mean/std x 255, `color_space=RGB`,
`resize_type=crop`); DL Streamer reads it and composes the input affine transform. No DL Streamer
model-proc file is required. The IR input keeps a fixed spatial shape `[N,3,224,224]`.

## Calibration and the unknown class

- **logit_scale.** CLIP scores are `logit_scale · cosine` (the learned temperature is ~100). Without
  it, a softmax over raw cosine in `[-1, 1]` is nearly flat and the confidences, while correctly
  ordered, are not meaningful. The converter applies `logit_scale` (from the embeddings-file
  metadata) before the softmax; if it is absent it falls back to `1.0` and warns.
- **unknown_threshold.** An optional value in the embeddings-file metadata. If the top-1 cosine
  similarity is below it, the result is labelled `unknown` (`label_id = -1`) rather than forced to
  the nearest class. Thresholding on the raw cosine keeps the decision independent of `logit_scale`.
  Omitted or negative disables the check.

## Output metadata

Each emitted classification carries the usual `label`, `label_id`, `confidence`, `rank`, plus
`zs_mode` (true), `zs_unknown` (bool), and `zs_model` (the model name).

## Tooling and sample

Model preparation reuses the Hugging Face scripts in `scripts/download_models`:
`download_hf_models.py --model <clip_id> --extra_args --zeroshot` exports the CLIP image encoder
(the projected image embedding) to OpenVINO IR with the preprocessing written into `model_info`, and
`clip_text_embeddings.py` turns `labels.txt` into `labels.safetensors` with the `logit_scale`
metadata. The end-to-end pipeline is in `samples/gstreamer/gst_launch/zero_shot_classification/`.
