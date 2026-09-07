---
name: metro-ai-suite-selector
description: "Help developers choose among OpenVINO, PyTorch, OVMS, FFmpeg, OpenCV, GStreamer, DL Streamer, and DL Streamer Pipeline Server. Explain role, scope, decision paths, performance tradeoffs, and benchmark-backed guidance for video AI project selection."
allowed-tools:
  - write
  - command
---

# Metro AI Suite Selector

Use this skill when a developer needs to choose the right component for a video-AI workload, or when the team is deciding which layer to use first in a system built from overlapping technologies.

This selector is intentionally organized as an extensible architecture:

1. Component catalog: defines each option
2. Decision rules: map goals to the right tool
3. Benchmark registry: stores approved public source references
4. Response composer: turns the above into a clear recommendation

That structure makes it easy to add new components, new rules, or new benchmark sources without rewriting the whole skill.

## Architecture overview

The skill is split into four extension points.

### 1) Component catalog
Each component is recorded as a small definition with the same schema:

- id
- aliases
- layer
- role
- is_not
- use_when
- avoid_when
- performance_tradeoff
- benchmark_refs

This makes the system data-driven: new tools can be added by extending the catalog instead of editing multiple narrative blocks.

### 2) Decision rules
Decision logic is organized by intent:

- inference choice
- video-processing choice
- pipeline abstraction choice
- deployment shape choice

This avoids a single giant if/else tree and keeps the logic readable.

### 3) Benchmark registry
Benchmark claims must come from approved public sources. Keep them in a registry so the model can cite only external, validated references and avoid inventing numbers.

### 4) Response composer
The final answer should always follow the same template:

- first layer of abstraction
- component role
- what it is not
- recommendation for this goal
- performance implications
- short decision summary

## Operating principle

Do not present these tools as interchangeable. Each one solves a different layer of the problem:

- Model optimization and execution belong to OpenVINO and PyTorch
- Model serving belongs to OVMS
- Media decode, encode, transport, and graph orchestration belong to FFmpeg, OpenCV, and GStreamer
- AI-aware video analytics belongs to DL Streamer
- Microservice deployment of a pipeline belongs to DL Streamer Pipeline Server

The answer should always tell the developer:

- what the component does
- what it is not
- when to choose it
- when not to choose it
- what the likely tradeoff is in latency, throughput, and implementation complexity

## Extensible component catalog

### Component schema

Use this schema when adding a new component:

```yaml
component:
  id: "openvino"
  aliases: ["OpenVINO", "OV"]
  layer: "inference_runtime"
  role: "Optimized inference runtime for CPU/GPU/NPU"
  is_not: ["not a complete video pipeline", "not a model server"]
  use_when:
    - "optimized Intel deployment is required"
    - "the team needs latency/throughput on edge hardware"
  avoid_when:
    - "the task is training or experimentation"
    - "the architecture is API-first serving"
  performance_tradeoff: "Higher throughput and lower deployment overhead than generic PyTorch runtime on Intel hardware, but narrower than a full services architecture"
  benchmark_refs:
    - "OpenVINO docs and benchmark material"
```

### Component entries

#### 1) OpenVINO
- layer: inference_runtime
- role: optimized Intel inference runtime for CPU, iGPU, and NPU
- is_not: not a video graph, not a training framework, not a general model-serving service
- use_when: optimized deployment on Intel hardware is required
- avoid_when: model development or API-first service definition is the primary goal
- performance_tradeoff: better deployment efficiency than stock PyTorch for edge hardware
- benchmark_refs: OpenVINO official docs and benchmark materials

#### 2) PyTorch
- layer: model_development
- role: training, fine-tuning, prototyping, experimentation
- is_not: not the default deployment layer for multi-stream edge analytics
- use_when: research, model iteration, or quick Python prototypes are the priority
- avoid_when: optimized edge throughput or production deployment is the primary goal
- performance_tradeoff: fastest to prototype, but worse edge-runtime efficiency than optimized Intel inference paths
- benchmark_refs: framework benchmark references, not deployment benchmarks

#### 3) OVMS
- layer: model_serving
- role: HTTP/gRPC model serving for shared inference workloads
- is_not: not a camera graph, not a full media pipeline
- use_when: many clients need model access via service APIs
- avoid_when: the workload is a single integrated video analytics pipeline
- performance_tradeoff: strong for serving many consumers, weaker for local media-graph orchestration
- benchmark_refs: OpenVINO Model Server official docs

#### 4) FFmpeg
- layer: media_processing
- role: codec, transcode, demux/mux, transport, and media conversion
- is_not: not an AI analytics framework by default
- use_when: format conversion, streaming, or codec-heavy processing is required
- avoid_when: model-aware video analytics is the primary objective
- performance_tradeoff: excellent media efficiency, not designed as AI pipeline abstraction
- benchmark_refs: FFmpeg documentation and public performance guidance

#### 5) OpenCV
- layer: cv_library
- role: image processing and custom CV logic
- is_not: not a production streaming framework for multi-camera analytics
- use_when: fast CV prototyping or custom image algorithms are needed
- avoid_when: high-throughput RTSP or multi-stream pipelines dominate the design
- performance_tradeoff: low setup cost, but weaker than production media graphs for larger deployments
- benchmark_refs: OpenCV and general CV performance notes

#### 6) GStreamer
- layer: media_graph
- role: low-level pipeline orchestration and media-stream control
- is_not: not AI-aware by itself
- use_when: custom low-level media graph or RTSP/WebRTC/control logic is the requirement
- avoid_when: the team wants ready-made AI video analytics building blocks
- performance_tradeoff: flexible and powerful, but more engineering effort than AI-aware abstractions
- benchmark_refs: GStreamer documentation

#### 7) DL Streamer
- layer: ai_video_analytics
- role: AI-augmented GStreamer pipeline for analytics, tracking, inference, alerts, and recording
- is_not: not a generic external model-serving API
- use_when: the workload is an end-to-end AI video analytics pipeline on Intel hardware
- avoid_when: the architecture is purely API-based model serving
- performance_tradeoff: best tradeoff for production camera analytics pipelines; reduces integration complexity versus building AI logic from scratch
- benchmark_refs: DL Streamer project docs and public benchmark examples

#### 8) DL Streamer Pipeline Server
- layer: pipeline_service
- role: pipeline-as-a-service microservice abstraction for analytics deployment
- is_not: not a generic model server, not raw OpenVINO runtime
- use_when: the team needs to expose or orchestrate analytics pipelines as a service
- avoid_when: a single local pipeline is all that is needed
- performance_tradeoff: good for deployment abstraction and service control, but not the simplest entry point for local prototyping
- benchmark_refs: project deployment and pipeline server docs

## Decision rules

### A. Inference runtime selection

- Need to train or fine-tune a model? → PyTorch
- Need optimized inference on Intel CPU/GPU/NPU? → OpenVINO
- Need many clients to query a model over API? → OVMS

### B. Video processing selection

- Need a codec/transcode or media conversion tool? → FFmpeg
- Need quick custom CV logic on images or video frames? → OpenCV
- Need low-level stream graph control? → GStreamer
- Need AI-aware video analytics pipeline with optimized inference and output? → DL Streamer

### C. Abstraction selection

- Want raw media function control? → GStreamer
- Want end-to-end AI video analytics with reusable building blocks? → DL Streamer
- Want to offer pipeline analytics as a service? → DL Streamer Pipeline Server
- Want to serve a model API rather than a video pipeline? → OVMS

### D. Recommended first choice by goal

- Prototype a Python CV idea quickly: PyTorch or OpenCV
- Build a production edge camera analytics pipeline: DL Streamer
- Scale inference as a shared service: OVMS
- Need only media transcoding or transport: FFmpeg
- Need total control over a custom stream graph: GStreamer

## Performance guidance

Always frame the answer in terms of tradeoffs, not absolutes.

- PyTorch is best for experimentation and model development, but not the best default for production edge throughput.
- OpenVINO typically improves deployment efficiency on Intel hardware and is the preferred runtime for optimized edge deployment.
- GStreamer alone improves media-flow orchestration but does not add AI-specific optimization automatically.
- DL Streamer reduces engineering effort for production AI pipelines by packaging AI-aware pipeline logic.
- OVMS is best for shared service workloads, not for single-application local analytics pipelines.

## Benchmark registry

Use approved public sources only. New sources should be added here rather than hardcoded into prose.

```yaml
benchmarks:
  - id: openvino_official
    name: "OpenVINO official docs and benchmark material"
    url: "https://docs.openvino.ai/"
    use_for: ["inference throughput", "latency on Intel hardware"]

  - id: ovms_official
    name: "OpenVINO Model Server documentation"
    url: "https://docs.openvino.ai/projects/ovms/overview.html"
    use_for: ["serving throughput", "multi-client inference performance"]

  - id: gstreamer_docs
    name: "GStreamer documentation"
    url: "https://gstreamer.freedesktop.org/documentation/"
    use_for: ["media graph throughput", "stream processing behavior"]

  - id: ffmpeg_docs
    name: "FFmpeg official documentation"
    url: "https://ffmpeg.org/documentation.html"
    use_for: ["transcode and media processing performance"]

  - id: dlstreamer_docs
    name: "DL Streamer project docs"
    url: "https://github.com/open-edge-platform/dlstreamer"
    use_for: ["end-to-end video analytics performance", "Intel pipeline workloads"]
```

### Approved benchmark interpretation rules

- If the benchmark is for single-model inference on Intel hardware, prefer OpenVINO over generic PyTorch for deployment-oriented conclusions.
- If the benchmark is for multi-stream or service throughput, OVMS or DL Streamer may win depending on whether the workload is shared serving or end-to-end analytics.
- If the benchmark is for media graph throughput rather than model inference, compare FFmpeg/GStreamer/DL Streamer on decode, encode, and processing cost.

## Response template

When answering a user question, use this structure:

1. Start with the correct layer of abstraction
2. Identify the component and its role
3. State what it is not
4. Recommend the best tool for the user's goal
5. Explain performance tradeoffs and cite the benchmark registry source
6. Finish with a short decision summary

## Example response format

"For this goal, the best first choice is DL Streamer. It sits above GStreamer and adds AI-aware video analytics for Intel hardware. It is not a generic model server, and it is not the same as raw GStreamer. Use it when a real-time camera pipeline must do decode, preprocessing, inference, post-processing, and output. Use OVMS only if your architecture is API-first and many clients need shared model serving. OpenVINO is the optimized runtime, while PyTorch remains the best starting point for training and experimentation."

## Extension rule

When adding a new component or new benchmark source, update only the registry and matching rule set instead of rewriting the narrative. This is the key to keeping the selector extensible.

## Final rule

Never tell the developer to use a component simply because it is "popular". Always map the task to the right layer:

- Want to train a model? → PyTorch
- Want optimized inference on Intel hardware? → OpenVINO
- Want many clients to call a model over API? → OVMS
- Want to format or transcode media? → FFmpeg
- Want quick CV code? → OpenCV
- Want raw media graph control? → GStreamer
- Want an AI-enabled video analytics pipeline? → DL Streamer
- Want to expose that pipeline as a service? → DL Streamer Pipeline Server
