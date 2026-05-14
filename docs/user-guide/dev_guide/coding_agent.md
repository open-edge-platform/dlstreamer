# Coding Agent (Preview)

The DL Streamer Coding Agent is an AI-powered assistant that helps you build DL Streamer video analytics applications quickly from natural language descriptions. It generates complete, ready-to-run projects — including model download scripts, application code, and documentation.

> **Note:** This feature is in **Preview** stage. Please share your feedback to help us improve it.

## Prerequisites

### Development Machine

You need a development machine with one of the following coding environments installed:

- [Visual Studio Code](https://code.visualstudio.com/) with the [GitHub Copilot Chat](https://marketplace.visualstudio.com/items?itemName=GitHub.copilot-chat) extension
- [Cursor](https://www.cursor.com/)
- [Claude Code](https://docs.anthropic.com/en/docs/agents-and-tools/claude-code/overview)

Note: The Coding Agent communicates with AI coding models in the cloud, which typically requires a paid service or subscription.
For best results, select a reasoning AI model.

### Target Machine

You need a target machine to execute, run, and debug the generated code. The target machine can be the same as the development machine or a remote system. Refer to the [System Requirements](../get_started/system_requirements.md) page for the list of supported hardware platforms.

> **Important:** In the Preview release, only **Linux** target machines are supported.

## Procedure

### 1. Create a New Project

Create a new, empty project directory on your development machine and open it in your coding environment (e.g., VS Code).

There is no need to clone the DL Streamer GitHub repository. The Coding Agent reads all required information directly from
https://github.com/open-edge-platform/dlstreamer.

### 2. Open the Chat Window and Enter Your Prompt

Open the AI chat window in your coding environment and enter a prompt describing the application you want to build.
The prompt must start with a reference to the DL Streamer Coding Agent:

```
Use DL Streamer Coding Agent from https://github.com/open-edge-platform/dlstreamer

Develop a Python application that implements a license plate recognition pipeline optimized for Intel® Core™ Ultra Series 3 Processors:
- Read input video from a file (https://github.com/open-edge-platform/edge-ai-resources/raw/main/videos/ParkingVideo.mp4) but also allow remote IP cameras
- Run YOLOv11 (https://huggingface.co/morsetechlab/yolov11-license-plate-detection) for object detection and PaddleOCR (https://huggingface.co/PaddlePaddle/PP-OCRv5_server_rec) model for character recognition
- Output license plate text for each detected object as a JSON file
- Annotate the video stream and store it as an output video file

Save source code in the license_plate_recognition directory.
```

### 3. What the Coding Agent Does

Once the prompt is submitted, the DL Streamer Coding Agent automatically performs the following steps:

1. **Sets up the target machine** — installs DL Streamer or pulls the DL Streamer Docker container on the target system.
2. **Downloads AI models** — fetches the required AI models and converts them to OpenVINO™ IR format on the target machine.
3. **Builds the DL Streamer application** — generates the complete application source code, including the main script, model export scripts, configuration files, and a `README.md` with setup and run instructions.
4. **Runs, debugs, and validates** — executes the generated application, debugs any issues, and validates that the output matches expectations.

If specific information is missing, the agent may ask clarifying questions and/or provide suggestions based on the existing DL Streamer sample applications.

The agent may ask for permission to access remote web pages, create files in your project, or execute commands on the target machine. The entire flow — from prompt to running application — typically takes 10–20 minutes, depending on network speed and application complexity.

The final output is a complete project directory containing:

```
<project_name>/
├── <app_name>.py           # Main application script
├── export_models.py        # Model download and export script
├── requirements.txt        # Python dependencies
├── export_requirements.txt # Dependencies for model export
├── README.md               # Setup and run instructions
├── plugins/                # Custom GStreamer elements (if needed)
│   └── python/
├── config/                 # Configuration files (if needed)
├── models/                 # Downloaded and converted models
├── videos/                 # Cached input videos
└── results/                # Output files (JSON, annotated video, etc.)
```

<!--hide_directive
**Video walkthrough:** The following video illustrates how a license plate recognition
application can be created in less than 10 minutes.

:::{video} ./_assets/coding_agent_demo.mp4
:playsinline:
:loop:
:width: 70%
:align: center
hide_directive-->

### 4. How to Write Effective Prompts

Well-structured prompts produce better results. Follow these guidelines when writing your prompts:

#### Prompt Structure

A good prompt should include:

1. **Application type** — Specify whether you want a command-line application, a Python application, a C++ application, a microservice, etc. In Preview mode, only Linux command-line and Python applications are supported.
2. **Input source** — Specify the video source: a file URL, RTSP stream, or webcam. Provide a test video URL when possible.
3. **AI models** — Name the detection, classification, OCR, or VLM models to use (e.g., "YOLOv11 for detection and PaddleOCR for text recognition"). If unsure, describe the task and the agent will select an appropriate model.
4. **Pipeline description** — Describe the processing sequence (e.g., detection → tracking → classification).
5. **Expected output** — Specify what the application should produce (e.g., a JSON file, an annotated video, or alerts).
6. **Target hardware** — Specify the Intel hardware to optimize for (e.g., "Intel® Core™ Ultra Series 3 Processors").

#### Example Prompts

See the [example prompts in the DL Streamer Coding Agent repository](https://github.com/open-edge-platform/dlstreamer/tree/main/.github/skills/dlstreamer-coding-agent/examples) for ready-to-use prompt templates, including:

- **License Plate Recognition** — Detection + OCR pipeline with JSON and annotated video output
- **Event-Based Smart NVR** — Person detection with triggered video recording
- **Multi-Stream Compose** — Multiple RTSP cameras with combined WebRTC output

### 5. Convert Existing Apps with convert-app Prompt (Preview)

The Coding Agent can convert an existing video analytics application into a
native Intel DL Streamer and OpenVINO C++ project using the `convert-app`
prompt workflow.

This is a good fit when you want to:

- migrate a DeepStream application to DL Streamer,
- port a legacy GStreamer AI pipeline,
- preserve the functional stages of an existing app,
- generate reproducible build, run, and documentation deliverables.

Expected output:

- C++ source files and CMake project files.
- `run.sh` wrapper for reproducible execution.
- Model export or download scripts when required.
- `README.md` with conversion notes and validation-oriented run results.

How to invoke:

- Start your prompt with a reference to the DL Streamer Coding Agent.
- Ask to convert an existing application and preserve functional pipeline
	stages.
- Request complete project deliverables (build files, wrapper script, and
	documentation).

The agent reads the source application directly, but you must make sure the
repository is accessible to it, either as a local path or through a URL that
can be fetched with the required permissions.

Example invocations:

```text
/convert-app <source-application-repository-url>
```

```text
/convert-app /workspace/my_ds_app --output-name my_ds_app_dls --device GPU
```

In free-form chat, you can also use:

```text
Use DL Streamer Coding Agent from https://github.com/open-edge-platform/dlstreamer

Convert application at <source-application-repository-url> to a native DL Streamer and OpenVINO C++ project.
Preserve all inference stages, generate CMakeLists.txt, run.sh, README.md, and model export scripts if needed.
```

For implementation details, see [convert_app.md](./convert_app.md).
