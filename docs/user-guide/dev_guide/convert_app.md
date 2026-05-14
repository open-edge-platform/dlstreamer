# Application Conversion Prompt (`convert-app`)


## Overview

The [`convert-app`](../../../.github/prompts/convert-app.prompt.md) prompt is an automated workflow for converting existing applications into native Intel® DL Streamer / OpenVINO™ C++ applications. It guides users through all required steps, from project scaffolding to final compliance audit, ensuring best practices and reproducibility.

This workflow is a good fit when you want to migrate an NVIDIA DeepStream application, port a legacy GStreamer AI pipeline, preserve the functional stages of an existing app, or generate reproducible build, run, and documentation deliverables.

## How It Works

The prompt orchestrates the conversion process using a sequence of well-defined steps, each referencing a dedicated instruction file. The workflow ensures reproducibility, best practices, and traceability for every conversion. All steps and requirements are documented in `.github/prompts/convert-app.prompt.md` and the `.github/prompts/convert-app/` directory.


### Workflow Steps

| Stage                                 | Description                                                                                   |
|---------------------------------------|-----------------------------------------------------------------------------------------------|
| Scaffold project and environment      | Create a new project structure, output directory, and set up the build/runtime environment.   |
| Build deprecation deny-list           | Scan for deprecated APIs and build a deny-list to avoid unsupported or obsolete constructs.   |
| Inventory source app functional blocks| Analyze the source application and list all functional blocks and pipeline stages.            |
| Plan model and pipeline mapping       | Plan model substitutions and map each functional block to DL Streamer/OpenVINO pipeline elements. |
| Implement pipeline and logic          | Implement the pipeline, probes, encoders, and ensure all elements are available and correct.  |
| Validate correctness and outputs      | Run validation (clean-shell runs, auto-fix loop) to ensure the converted app works as intended. |
| Document the conversion               | Generate a detailed README and documentation for the converted application.                   |
| Final compliance audit                | Perform a final audit to ensure all requirements and deliverables are met.                    |

## What Shapes the Converted Application

The form and structure of the converted application are not fixed — they emerge
from the interplay of several factors that the workflow analyses and resolves
automatically. Understanding these factors helps set realistic expectations
before starting a conversion.

### AI model and its context

The choice of AI model is one of the most influential factors:

- **Model architecture** (SSD, YOLO, ResNet, …) determines which DL Streamer
  inference elements are used (`gvadetect`, `gvaclassify`) and how the
  pipeline stages are chained.
- **Inference mode** (`full-frame` vs `roi-list`) affects whether a
  single-pass or cascaded detection approach is used — this choice is made
  per-model based on object scale and density.
- **Precision** (FP16 / FP32 / INT8) influences runtime performance and
  the target inference device selection.
- **Training domain** of the model (e.g. barrier/toll-booth vs. open-road
  surveillance) determines whether a direct model reuse is valid or a
  domain-matching substitute must be found.
- **Character set / language** for OCR and text-recognition models constrains
  which model is selected (e.g. Latin-alphabet vs. CJK character sets).

### Architecture of the source application

The source app's pipeline structure is preserved 1-to-1 in the conversion:

- **Number and order of inference stages** (PGIE, SGIE, secondary
  classifiers) maps directly to the number of DL Streamer elements.
- **Presence of object tracking** determines whether `gvatrack` is included
  and which tracking mode is selected.
- **Visualization and metadata output** requirements determine the rendering
  and publishing elements (`gvawatermark`, `gvametaconvert`,
  `gvametapublish`).

### Target execution environment

Runtime characteristics of the deployment machine affect the output:

- **Available inference device** (Intel iGPU / dGPU / CPU / NPU) determines
  the `device=` parameter and whether hardware video encoding is available.
- **Display availability** (headless vs. GUI) determines whether
  `autovideosink`, `filesink`, or `fakesink` is used as the output sink.
- **Operating system and driver stack** may require environment workarounds
  (e.g. GStreamer registry cache rebuild, Python plugin compatibility).

### Input and output format requirements

- **Input source type** (local file, USB camera, RTSP stream) selects the
  appropriate GStreamer source element.
- **Required output format** (annotated video, JSON metadata, CSV log)
  determines the metadata publishing and sink configuration.

> **In short:** the converted application is a direct function of the source
> app's pipeline, the models available for the target platform, and the
> execution environment. The workflow resolves all these factors automatically
> and documents every decision in the generated README under
> **Conversion Notes**.

## Example Usage

While the prompt is primarily designed for use with an agent or automation, a typical conversion workflow may be initiated as follows (pseudo-command):

```bash
# Example: Run the convert-app prompt to convert an application
/convert-app <source-application-repository-url>
```

Replace `<source-application-repository-url>` with the repository you wish to convert. The agent will guide you through each step, generate the output project, and produce a detailed README for the converted application.

Typical invocations:

```text
/convert-app /workspace/deepstream_lpr_app
```

```text
/convert-app https://github.com/NVIDIA-AI-IOT/deepstream_reference_apps/tree/master/legacy_apps/deepstream-app
```

```text
/convert-app /workspace/my_ds_app --output-name my_ds_app_dls --device GPU
```

In free-form chat, you can also use:

```text
Use DL Streamer Coding Agent from https://github.com/open-edge-platform/dlstreamer

Convert application at /workspace/deepstream_lpr_app to a native DL Streamer and OpenVINO C++ project.
Preserve all inference stages, generate CMakeLists.txt, run.sh, README.md, and model export scripts if needed.
```

**Note:**
The `/convert-app` command is a shorthand for invoking the application conversion workflow. It is equivalent to running the full prompt with the source repository as an argument. Make sure your environment supports this command or use the full prompt invocation if needed.

For full details, see [convert-app.prompt.md](../../../.github/prompts/convert-app.prompt.md).

If you are converting a DeepStream-based application, see also [Converting NVIDIA DeepStream Pipelines to Deep Learning Streamer Pipeline Framework](./converting_deepstream_to_dlstreamer.md) for framework-level mapping guidance and migration context.
