# Pipeline Construction Reference

GStreamer pipeline syntax for DL Streamer video-analytics applications.

## DL Streamer GStreamer Elements

For the full list of elements, see also `../../../../docs/user-guide/elements/`.

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

> **⚠ `vapostproc` metadata warning:** `vapostproc` does not preserve GstAnalytics metadata.
> Do not place it between elements that produce and read analytics metadata.
> Use `videoconvertscale` instead when metadata must be preserved.

### AI Inference (DLStreamer-specific)

| Element | Purpose | Model Types | Key Properties |
|---------|---------|-------------|----------------|
| `gvadetect` | Object detection | YOLO, SSD, RT-DETR, D-FINE | `model`, `device`, `batch-size`, `threshold`, `model-instance-id`, `scheduling-policy` |
| `gvaclassify` | Classification & OCR | ResNet, EfficientNet, CLIP, ViT, PaddleOCR | `model`, `device`, `batch-size`, `model-instance-id`, `scheduling-policy` |
| `gvagenai` | VLM / GenAI inference | MiniCPM-V, Qwen2.5-VL, InternVL, SmolVLM | `model-path`, `device`, `prompt`, `generation-config`, `frame-rate`, `chunk-size` |

> **See Rule 3 below** for guidance on choosing the correct element for each model type.

> **`max_new_tokens` sizing guide for `gvagenai`:**
>
> | Use Case | Recommended `max_new_tokens` |
> |----------|-----------------------------|
> | Classification (single label) | 1–4 |
> | Short structured answer (yes/no + label) | 10-15 |
> | Multi-object structured analysis | 30-50 |
> | Free-form description or summary | 100-200 |
>
> **Thinking models** may emit `<think>...</think>` tokens consuming `max_new_tokens`.
> Add "Do not explain your reasoning, answer directly." to the prompt to disable.

### Tracking

| Element | Purpose | Key Properties |
|---------|---------|----------------|
| `gvatrack` | Object tracking across frames | `tracking-type=zero-term-imageless` |

### Overlay & Metrics

| Element | Purpose | Key Properties |
|---------|---------|----------------|
| `gvawatermark` | Draw bounding boxes and labels on video | `device=...`, `displ-cfg=...` |
| `gvafpscounter` | Print FPS to stdout | (no key properties) |


> **Always use `gvawatermark` for overlays.** It renders all `ODMtd` entries from GstAnalytics metadata.
> Custom text labels: `rmeta.add_od_mtd(GLib.quark_from_string("label"), x, y, 0, 0, confidence)`.

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

| Use Case | Templates | Design Patterns | Key Model Export | Reference Sample |
|----------|-----------|-----------------|------------------|------------------|
| Detection + save video + JSON | `python-app-template.py` | 1 + 2 | Ultralytics | `detection_with_yolo` (CLI) |
| Detection + save video + JSON + display | `python-app-template.py` | 1 + 2 + 9 | Ultralytics | `detection_with_yolo` (CLI) |
| Detection + classification/OCR + save | `python-app-template.py` + `export-models-template.py` | 1 + 2 | YOLO + PaddleOCR/optimum-cli | `license_plate_recognition` (CLI), `face_detection_and_classification` (Python) |
| Detection + classification/OCR + save + display | `python-app-template.py` + `export-models-template.py` | 1 + 2 + 9 | YOLO + PaddleOCR/optimum-cli | `license_plate_recognition` (CLI), `face_detection_and_classification` (Python) |
| VLM alerting + save | `python-app-template.py` | 1 + 2 | optimum-cli | `vlm_alerts` (Python) |
| Detection + custom analytics (single output) | `python-app-template.py` | 1 + 2 + 8 | Ultralytics | `smart_nvr` (Python) |
| Detection + custom analytics + display | `python-app-template.py` | 1 + 2 + 8 + 9 | Ultralytics | `smart_nvr` (Python) |
| Detection + tracking + recording | `python-app-template.py` | 1 + 2 + 7 + 8 | Ultralytics | `smart_nvr` (Python), `vehicle_pedestrian_tracking` (CLI) |
| Detection + tracking + recording + display | `python-app-template.py` | 1 + 2 + 7 + 8 + 9 + 10 | Ultralytics | `smart_nvr` (Python), `open_close_valve` (Python) |
| Detection + VLM on selected frames | `python-app-template.py` | 1 + 2 + 7 + 8 + 9 | Ultralytics + optimum-cli | `vlm_self_checkout` (Python) |
| Custom analytics + chunked storage | `python-app-template.py` | 1 + 2 + 8 | Ultralytics | `smart_nvr` (Python) |
| Custom analytics + chunked storage + display | `python-app-template.py` | 1 + 2 + 8 + 9 + 10 | Ultralytics | `smart_nvr` (Python) |
| Multi-camera RTSP | `python-app-template.py` | 1 + 2 + 3 | (per camera) | `onvif_cameras_discovery` (Python), `multi_stream` (CLI) |
| Multi-stream composite mosaic | `python-app-template.py` | 1 + 2 + 4 | (per stream) | `multi_stream` (CLI) |
| Multi-stream composite + WebRTC + recording | `python-app-template.py` | 1 + 2 + 4 + 9 | Ultralytics | `multi_stream` (CLI) |

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

### Example: Tee + Valve (conditional recording)

Valves start with `drop=false` so downstream sinks negotiate caps and complete
preroll. Add `async=false` to the terminal sink in valve-gated branches.
See [Pattern 9](./design-patterns.md#pattern-9-dynamic-pipeline-control-tee--valve)
for Python control code.

```
filesrc location=video.mp4 ! decodebin3 !
gvadetect model=model.xml device=GPU ! queue !
tee name=t
  t. ! queue ! gvawatermark ! videoconvert ! autovideosink
  t. ! queue ! valve name=rec drop=false !
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

Use `model-instance-id=<name>` to share model instances across streams.
Set `batch-size=<stream count>` for cross-stream batching.

With a compositor, you **must** add `scheduling-policy=latency` to all inference elements
to prevent deadlocks.

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
gvawatermark !
vapostproc ! video/x-raw(memory:VAMemory),width=640,height=360 !
queue ! comp.sink_0

filesrc location=cam2.mp4 ! decodebin3 !
gvadetect model=model.xml device=GPU model-instance-id=model0 batch-size=4
  scheduling-policy=latency !
queue flush-on-eos=true ! gvafpscounter !
gvametaconvert ! gvametapublish file-format=json-lines file-path=cam2.jsonl !
gvawatermark !
vapostproc ! video/x-raw(memory:VAMemory),width=640,height=360 !
queue ! comp.sink_1

... (repeat for sink_2, sink_3, etc.)
```

### Example: Multi-Stream Selective Recording (per-stream tee + valve)

Dynamically choose which stream to record using inline `valve` elements.
See [Pattern 9](./design-patterns.md#pattern-9-dynamic-pipeline-control-tee--valve)
for the Python implementation and preroll strategy.

**Per-stream topology:**
```
source → decode → detect → queue → fpscounter → metaconvert → metapublish →
gvawatermark → tee name=stream_tee_N
  stream_tee_N. ! queue ! ...                                    ← further stream processing branch
  stream_tee_N. ! queue ! valve name=rec_valve_N drop=false !    ← on-demand recording branch
       videoconvert ! vah264enc ! h264parse !
       mp4mux fragment-duration=1000 ! filesink location=streamN.mp4 async=false
```

### Example: Continuous VLM Analysis with JSON + Video Output

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
- **Leaky queue:** prevents VLM branch from back-pressuring the real-time output.
- **`videoconvertscale`** is optional — only to reduce VLM input resolution.
- **Preroll prompt:** initialize with a trivial prompt (`"Say OK."`) and update
  programmatically after the pipeline enters PLAYING state.
- **Tracker fragmentation:** `zero-term-imageless` tracking may re-trigger VLM queries
  due to unstable IDs. Use `zero-term` or `deep-sort` for more stable tracking.

### Per-Object VLM Analysis

**Approach A: Full-frame prompt (simpler, less accurate)**

Send the full frame and ask the VLM to analyze all visible objects. Works when objects
are large (> 15% of frame) and few (< 5).

```
gvagenai ... prompt="Analyze each worker in this image. For each worker:\n
  1. Helmet? (WEARING / NOT_WEARING / UNCERTAIN)\n
  2. Harness? (SECURED / NOT_SECURED / UNCERTAIN)\n
  Answer directly, no reasoning."
  generation-config="max_new_tokens=256"
```

**Approach B: Per-crop custom element (more accurate, more complex)**

Create a custom `BaseTransform` element that crops each detected object and passes
one crop per buffer to `gvagenai` with `chunk-size=1`. Use when objects are small
(< 10% of frame) or per-object tracking correlation is required.

## Pipeline Design Rules

### Rule 1 — Prefer VA Memory and GPU/NPU for AI Inference

Do **not** insert explicit caps filters for `video/x-raw(memory:VAMemory)` or `format=NV12`
between decode and AI elements. DL Streamer inference elements handle memory negotiation
automatically. Prefer `device=GPU` or `device=NPU`.

### Rule 2 — Let GStreamer Auto-Negotiate Pixel Format

Do **not** force pixel formats (e.g. `format=RGB`, `format=NV12`) unless an element
**requires** it (e.g. custom Python element mapping buffers to numpy).

### Rule 3 — Element Usage Guidelines

Use `gvadetect` for detection, `gvaclassify` for classification/OCR, `gvagenai` for VLMs.
Model-proc files are deprecated. Only fall back to a custom Python element when the model
requires custom pre/post-processing.

### Rule 4 — Use `queue` After Inference Elements

Add `queue` after inference elements to transfer processing to another thread.

### Rule 5 — Use `gvametapublish` for JSON Output

```
gvametaconvert ! gvametapublish file-format=json-lines file-path=results.jsonl
```

### Rule 6 — Device Assignment Strategy for Intel Core Ultra

| Model Type | Recommended Device |
|------------|-------------------|
| Object detection (YOLO, SSD) | **GPU** |
| Classification / OCR | **NPU** or **GPU** |
| VLM (gvagenai) | **GPU** |
| CV + VLM | **NPU** and **GPU** |

Use NPU for secondary models on Core Ultra 3. Prefer GPU for all models on Core Ultra 1/2.

### Rule 7 — Use Fragmented MP4 for Robust Output

Use `mp4mux fragment-duration=1000` for long-running or containerized pipelines.
Add `flush-on-eos=true` to all `queue` elements in multi-branch pipelines.

```
vah264enc ! h264parse ! mp4mux fragment-duration=1000 ! filesink location=output.mp4
```

### Rule 8 — Handle Audio Tracks in Video-Only Pipelines

`.ts`, `.mkv`, and some MP4 files contain audio tracks. `decodebin3` emits an error if
an audio codec plugin is unavailable. Filter this as non-fatal in the event loop.
See [Pattern 2](./design-patterns.md#pattern-2-pipeline-event-loop).

### Rule 9 — Avoid Unnecessary Tee Splits

Use `tee` only when branches genuinely diverge in frame selection or processing rate.
If all outputs process the same frames at the same rate, use a linear pipeline.

### Rule 10 — Place `gvawatermark` Before `tee` in Multi-Branch Pipelines

When a stream branches via `tee` and multiple branches need overlays, place a **single**
`gvawatermark` element **before** the `tee`, not on individual branches:

```
gvadetect ... ! queue ! gvawatermark ! tee name=t
  t. ! queue ! vapostproc ! ... ! comp.sink_N
  t. ! queue ! fakesink async=false sync=false
```

## Common Gotchas

See [Common Gotchas](./debugging-hints.md#common-gotchas) in the Debugging Hints Reference for
a table of known pitfalls (unplayable MP4, audio track crashes, EOS hangs, etc.) and their mitigations.
