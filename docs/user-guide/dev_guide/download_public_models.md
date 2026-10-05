# Download and Convert Models

The recommended way to obtain models for Deep Learning Streamer is to use the
standalone conversion scripts in
[`scripts/download_models`](https://github.com/open-edge-platform/dlstreamer/tree/main/scripts/download_models).
They download models from their original sources, convert them to OpenVINO IR,
and save the resulting files in the requested output directory.

For the complete setup, command reference, supported options, and examples, see
the [Model Conversion Scripts README](https://github.com/open-edge-platform/dlstreamer/blob/main/scripts/download_models/README.md).

## Available Scripts

- `download_hf_models.py` converts supported Hugging Face models with
	`optimum-cli`. It also provides custom conversion paths for selected models,
	including CLIP and RT-DETR.
- `download_ultralytics_models.py` converts Ultralytics models, including YOLO
	detection, segmentation, pose, OBB, classification, and YOLOE models. It
	supports model names, local `.pt` files, and Hugging Face repositories.
- `download_timm_models.py` converts supported TIMM image-classification
	models hosted on Hugging Face.
- `download_other_models.sh` downloads and converts selected helper models that
	are not handled by the other scripts, including `yolox-tiny`, `yolox_s`, and
	`yolov7`.

## Reproducible Downloads

Model references can include an `@...` suffix to pin the source version:

- Hugging Face and TIMM use `repo_id@revision`, typically a commit SHA.
- Ultralytics uses `model.pt@tag`, where `tag` is an `ultralytics/assets`
	GitHub release tag.
- `download_other_models.sh` uses sources and tool versions defined in the
	script and does not support per-model version suffixes.

Without a pinned revision, the Hugging Face, TIMM, and Ultralytics scripts may
resolve the latest available model at runtime.

## Model Usage

After conversion, use the generated OpenVINO `.xml` file with the appropriate
Deep Learning Streamer inference element. The matching `.bin` file must remain
in the same directory. See the
[Supported Models](../supported_models.md) table and the
[GStreamer samples](https://github.com/open-edge-platform/dlstreamer/tree/main/samples/gstreamer/gst_launch)
for model-specific elements and pipeline examples.
