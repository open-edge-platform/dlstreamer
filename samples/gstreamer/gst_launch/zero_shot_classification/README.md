# Zero-Shot Classification (CLIP)

This sample runs open-vocabulary (zero-shot) classification with `gvaclassify`.
Instead of a model with a fixed, trained classification head, `gvaclassify` runs a CLIP
image encoder and a post-processing converter (`clip_zeroshot`) scores the resulting
image embedding by cosine similarity against text-label embeddings supplied at runtime.

For images, CLIP classifies the full frame. For video, `gvadetect` first detects faces
with [arnabdhar/YOLOv8-Face-Detection](https://huggingface.co/arnabdhar/YOLOv8-Face-Detection),
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

Use the helper scripts in `scripts/download_models` (see its README for the Python
environment). Prepare both artifacts with the **same** CLIP model so the image and text
embeddings share one space:

```bash
cd scripts/download_models
CLIP=openai/clip-vit-base-patch32

# 1) CLIP image encoder -> OpenVINO IR (projected image embedding, with model_info preprocessing)
python3 download_hf_models.py --model "$CLIP" --export-variant clip-zeroshot --outdir .

# 2) Text-label embeddings -> labels.safetensors (carries the CLIP logit_scale)
python3 clip_text_embeddings.py --model "$CLIP" \
        --labels <path-to>/labels.txt --output labels.safetensors
```

The downloader writes the model XML to
`openai_clip-vit-base-patch32/FP16/openai_clip-vit-base-patch32.xml` under `--outdir`.

Video input additionally requires the YOLOv8 face detector. Export it to the default model path:

```bash
export MODELS_PATH="$HOME/models"
python3 download_ultralytics_models.py --model arnabdhar/YOLOv8-Face-Detection \
   --outdir "${MODELS_PATH}/public/arnabdhar_YOLOv8-Face-Detection/FP16" --half
```

Optionally add `--unknown-threshold 0.2` to `clip_text_embeddings.py` to label weak
matches as `unknown` (the threshold is a top-1 cosine similarity; tune per model and label set).

Copy `openai_clip-vit-base-patch32/` and `labels.safetensors` next to this sample, or set `MODEL`
to the exported `.xml` file and `EMBEDDINGS` to the generated `.safetensors` file:

```bash
export MODEL=/path/to/openai_clip-vit-base-patch32/FP16/openai_clip-vit-base-patch32.xml
export EMBEDDINGS=/path/to/labels.safetensors
```

For video, the detector defaults to
`${MODELS_PATH}/public/arnabdhar_YOLOv8-Face-Detection/FP16/arnabdhar_YOLOv8-Face-Detection.xml`.
Set `DETECTION_MODEL` to use another face detector XML location. The detector is not required
for image input.

## Run

```bash
./zero_shot_classification.sh [INPUT] [DEVICE] [OUTPUT]
```

- `INPUT`: an image or video path or URI. It defaults to
   [People Giving a Thumbs Up](https://www.pexels.com/video/people-giving-a-thumbs-up-7504884/),
   a video by Moe Magners via Pexels.
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
