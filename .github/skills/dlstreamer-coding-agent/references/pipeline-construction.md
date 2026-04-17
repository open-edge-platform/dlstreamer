# Pipeline Construction Reference

GStreamer pipeline syntax for DLStreamer video-analytics applications.
All pipeline examples use standard GStreamer command-line syntax, applicable both to
`gst-launch-1.0` command-line usage and to `Gst.parse_launch()` in Python applications.

## DLStreamer GStreamer Elements

This section lists elements commonly used in DLStreamer pipelines.
For the full list of DLStreamer elements, see also `../../../../docs/user-guide/elements/`.

### Source Elements

| Element | Purpose | Key Properties |
|---------|---------|----------------|
| `filesrc` | Read video from local file | `location=<path>` |
| `rtspsrc` | Read from RTSP camera stream | `location=<rtsp://url>` |
| `urisourcebin` | Auto-detect source type | `buffer-size=4096 uri=<url>` |
| `gvafpsthrottle` | Limit input frame rate (typically used with filesrc) | `target-fps=30` |

### Decode

| Element | Purpose | Notes |
|---------|---------|-------|
| `decodebin3` | Auto-select decoder | Uses hardware decode when available. **Warning:** Decodes *all* tracks including audio. See Rule 8 for handling audio-track errors in video-only pipelines. |

### Video Processing

| Element | Purpose | Key Properties |
|---------|---------|----------------|
| `videoconvertscale` | Format conversion + scaling | Combined convert+scale |
| `videoconvert` | Pixel format conversion only | |
| `videoscale` | Resolution scaling only | |
| `videorate` | Frame rate adjustment | |
| `vapostproc` | VA-API hardware post-processing | Use before `video/x-raw(memory:VAMemory)` caps |

> **⚠ `vapostproc` metadata warning:** `vapostproc` does not preserve **GstAnalytics** metadata
> (bounding boxes, tracking IDs, classification results). Do not place `vapostproc` between
> elements that produce analytics metadata and elements that read it. On low-throughput branches
> (e.g. dynamic frame selection for VLM), use `videoconvertscale` instead — the CPU copy cost
> is negligible when only a few frames per second are processed.

### AI Inference (DLStreamer-specific)

| Element | Purpose | Model Types | Key Properties |
|---------|---------|-------------|----------------|
| `gvadetect` | Object detection | YOLO, SSD, RT-DETR, D-FINE | `model`, `device`, `batch-size`, `threshold`, `model-instance-id`, `scheduling-policy` |
| `gvaclassify` | Classification & OCR | ResNet, EfficientNet, CLIP, ViT, PaddleOCR | `model`, `device`, `batch-size`, `model-instance-id`, `scheduling-policy` |
| `gvagenai` | VLM / GenAI inference | MiniCPM-V, Qwen2.5-VL, InternVL, SmolVLM | `model-path`, `device`, `prompt`, `generation-config`, `frame-rate`, `chunk-size` |

> **See Rule 3 below** for guidance on choosing the correct element for each model type.

### Tracking

| Element | Purpose | Key Properties |
|---------|---------|----------------|
| `gvatrack` | Object tracking across frames | `tracking-type=zero-term-imageless` |

### Overlay & Metrics

| Element | Purpose | Key Properties |
|---------|---------|----------------|
| `gvawatermark` | Draw bounding boxes and labels on video | `device=...`, `displ-cfg=...` |
| `gvafpscounter` | Print FPS to stdout | (no key properties) |


> **Always use `gvawatermark` for overlays — including custom annotations.**
> `gvawatermark` renders **all** `ODMtd` entries from GstAnalytics metadata.
> Add custom text labels as zero-area boxes:
> `rmeta.add_od_mtd(GLib.quark_from_string("label"), x, y, 0, 0, confidence)`.

### Metadata Publishing

| Element | Purpose | Key Properties |
|---------|---------|----------------|
| `gvametaconvert` | Convert metadata to JSON format | `file-format=json-lines`, `file-path=<path>` |
| `gvametapublish` | Export inference metadata to file | `file-format=json-lines`, `file-path=<path>` |

### Flow Control

| Element | Purpose | Key Properties |
|---------|---------|----------------|
| `tee` | Split stream into multiple branches | `name=<tee_name>` |
| `valve` | Conditionally block/allow stream flow | `drop=true\|false` |
| `queue` | Decouple upstream/downstream threading | `max-size-buffers`, `leaky`, `flush-on-eos` |
| `identity` | Pass-through with sync option | `sync=true` for timing control |

### Multi-Stream Compositing

| Element | Purpose | Key Properties |
|---------|---------|----------------|
| `vacompositor` | **Preferred.** GPU-accelerated compositor operating on VA memory buffers | `name=comp`, `sink_N::xpos`, `sink_N::ypos` |
| `compositor` | CPU-based compositor (use only when VA memory path is not available) | `name=comp`, `sink_N::xpos`, `sink_N::ypos` |

> **Always prefer `vacompositor`** over `compositor` for multi-stream composition.
> The CPU `compositor` requires `video/x-raw` buffers, forcing expensive GPU→CPU
> memory copies that reduce throughput below real-time with 2+ streams.
> `vacompositor` keeps all buffers in `video/x-raw(memory:VAMemory)` for the entire
> decode → infer → scale → composite path.

### Encode & Output

| Element | Purpose | Key Properties |
|---------|---------|----------------|
| `vah264enc` | Hardware H.264 encoder (Intel VA-API) | `bitrate=2000` |
| `h264parse` | H.264 stream parser | Required between encoder and muxer |
| `mp4mux` | MP4 container muxer | |
| `splitmuxsink` | Split output into time-based chunks | `max-size-time=<ns>`, `location=<pattern>` |
| `filesink` | Write to file | `location=<path>` |
| `multifilesink` | Write numbered files | `location=output-%d.jpeg` |
| `autovideosink` | Auto-select display sink | `sync=true` |
| `webrtcsink` | Stream output to a remote machine via WebRTC | `run-signalling-server=true run-web-server=true signalling-server-port=8443`. Built-in signaling + web server — **both default to `false`**, must be enabled explicitly. Web viewer at `http://localhost:8080/`, signaling on port 8443. Use `--network host` in Docker. |
| `jpegenc` | Encode frames as JPEG | |
| `appsink` | Pull frames into application code | `emit-signals=true`, `name=<name>` |

### Custom Logic

If a user pipeline requires custom processing, add new Python GStreamer elements in:
- `plugins/python/<element_name>.py`

For new development, prefer custom Python GStreamer elements in `plugins/python/` over `gvapython`.

## Common Pipeline Patterns

For common use cases, go straight to file generation using predefined application templates and design patterns.

> **Note:** Patterns 1 (Pipeline Core) and 2 (Pipeline Event Loop) are required by every application.
> They are listed explicitly in each row below for completeness.

| Use Case | Templates | Design Patterns | Key Model Export | Reference Sample |
|----------|-----------|-----------------|------------------|------------------|
| Detection + save video + JSON | `python-app-template.py` | 1 + 2 + 12 | Ultralytics | `detection_with_yolo` (CLI) |
| Detection + save video + JSON + display | `python-app-template.py` | 1 + 2 + 9 + 12 | Ultralytics | `detection_with_yolo` (CLI) |
| Detection + classification/OCR + save | `python-app-template.py` + `export-models-template.py` | 1 + 2 + 12 | YOLO + PaddleOCR/optimum-cli | `license_plate_recognition` (CLI), `face_detection_and_classification` (Python) |
| Detection + classification/OCR + save + display | `python-app-template.py` + `export-models-template.py` | 1 + 2 + 9 + 12 | YOLO + PaddleOCR/optimum-cli | `license_plate_recognition` (CLI), `face_detection_and_classification` (Python) |
| VLM alerting + save | `python-app-template.py` | 1 + 2 + 11 + 12 | optimum-cli | `vlm_alerts` (Python) |
| Detection + custom analytics (single output) | `python-app-template.py` | 1 + 2 + 8 + 12 | Ultralytics | `smart_nvr` (Python) |
| Detection + custom analytics + display | `python-app-template.py` | 1 + 2 + 8 + 9 + 12 | Ultralytics | `smart_nvr` (Python) |
| Detection + tracking + recording | `python-app-template.py` | 1 + 2 + 7 + 8 | Ultralytics | `smart_nvr` (Python), `vehicle_pedestrian_tracking` (CLI) |
| Detection + tracking + recording + display | `python-app-template.py` | 1 + 2 + 7 + 8 + 9 + 10 | Ultralytics | `smart_nvr` (Python), `open_close_valve` (Python) |
| Detection + VLM on selected frames | `python-app-template.py` | 1 + 2 + 7 + 8 + 9 + 11 + 12 | Ultralytics + optimum-cli | `vlm_self_checkout` (Python) |
| Custom analytics + chunked storage | `python-app-template.py` | 1 + 2 + 8 | Ultralytics | `smart_nvr` (Python) |
| Custom analytics + chunked storage + display | `python-app-template.py` | 1 + 2 + 8 + 9 + 10 | Ultralytics | `smart_nvr` (Python) |
| Multi-camera RTSP | `python-app-template.py` | 1 + 2 + 3 | (per camera) | `onvif_cameras_discovery` (Python), `multi_stream` (CLI) |
| Multi-stream composite mosaic | `python-app-template.py` | 1 + 2 + 4 | (per stream) | `multi_stream` (CLI) |
| Multi-stream composite + WebRTC + recording | `python-app-template.py` | 1 + 2 + 4 + 8 + 9 | Ultralytics | `multi_stream` (CLI) |

### Example: Decode → Detect → Watermark → Display

```
filesrc location=video.mp4 ! decodebin3 !
gvadetect model=model.xml device=GPU batch-size=4 ! queue !
gvawatermark ! videoconvertscale ! autovideosink
```

### Example: Detect → Watermark → WebRTC Output

```
filesrc location=video.mp4 ! decodebin3 !
gvadetect model=model.xml device=GPU batch-size=4 ! queue !
gvafpscounter ! gvawatermark !
videoconvert ! webrtcsink run-signalling-server=true run-web-server=true signalling-server-port=8443
```

> See the [`webrtcsink` element entry](#encode--output) above for signaling server
> configuration, Docker `--network host` requirement, and the `signaller::address` caveat.

### Example: Decode → Detect → Classify → Encode → Save

```
filesrc location=video.mp4 ! decodebin3 !
gvadetect model=detect.xml device=GPU batch-size=4 ! queue !
gvaclassify model=classify.xml device=GPU batch-size=4 ! queue !
gvafpscounter ! gvawatermark !
gvametaconvert ! gvametapublish file-format=json-lines file-path=results.jsonl !
videoconvert ! vah264enc ! h264parse ! mp4mux !
filesink location=output.mp4
```

### Example: Tee → Dual-Branch (display + analytics)

```
filesrc location=video.mp4 ! decodebin3 !
gvadetect model=model.xml device=GPU ! queue ! gvatrack !
tee name=t
  t. ! queue ! gvawatermark ! videoconvert ! autovideosink
  t. ! queue ! <analytics_branch> ! gvametapublish file-path=results.jsonl
```

### Example: Tee + Valve with Async Sink (conditional recording)

When a valve starts with `drop=true`, no buffers reach downstream sinks, blocking pipeline
preroll indefinitely. Add `async=false` to the terminal sink in valve-gated branches:

```
filesrc location=video.mp4 ! decodebin3 !
gvadetect model=model.xml device=GPU ! queue !
tee name=t
  t. ! queue ! gvawatermark ! videoconvert ! autovideosink
  t. ! queue ! valve name=rec drop=true !
       videoconvert ! vah264enc ! h264parse ! mp4mux fragment-duration=1000 !
       filesink location=output.mp4 async=false
```

### Example: Detect → Track → Custom Python Element

```
filesrc location=video.mp4 ! decodebin3 !
gvadetect model=model.xml device=GPU threshold=0.7 ! queue !
gvaanalytics_py distance=500 angle=-135,-45 !
gvafpscounter ! gvawatermark !
gvarecorder_py location=output.mp4 max-time=10
```

### Example: Multi-Stream Analytics (N streams)

```
filesrc location=cam1.mp4 ! decodebin3 !
gvadetect model=model.xml device=GPU model-instance-id=model0 batch-size=<stream count> ! queue ! ...

filesrc location=cam1.mp4 ! decodebin3 !
gvadetect model=model.xml device=GPU model-instance-id=model0 batch-size=<stream count> ! queue ! ...

... (repeat for stream_2, stream_3, etc.)
```

If multiple parallel streams refer to same model, use `model-instance-id=<instance_name>` to share
model instance across all streams. Set `batch-size=<stream count>` to enable cross-stream batching.
This configuration improves performance and conserves system resources at the same time.

When using a shared `model-instance-id` with a compositor element (`vacompositor` or `compositor`),
you **must** add `scheduling-policy=latency` to all inference elements. Without it, the default
throughput scheduling holds frames from one stream while waiting to fill a cross-stream batch,
creating a circular dependency with the compositor's per-pad synchronization — causing a deadlock.

### Example: Multi-Stream Compositor (N streams → 2×2 grid, GPU memory path)

Use `vacompositor` (not `compositor`) to keep the entire pipeline in VA memory:

```
vacompositor name=comp sink_0::xpos=0 sink_0::ypos=0 sink_1::xpos=640 sink_1::ypos=0
  sink_2::xpos=0 sink_2::ypos=360 sink_3::xpos=640 sink_3::ypos=360 !
vah264enc ! h264parse ! mp4mux fragment-duration=1000 ! filesink location=mosaic.mp4

filesrc location=cam1.mp4 ! decodebin3 !
gvadetect model=model.xml device=GPU model-instance-id=model0 batch-size=4
  scheduling-policy=latency !
queue flush-on-eos=true ! gvafpscounter !
gvametaconvert ! gvametapublish file-format=json-lines file-path=cam1.jsonl !
vapostproc ! video/x-raw(memory:VAMemory),width=640,height=360 !
queue ! comp.sink_0

filesrc location=cam2.mp4 ! decodebin3 !
gvadetect model=model.xml device=GPU model-instance-id=model0 batch-size=4
  scheduling-policy=latency !
queue flush-on-eos=true ! gvafpscounter !
gvametaconvert ! gvametapublish file-format=json-lines file-path=cam2.jsonl !
vapostproc ! video/x-raw(memory:VAMemory),width=640,height=360 !
queue ! comp.sink_1

... (repeat for sink_2, sink_3, etc.)
```

**Key elements in the VA memory compositor pipeline:**
- `vacompositor`: GPU-accelerated composition, operates natively on VA memory buffers.
- `vapostproc ! video/x-raw(memory:VAMemory),width=W,height=H`: GPU-accelerated scaling,
  replaces CPU-based `videoconvertscale ! video/x-raw,width=W,height=H`.
- `scheduling-policy=latency`: Required on all `gvadetect`/`gvaclassify` elements when
  using shared `model-instance-id` with a compositor. Processes frames round-robin without
  waiting to fill a batch, preventing deadlocks.

### Example: Continous VLM Analysis with JSON + Video Output

```
filesrc location=video.mp4 ! decodebin3 !
gvagenai model-path=model_dir device=GPU prompt-path=prompt.txt
    generation-config="max_new_tokens=1,num_beams=4"
    chunk-size=1 frame-rate=1.0 metrics=true !
gvametapublish file-format=json-lines file-path=results.jsonl ! queue !
gvafpscounter ! gvawatermark name=watermark ! videoconvert !
vah264enc ! h264parse ! mp4mux ! filesink location=output.mp4
```

### Example: Detect + VLM with dynamic prompt and/or dynamic frame selection

Use a custom Python element (`gvaframeselection_py`) to select frames for VLM processing.
Set `chunk-size=1` and do not set `frame-rate` on the `gvagenai` element.
This way `gvagenai` processes every frame selected by the custom logic.

```
filesrc location=video.mp4 ! decodebin3 !
gvafpsthrottle target-fps=30 !
gvadetect model=detect.xml device=GPU threshold=0.4 ! queue !
gvatrack tracking-type=zero-term-imageless !
tee name=detect_tee

  detect_tee. ! queue !
  gvawatermark name=watermark ! gvafpscounter !
  vah264enc ! h264parse ! mp4mux ! filesink location=output.mp4

  detect_tee. ! queue leaky=downstream !
  gvaframeselection_py !
  videoconvertscale ! video/x-raw,width={width},height={height} !
  gvagenai name=vlm model-path=vlm_dir device=GPU
      prompt="Say OK." generation-config="max_new_tokens=2"
      chunk-size=1 metrics=true !
  gvametapublish file-format=json-lines file-path=results.jsonl !
  gvawatermark device=CPU !
  jpegenc ! multifilesink location=snapshots-%d.jpeg
```

**VLM branch design notes:**
- **Leaky queue:** prevents the slow VLM branch from back-pressuring the real-time video output.
- **`videoconvertscale` is optional** — only to reduce VLM processing time when frame-level features
  are analyzed. Omit scaling when preserving the original frame resolution matters for analysis.
- **Preroll prompt:** initialize with a trivial prompt (`"Say OK."`) and update it
  programmatically after the pipeline enters PLAYING state.
- **Tracker fragmentation and redundant VLM queries:** With `zero-term-imageless` tracking,
  the same object may receive multiple tracking IDs over time, re-triggering the frame-selection
  threshold and causing duplicate VLM queries. As an alternative, use `tracking-type=zero-term`
  or `deep-sort` for more stable tracking IDs at higher compute cost.

## Pipeline Design Rules

These rules govern how pipelines should be constructed. Follow them in every new application.

### Rule 1 — Prefer VA Memory and GPU/NPU for AI Inference

Keep frames in VA memory throughout the pipeline. Let `decodebin3` auto-select the
decode format and memory type — do **not** insert explicit caps filters for
`video/x-raw(memory:VAMemory)` or `format=NV12` between decode and AI elements.
DLStreamer inference elements (`gvadetect`, `gvaclassify`, `gvagenai`) handle
memory negotiation automatically.

Prefer `device=GPU` or `device=NPU` for inference elements to keep data on the
accelerator and avoid unnecessary GPU↔CPU copies.

### Rule 2 — Let GStreamer Auto-Negotiate Pixel Format

Do **not** force pixel formats (e.g. `video/x-raw,format=RGB`, `format=NV12`) in caps
filters unless a specific element **requires** a particular format (e.g. a custom Python
element that maps buffers to numpy). DLStreamer AI elements adapt to whatever format
they receive. Unnecessary format forcing causes extra `videoconvert` copies and can
break zero-copy paths.

**Exception:** Custom Python elements that call `buffer.map()` to access raw pixels need
a CPU-accessible format — see the "CPU-Accessible Pixel Formats" section below.

### Rule 3 — Element Usage Guidelines

Use the [AI Inference element table](#ai-inference-dlstreamer-specific) above to choose
the correct DLStreamer inference element for each model type (`gvadetect` for detection,
`gvaclassify` for classification/OCR, `gvagenai` for VLMs).

Additional guidance:
- Use `gvaclassify` for OCR models (e.g. PaddleOCR text recognition) and classification
models. DLStreamer handles pre/post-processing automatically via model metadata —
no model-proc files are needed (model-proc is deprecated).
- Only fall back to a custom Python element (Pattern 7 in Design Patterns) when the model
requires custom pre/post-processing that DLStreamer cannot handle automatically.

### Rule 4 — Use queue element after Inference Elements

Inference elements like `gvadetect` or `gvaclassify` are asynchronous and they process output tensors in the context of OpenVINO inference engine threads. Use `queue` elements after inference elements to transfer processing to another thread.

### Rule 5 — Use `gvametapublish` for JSON Output

Use `gvametaconvert` followed by `gvametapublish` as the standard way to export inference results to JSON:

```
gvametaconvert ! gvametapublish file-format=json-lines file-path=results.jsonl
```

Do not write custom file-output logic in pad probes or custom elements when
`gvametapublish` can handle the use case.

### Rule 6 — Device Assignment Strategy for Intel Core Ultra

When targeting Intel Core Ultra processors (which have CPU, GPU, and NPU), assign
inference devices to balance throughput:

| Model Type | Recommended Device | Rationale |
|------------|-------------------|-----------|
| Object detection (YOLO, SSD) | **GPU** | Highest throughput for large models |
| Classification / OCR | **NPU** or **GPU** | NPU is efficient for smaller models; may free GPU bandwidth |
| VLM (gvagenai) | **GPU** | VLMs require GPU memory bandwidth |
| CV + VLM | **NPU** and **GPU** | Run entire computer vision pipeline on NPU and let VLMs occupy GPU |

Use NPU for secondary models when Intel Core Ultra 3 series detected.
Prefer GPU for all models on Intel Core Ultra and Core Ultra 2 series.

### Rule 7 — Use Fragmented MP4 for Robust Output

Standard `mp4mux` requires a clean EOS event to write the `moov` atom. If the pipeline
is interrupted (SIGINT, Docker kill, crash), the output file is **unplayable**. For
long-running, multi-stream, or containerized pipelines, use **fragmented MP4**:

```
vah264enc ! h264parse ! mp4mux fragment-duration=1000 ! filesink location=output.mp4
```

Fragmented MP4 writes self-contained fragments every second. The file is playable at
any point, even without a final EOS.

Also add `flush-on-eos=true` to all `queue` elements in multi-branch pipelines to
speed up EOS propagation through the pipeline graph:

```
queue flush-on-eos=true
```

### Rule 8 — Handle Audio Tracks in Video-Only Pipelines

Transport stream (`.ts`), Matroska (`.mkv`), and some MP4 files contain audio tracks.
`decodebin3` attempts to decode **all** tracks and emits an error if an audio codec
plugin is unavailable. In video-only analytics pipelines, this error is non-fatal and
should be filtered in the application event loop instead of terminating the pipeline.

See [Pattern 2: Pipeline Event Loop](./design-patterns.md#pattern-2-pipeline-event-loop)
in the Design Patterns Reference for the Python implementation of audio-error filtering.

### Rule 9 — Avoid Unnecessary Tee Splits

Use `tee` only when the pipeline genuinely requires two or more **concurrent outputs
that process different subsets of frames**. This typically occurs when downstream
branches operate at a different frequency:
- one branch records selected frames to local files on demand
- another branch sends selected frames to a VLM for additional analysis
- yet another branch forwards each frame to webRTC stream

If a pipeline produces multiple outputs (file save, JSON, MQTT) but every output
processes the same frames at the same rate, prefer a **linear** pipeline:
`source → detect → queue → fpscounter → file_recorder → JSON_output → mqtt_publisher`.
Adding a `tee` introduces extra queues, thread-synchronization overhead, and
EOS-propagation complexity (see Rule 7) — avoid it unless the branches genuinely
diverge in frame selection or processing rate.

## Common Gotchas

See [Common Gotchas](./debugging-hints.md#common-gotchas) in the Debugging Hints Reference for
a table of known pitfalls (unplayable MP4, audio track crashes, EOS hangs, etc.) and their mitigations.
