# Zero-Shot Classification (CLIP)

This sample runs open-vocabulary (zero-shot) classification with `gvaclassify`.
Instead of a model with a fixed, trained classification head, `gvaclassify` runs a CLIP
image encoder and a post-processing converter (`clip_zeroshot`) scores the resulting
image embedding by cosine similarity against text-label embeddings supplied at runtime.

For images, CLIP classifies the full frame. For video, `gvadetect` first detects faces
with a compatible face-detection model supplied through `DETECTION_MODEL`,
and CLIP then classifies every detected face ROI.

The class list lives outside the model: to change the classes you edit `labels.txt` and
regenerate the embeddings file, with no retraining and no change to the model.

## How it works

1. For image input, `gvaclassify` uses `inference-region=full-frame`. For video input,
   `gvadetect` runs YOLOv8-Face-Detection on each frame and `gvaclassify` uses
   `inference-region=roi-list` to run CLIP on each detected face.
2. `gvaclassify` runs the CLIP image encoder. Its OpenVINO IR carries `model_type=clip_zeroshot`
   and the CLIP preprocessing (mean/std, RGB, center crop) in the `model_info` section of
   `model.xml`, so no DL Streamer model-proc file is used.
3. `model_type=clip_zeroshot` selects the `clip_zeroshot` converter. It L2-normalizes the image
   embedding, computes cosine similarity against the label embeddings from `labels.safetensors`,
   applies the CLIP `logit_scale` and a softmax, then reports top-k. The embeddings file is parsed
   once in the post-processor setup, so the converter itself does no file I/O.
4. `zeroshot-embeddings-file` **supplies** the class bank; it does not select the converter.
   Supplying it with a non-zero-shot model (or omitting it for a `clip_zeroshot` model) is an
   error, so a `clip_token` model exported for image-to-image use can never be silently compared
   against text embeddings.

## Prepare the models

Prepare a compatible CLIP image encoder (exported with the `clip-zeroshot` variant) and its
text-label embeddings with the general helpers in
[`scripts/download_models`](../../../../scripts/download_models/README.md#1-hugging-face-conversion)
(`download_hf_models.py` and `clip_text_embeddings.py`). Both artifacts must come from the same
CLIP model so the image and text embeddings share one space.

Video input additionally needs a compatible face-detection model, prepared with the same general
helpers. Image input does not require this detector.

The models used to validate this sample are listed in [Validation](#validation).

Optionally add `--unknown-threshold 0.2` to `clip_text_embeddings.py` to label weak
matches as `unknown` (the threshold is a top-1 cosine similarity; tune per model and label set).

Place the prepared model and `labels.safetensors` next to this sample, or set `MODEL`
to the exported `.xml` file and `EMBEDDINGS` to the generated `.safetensors` file:

```bash
export MODEL=/path/to/openai_clip-vit-base-patch32/FP16/openai_clip-vit-base-patch32.xml
export EMBEDDINGS=/path/to/labels.safetensors
```

For video, set `DETECTION_MODEL` to the prepared face-detector XML. The detector is not required
for image input.

## Run

```bash
./zero_shot_classification.sh [INPUT] [DEVICE] [OUTPUT]
```

- `INPUT`: required image or video path/URI. Use any input supported by GStreamer; the sample has no bundled or default media.
- `DEVICE`: device used for both YOLOv8-Face-Detection and CLIP: `CPU`, `GPU` (default), `NPU`, or
   e.g. `MULTI:GPU,CPU`.
- `OUTPUT`: video output mode: `display` (default), `file`, `fps`, `json`, or `display-and-json`.
   The `file` mode writes an annotated H.264 video to `output.mp4` in the current directory; `fps`
   runs headlessly with `gvafpscounter` and `fakesink`; JSON modes write detections and
   classifications to `output.json` in the current directory.

For image input, the script always prints full-frame classification results as JSON. For video,
the selected output includes face detections and per-face CLIP classifications where applicable.

## Change the classes

Edit `labels.txt` (one label per line), regenerate `labels.safetensors` with
`clip_text_embeddings.py`, and rerun. The model is unchanged.

## Notes

- The embeddings file is a single 2-D tensor `[num_labels, embedding_dim]` named
  `embeddings`, with rows aligned to `labels.txt`. Its metadata carries `logit_scale`
  (for calibrated confidences) and, optionally, `unknown_threshold`.
- Re-export the model if you prepared it before `clip_zeroshot` existed: an IR still tagged
  `model_type=clip_token` is now rejected when paired with an embeddings file.
- Only the CLIP image encoder runs on the device; the similarity, softmax and top-k run on
  the host CPU. The model graph stays a fixed-shape vision encoder, which suits NPU execution.

## Validation

This sample was validated with:

- Models: `openai_clip-vit-base-patch32`, `arnabdhar_YOLOv8-Face-Detection` (FP16)
- Video: [`7504884-hd_1280_720_25fps.mp4`](https://videos.pexels.com/video-files/7504884/7504884-hd_1280_720_25fps.mp4)
