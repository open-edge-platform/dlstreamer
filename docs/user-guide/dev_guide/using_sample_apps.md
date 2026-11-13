# Using Sample Apps

DL Streamer ships with **40+ ready-to-run samples** that turn common media-analytics
tasks into working pipelines you can launch in minutes. They are the fastest way to
see an element or a full use case in action, and a great starting point to copy from
when building your own application.

Each sample lives in its own folder with a `README.md` and a run script. Browse them
online in the
[samples directory on GitHub](https://github.com/open-edge-platform/dlstreamer/tree/main/samples)
or, after installation, under `/opt/intel/dlstreamer/samples`.

> **Looking for a specific element?** See the [Elements](../elements/elements.md) page —
> most elements are demonstrated by one or more of the samples listed in the
> [Available Sample Apps](./sample_apps_index.md).

## How to run

- Install DL Streamer first — see the [Get Started](../system_requirements.md) guide.
- When a sample requires a model, prepare a compatible model before running it. The models listed
  in the sample index or README may be examples or defaults, not the only supported choices.
  General conversion helpers are available under
  [scripts/download_models](https://github.com/open-edge-platform/dlstreamer/tree/main/scripts/download_models).
  They export models from Hugging Face, Ultralytics, TIMM and other sources to OpenVINO IR:
  - `download_hf_models.py` — Hugging Face models (VLMs, CLIP, Whisper, …).
  - `download_ultralytics_models.py` — Ultralytics YOLO models.
  - `download_timm_models.py` — TIMM image-classification models.
  - `download_other_models.py` — other helper models (e.g. `centerface`, `hsemotion`, `deeplabv3`, `mars-small128`).

  See the [download_models README](https://github.com/open-edge-platform/dlstreamer/tree/main/scripts/download_models)
  for prerequisites, per-script requirements files and usage examples. Some specialized models
  and datasets use their own preparation steps. Each sample's `README.md` explains its model
  requirements, example models, compatible alternatives, and expected file layout (see the
  **Models** column in the [Available Sample Apps](./sample_apps_index.md)).
- Samples with C/C++ code provide a `build_and_run.sh`; other samples provide a `.sh` script that builds and runs a `gst-launch-1.0` or Python command line.

> **Platform support:** Samples target **Linux (Ubuntu 22.04/24.04)** by default. A subset also ships a
> **Windows** variant (PowerShell scripts, D3D11/`ksvideosrc` backends). See the
> [Windows samples folder](https://github.com/open-edge-platform/dlstreamer/tree/main/samples/windows).

## Next step

Browse the [Available Sample Apps](./sample_apps_index.md) to find a sample by use case, key elements, or models.
