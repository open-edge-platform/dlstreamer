# DL Streamer Samples

This directory contains ready-to-run samples that demonstrate how to build video-analytics
pipelines with DL Streamer. Each sample lives in its own folder with a `README.md`
(with run instructions) and a run script.

> For the full, browsable catalog — descriptions, elements/models used, and preview images —
> see the [Samples reference page](../docs/user-guide/samples.md) in the documentation.

## Structure

```
samples/
├── gstreamer/
│   ├── gst_launch/       # gst-launch-1.0 command-line samples (CLI)
│   ├── cpp/              # C++ samples
│   ├── python/           # Python samples
│   ├── benchmark/        # Benchmark sample
│   └── e2e_performance/  # DL Streamer vs. OpenCV+OpenVINO benchmark
├── auto_generated_samples/  # End-to-end apps built by the DL Streamer Coding Agent
└── windows/               # Windows variants (PowerShell/.bat scripts)
```

## Before you start

- Install DL Streamer first — see the [Get Started](../docs/user-guide/system_requirements.md) guide.
- Download the models used by samples with the conversion scripts under
  [scripts/download_models](../scripts/download_models).
- Samples with C/C++ code provide a `build_and_run.sh`; other samples provide a `.sh`
  (or, on Windows, `.bat`/PowerShell) script that builds and runs the pipeline.

## Finding a sample

Browse the [Samples reference page](../docs/user-guide/samples.md) for the full list grouped
by use case (detection/classification/segmentation, tracking, VLM & GenAI, audio, 3D LiDAR/radar,
cameras, metadata, customization, benchmarking, interoperability), or open a category folder
above and read its samples' individual `README.md` files directly.
