# Release Notes: Deep Learning Streamer (DL Streamer) Pipeline Framework

## Version 2026.3.0

### Overview

DL Streamer 2026.3 broadens 3D perception, makes generative AI pipelines more efficient, and strengthens Windows and NPU acceleration. The release adds six new elements, enables NPU zero-copy by default on Intel® Core™ Ultra Series 3, adds Ubuntu 26.04 support, and removes the Architecture 2.0 elements.

### New

- New `gvamono3d` element: monocular 3D object detection (for example, MonoDETR) from a single calibrated camera image.
- New `g3dbevfusion` element: BEVFusion camera + LiDAR 3D object detection on a `gvastreammux` batch of six surround-view cameras and one LiDAR.
- New `gvatagger` element: tags H.264/H.265 access units with motion and keyframe metadata from NAL unit sizes only, so downstream inference can skip quiet frames.
- New `gvaeventrecorder`, `gvaframeselect` and `gvaroiscale` elements: event-triggered clip recording and frame/ROI preparation for Vision Language Models, with samples and unit tests.
- VLM model sharing between `gvagenai` instances (initial implementation), reducing memory footprint in multi-stream pipelines.
- `gvagenai` frame selection based on detected object class, and per-request performance posted as a bus message.
- NPU zero-copy (DMA-BUF) processing with strided tensor support, enabled by default on Intel® Core™ Ultra Series 3.
- Windows: D3D12 support with D3D11/D3D12 surface-sharing pre-processing backends, and GPU rendering for `gvawatermark`.
- New model support: D-FINE (HuggingFace and Geti), Geti RF-DETR-Seg instance segmentation, OpenVINO HuggingFace YOLO models, and zero-shot OpenCLIP classification in `gvaclassify`.
- Optimizer: data-based heuristic.
- `gvaanalytics`: `track-timeout` property to prevent unbounded tracking-state growth.
- `latency_tracer`: structured interval windows posted on the bus.
- Runtime configuration toggling.
- Option to pass a dataset YAML for INT8 calibration.
- Configurable EOS frame limit in GStreamer samples, a new DeepSORT sample, and preview images for sample applications.
- Added support for Ubuntu 26.04 LTS, with Panther Lake (Intel® Core™ Ultra Series 3) in CI.
- C++/Python unit tests now run on Windows.

### Improved

- `gvawatermark`: GPU render path draws directly into NV12 VA surfaces; contrast text color, custom color tables, and bounding box color derived from object label.
- `gvatrack` (DeepSORT): re-ID features read from analytics metadata tensors, per-instance frame counter, and tensors carried forward for non-inference frames.
- `g3dinference` processes frames asynchronously with a configurable `nireq` worker pool.
- `g3dlidarsrc` uses frame capture time for PTS when `ntp-sync=false`; `g3drender` clips perspective box edges at the near plane.
- Optimizer uses Metrics Manager 2026.3 power metric names and reports more detail about the optimization process.
- Unified `CENTRAL_RESIZE` crop geometry across pre-processing backends.
- Model download scripts migrated from shell to Python, models pinned to specific commits, deprecated scripts removed.
- Python package management switched from pip to uv.
- Windows installer can install GStreamer development files; Windows samples updated for D3D12.
- Added instructions for NPU DMA-BUF access in Docker containers.
- Removed the Architecture 2.0 elements and samples.
- Updated GStreamer to 1.28.8, OpenVINO to 2026.x, and NPU and GPU drivers.
- Restructured samples documentation and refreshed element and installation docs.
- CI: Trivy image scans, element sanity checks, re-run of failed jobs only; Tiger Lake and Ubuntu 22.04 removed from CI test workflows.

### Fixed

- Fixed several `gvastreammux` / `gvastreamdemux` issues, including state initialization under lock.
- Fixed `gvawatermark` segmentation mask dimensions, `show-roi`/`hide-roi` handling with `show-labels=false`, missing FPS text on frames without objects, and NV12 color conversion on Windows.
- Fixed tracker confidence for non-inference frames.
- Fixed RF-DETR output for models with fewer than 5 classes, GETI ResNet34 support, PaddleOCR padding, YOLO YAML parsing, and model reshaping with a user-provided shape.
- Fixed D3D11 `inference-region=1`.
- Fixed `g3dbevfusion` with OpenVINO 2026.5 and the Smart NVR Python sample on Ubuntu 26.04.
- Fixed Coverity findings across components and Trivy-reported container vulnerabilities.
- Masked sensitive environment variables in the dump report and added URL validation to ONVIF.

### Known Issues

- Automatic downloading of VLM models is disabled; download models before running VLM pipelines.
- On Meteor Lake and Arrow Lake iGPUs with the i915 driver, `gvawatermark` uses the CPU renderer.
- NPU zero-copy is disabled by default on platforms other than Intel® Core™ Ultra Series 3.

## Highlights:

- Broader 3D perception: monocular 3D detection (`gvamono3d`) and camera + LiDAR BEVFusion (`g3dbevfusion`).
- Event-driven GenAI: new elements record event clips and prepare frames and regions for Vision Language Models; VLM model sharing reduces memory use.
- Compute savings at the source: `gvatagger` detects motion in compressed video so inference can be skipped on quiet frames.
- NPU zero-copy enabled by default on Intel® Core™ Ultra Series 3.
- Stronger Windows support: D3D12, GPU-accelerated overlays and unit tests on Windows.
- Ubuntu 26.04 LTS support.

<!--hide_directive
:::{toctree}
:hidden:

release-notes/release-notes-2026
release-notes/release-notes-2025
release-notes/release-notes-2024

:::
hide_directive-->