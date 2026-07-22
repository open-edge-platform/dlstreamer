# Zero-Shot Image Classification (CLIP)

This sample runs open-vocabulary (zero-shot) image classification with `gvaclassify`.
Instead of a model with a fixed, trained classification head, `gvaclassify` runs a CLIP
image encoder and a post-processing converter (`zeroshot_openclip`) scores the resulting
image embedding by cosine similarity against text-label embeddings supplied at runtime.

The class list lives outside the model: to change the classes you edit `labels.txt` and
regenerate the embeddings file, with no retraining and no change to the model.

## How it works

1. `gvaclassify` runs the CLIP image encoder. Its OpenVINO IR carries the CLIP
   preprocessing (mean/std, RGB, center crop) in the `model_info` section of `model.xml`,
   so no DL Streamer model-proc file is used.
2. The `zeroshot_openclip` converter L2-normalizes the image embedding, computes cosine
   similarity against the label embeddings from `labels.safetensors`, applies the CLIP
   `logit_scale` (read from the embeddings-file metadata) and a softmax, then reports top-k.
3. Setting `zeroshot-embeddings-file` on `gvaclassify` selects the converter automatically.

## Prepare the models

Use the helper scripts in `scripts/download_models` (see its README for the Python
environment). Prepare both artifacts with the **same** CLIP model so the image and text
embeddings share one space:

```bash
cd scripts/download_models
CLIP=openai/clip-vit-base-patch32

# 1) CLIP image encoder -> OpenVINO IR (projected image embedding, with model_info preprocessing)
python3 download_hf_models.py --model "$CLIP" --extra_args --zeroshot --outdir .

# 2) Text-label embeddings -> labels.safetensors (carries the CLIP logit_scale)
python3 clip_text_embeddings.py --model "$CLIP" \
        --labels <path-to>/labels.txt --output labels.safetensors
```

Optionally add `--unknown-threshold 0.2` to `clip_text_embeddings.py` to label weak
matches as `unknown` (the threshold is a top-1 cosine similarity; tune per model and label set).

Copy `clip-vit-base-patch32/` and `labels.safetensors` next to this sample, or point the
`MODEL` and `EMBEDDINGS` environment variables at them.

## Run

```bash
./zero_shot_classification.sh [INPUT] [DEVICE]
```

- `INPUT`: an image/video file or a capture URI (defaults to `images/zebra.jpg` if present).
- `DEVICE`: `CPU` (default), `GPU`, `NPU`, or e.g. `MULTI:GPU,CPU`.

For an image input the script prints JSON classification results; for video it renders an
annotated window.

## Change the classes

Edit `labels.txt` (one label per line), regenerate `labels.safetensors` with
`clip_text_embeddings.py`, and rerun. The model is unchanged.

## Notes

- The embeddings file is a single 2-D tensor `[num_labels, embedding_dim]` named
  `embeddings`, with rows aligned to `labels.txt`. Its metadata carries `logit_scale`
  (for calibrated confidences) and, optionally, `unknown_threshold`.
- Only the CLIP image encoder runs on the device; the similarity, softmax and top-k run on
  the host CPU. The model graph stays a fixed-shape vision encoder, which suits NPU execution.
