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
| `decodebin3` | Auto-select decoder | Uses hardware decode when available. Add `caps="video/x-raw(ANY)"` to suppress audio pads and avoid `not-linked` errors. See [Decode Robustness](#decode-robustness). |

### Video Processing

| Element | Purpose | Notes |
|---------|---------|-------|
| `vapostproc` | GPU format conversion + scaling | **Preferred in GPU pipelines.** Does not preserve GstAnalytics metadata |
| `videoconvertscale` | CPU format conversion + scaling | Preserves metadata. Inserts GPU→CPU→GPU copies in VA memory pipelines |
| `videoconvert` | CPU pixel format conversion | Inserts GPU→CPU→GPU copies in VA memory pipelines |
| `videoscale` | CPU resolution scaling | Inserts GPU→CPU→GPU copies in VA memory pipelines |
| `videorate` | Frame rate adjustment | |

### AI Inference (DL Streamer-specific)

| Element | Purpose | Model Types | Key Properties |
|---------|---------|-------------|----------------|
| `gvadetect` | Object detection | YOLO, SSD, RT-DETR, D-FINE | `model`, `device`, `batch-size`, `model-instance-id`, `scheduling-policy` |

> **`threshold` default:** `gvadetect` uses `threshold=0.5` by default. Do not set it explicitly unless a non-default value is needed.
| `gvaclassify` | Classification & OCR | ResNet, EfficientNet, CLIP, ViT, PaddleOCR | `model`, `device`, `batch-size`, `model-instance-id`, `scheduling-policy` |
| `gvagenai` | VLM / GenAI inference | MiniCPM-V, Qwen2.5-VL, InternVL, SmolVLM | `model-path`, `device`, `prompt`, `generation-config`, `frame-rate`, `chunk-size` |

> **See [Element & Device Selection](#element--device-selection)** for guidance on choosing the correct element and device for each model type.

> **`gvagenai` scope:** Unlike `gvaclassify` (which automatically crops each detected
> object's ROI), `gvagenai` sends the **entire frame** to the VLM. For per-object VLM
> analysis, add custom crop elements upstream. See [VLM Examples](#vlm-examples).

> **`max_new_tokens` sizing guide for `gvagenai`:**
>
> | Use Case | Recommended `max_new_tokens` |
> |----------|-----------------------------|
> | Classification (single label) | 1–4 |
> | Short structured answer (yes/no + label) | 10–15 |
> | Multi-object structured analysis | 30–50 |
> | Free-form description or summary | 100–200 |

### Tracking

| Element | Purpose | Key Properties |
|---------|---------|----------------|
| `gvatrack` | Object tracking across frames | `tracking-type=zero-term-imageless` |

> **Deep SORT tracking:** For robust re-identification tracking, use `tracking-type=deep-sort`
> with a feature extraction model (e.g. mars-small128) via `gvainference` upstream.
> Always set `reid_max_age` to enable re-identification after occlusion.
> When using `object-class=person` on `gvainference`, always set `displ-cfg=show-roi=person`
> on `gvawatermark` to render only person ROIs (the detector may also produce non-person classes).
> See [object_tracking.md](../../../../docs/user-guide/dev_guide/object_tracking.md#deep-sort-tracking) for all tuning parameters.

### Overlay & Metrics

| Element | Purpose | Key Properties |
|---------|---------|----------------|
| `gvawatermark` | Draw bounding boxes and labels on video | `device=...`, `displ-cfg=...` |
| `gvafpscounter` | Print FPS to stdout | (no key properties) |


> **Always use `gvawatermark` for overlays.** It renders all `ODMtd` entries from GstAnalytics metadata.

> **FPS annotation on video:** Use `gvawatermark displ-avgfps=true` to overlay the average FPS
> directly on the output video frames. Requires `gvafpscounter` to be present somewhere in
> the same pipeline (typically placed after `gvawatermark`):
> ```
> gvawatermark displ-avgfps=true ! gvafpscounter ! ...
> ```
> Use `gvafpscounter` alone (without `displ-avgfps`) when FPS should only be printed to stdout.

> **Filter overlays by class:** When upstream elements use `object-class=<class>` (e.g.
> `gvainference ... object-class=person`), set `displ-cfg=show-roi=<class>` on `gvawatermark`
> to render only matching ROIs. Example: `gvawatermark displ-cfg=show-roi=person`.
> Custom text labels: `rmeta.add_od_mtd(GLib.quark_from_string("label"), x, y, 0, 0, confidence)`.

### Metadata Publishing

| Element | Purpose | Key Properties |
|---------|---------|----------------|
| `gvametaconvert` | Convert metadata to JSON format | `file-format=json-lines`, `file-path=<path>` |
| `gvametapublish` | **Pass-through transform** — publish metadata to file, Kafka, or MQTT while forwarding buffers downstream unchanged | `method=file\|kafka\|mqtt` |

> **`gvametapublish` is a transform, not a sink.** Unlike DeepStream's `nvmsgbroker` (which is a
> sink and requires a `tee` to split the stream), `gvametapublish` forwards buffers downstream.
> Place it inline in the same branch as watermark + encode — **no `tee` is needed** for combined
> publish + video output. See the [Detect → Classify → Encode → Save](#example-decode--detect--classify--encode--save) example.

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

Two approaches for adding custom per-frame logic in Python applications:

**Pad probe callback** (Pattern 5) — attach to any pad in the pipeline. Use for:
- Metadata inspection, logging, counting, or printing summaries
- Frame throttling or conditional dropping (`Gst.PadProbeReturn.DROP`)
- Simple stateful logic (counters, cooldowns) managed via closure or `user_data`

**Custom Python element** (Pattern 7/8) — add in `plugins/python/<element_name>.py`. Use for:
- Reusable elements with GObject properties configurable from the pipeline string
- Complex logic that manages internal sub-pipelines (e.g. event-triggered recording)
- Elements intended for sharing across multiple applications
- Elements that modify GstBuffers or metadata

Do not use `gvapython` element; it is deprecated and will be removed in future releases.

Prefer pad probe callbacks when the logic is self-contained within a single application
and does not need GObject properties. Prefer custom Python elements when the logic
needs to be parameterized from the pipeline string or reused across apps.

## Common Pipeline Patterns

Numbers in the **Design Patterns** column refer to [design-patterns.md](./design-patterns.md)

| Use Case | Templates | Design Patterns | Key Model Export | Reference Sample |
|----------|-----------|-----------------|------------------|------------------|
| Detection + save video + JSON | `python-app-template.py` | 1 + 2 | Ultralytics | `detection_with_yolo` (CLI) |
| Detection + save video + JSON + display | `python-app-template.py` | 1 + 2 + 9 | Ultralytics | `detection_with_yolo` (CLI) |
| Detection + classification/OCR + save | `python-app-template.py` + `export-models-template.py` | 1 + 2 | YOLO + PaddleOCR/optimum-cli | `license_plate_recognition` (CLI), `face_detection_and_classification` (Python) |
| Detection + classification/OCR + save + display | `python-app-template.py` + `export-models-template.py` | 1 + 2 + 9 | YOLO + PaddleOCR/optimum-cli | `license_plate_recognition` (CLI), `face_detection_and_classification` (Python) |
| Detection + custom analytics (single output) | `python-app-template.py` | 1 + 2 + 8 | Ultralytics | `smart_nvr` (Python) |
| Detection + custom analytics + display | `python-app-template.py` | 1 + 2 + 8 + 9 | Ultralytics | `smart_nvr` (Python) |
| Detection + tracking + recording | `python-app-template.py` | 1 + 2 + 7 + 8 | Ultralytics | `smart_nvr` (Python), `vehicle_pedestrian_tracking` (CLI) |
| Detection + tracking + recording + display | `python-app-template.py` | 1 + 2 + 7 + 8 + 9 + 10 | Ultralytics | `smart_nvr` (Python), `open_close_valve` (Python) |
| VLM alerting + save | `python-app-template.py` | 1 + 2 | optimum-cli | `vlm_alerts` (Python) |
| Detection + VLM on selected frames | `python-app-template.py` | 1 + 2 + 7 + 9 | Ultralytics + optimum-cli | `vlm_self_checkout` (Python) |
| Detection + per-object crop + VLM | `python-app-template.py` | 1 + 2 + 7 + 9 | Ultralytics + optimum-cli | — |
| Custom analytics + chunked storage | `python-app-template.py` | 1 + 2 + 8 | Ultralytics | `smart_nvr` (Python) |
| Custom analytics + chunked storage + display | `python-app-template.py` | 1 + 2 + 8 + 9 + 10 | Ultralytics | `smart_nvr` (Python) |
| Multi-camera RTSP | `python-app-template.py` | 1 + 2 + 3 | (per camera) | `onvif_cameras_discovery` (Python), `multi_stream` (CLI) |
| Multi-stream composite mosaic | `python-app-template.py` | 1 + 2 + 4 | (per stream) | `multi_stream` (CLI) |
| Multi-stream composite + WebRTC + recording | `python-app-template.py` | 1 + 2 + 4 + 9 | Ultralytics | `multi_stream` (CLI) |


## Single-stream Examples

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

## Multi-stream Examples

### Example: Multi-Stream Analytics (N streams)

```
filesrc location=cam1.mp4 ! decodebin3 !
gvadetect model=model.xml device=GPU model-instance-id=model0 batch-size=<stream count> ! queue ! ...

filesrc location=cam2.mp4 ! decodebin3 !
gvadetect model=model.xml device=GPU model-instance-id=model0 batch-size=<stream count> ! queue ! ...

... (repeat for stream_3, stream_4, etc.)
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

## VLM Examples

`gvagenai` always processes the full input frame — it does not crop per-object ROIs.
Choose the pipeline topology based on VLM scope and trigger:

| VLM Scope | Trigger | Topology | Reference |
|-----------|---------|----------|-----------|
| **Full scene** | Periodic (fixed interval) | `gvagenai` with `frame-rate` on full/downscaled frames | `vlm_alerts` |
| **Full scene** | On demand (triggered by detection analytics) | Custom selection element drops frames; `gvagenai` on full frames | `vlm_self_checkout` |
| **Per object** | On demand (triggered by specific object detection) | Custom selection + crop elements upstream of `gvagenai`; one object per VLM call | — |

### VLM branch design notes

- Set `chunk-size=1` when using frame selection — do not set `frame-rate`.
- Use `queue leaky=downstream` before the VLM branch.
- Place `videoconvertscale` between custom crop elements and `gvagenai` for caps negotiation.
- Preserve aspect ratio when resizing for VLM — use `videoconvertscale add-borders=true`
  or letterbox manually in custom crop elements.

### Example: Periodic Full-Frame VLM (no detection)

```
filesrc location=video.mp4 ! decodebin3 !
gvagenai model-path=model_dir device=GPU prompt-path=prompt.txt
    generation-config="max_new_tokens=4"
    chunk-size=1 frame-rate=1.0 metrics=true !
gvametapublish file-format=json-lines file-path=results.jsonl !
gvafpscounter ! gvawatermark ! videoconvert !
vah264enc ! h264parse ! mp4mux ! filesink location=output.mp4
```

### Example: Detect → Select → Full-Frame VLM

A custom selection element drops frames that do not meet analysis criteria; the VLM receives the full frame.

```
filesrc location=video.mp4 ! decodebin3 !
gvafpsthrottle target-fps=30 !
gvadetect model=detect.xml device=GPU threshold=0.4 ! queue !
gvatrack tracking-type=zero-term-imageless !
tee name=detect_tee
  detect_tee. ! queue ! gvawatermark ! gvafpscounter !
      vah264enc ! h264parse ! mp4mux ! filesink location=output.mp4
  detect_tee. ! queue leaky=downstream !
      gvaframeselection_py !
      videoconvertscale ! video/x-raw,width={width},height={height} !
      gvagenai name=vlm model-path=vlm_dir device=GPU
          prompt-path=prompt.txt generation-config="max_new_tokens=50"
          chunk-size=1 metrics=true !
      gvametapublish file-format=json-lines file-path=results.jsonl !
      gvawatermark device=CPU ! jpegenc !
      multifilesink location=snapshots-%05d.jpeg
```

### Example: Detect → Select → Per-Object Crop → VLM

Two custom elements: a selection element picks one object per frame and tags its bounding box;
a crop element extracts that region and scales it to the VLM input resolution.

```
filesrc location=video.mp4 ! decodebin3 !
gvadetect model=model.xml device=GPU ! queue ! gvatrack !
tee name=t
  t. ! queue ! gvafpscounter ! fakesink async=false
  t. ! queue leaky=downstream !
       gvaselection_py ! videoconvert ! video/x-raw,format=RGB !
       gvacrop_py !
       gvagenai model-path=vlm_model device=GPU prompt-path=prompt.txt
           generation-config="max_new_tokens=15" chunk-size=1 !
       gvametapublish file-format=json-lines file-path=results.jsonl !
       gvawatermark ! videoconvert ! jpegenc !
       multifilesink location=snap-%05d.jpeg
```

> **Align crop resolution to VLM tile size and object aspect ratio.**
> Use multiples of the model's effective tile size, and match the crop shape
> to the target object class. Letterbox (black-pad) to preserve proportions.
>
> | Model | Tile | Square | Portrait (person) | Landscape (vehicle) |
> |-------|------|--------|--------------------|---------------------|
> | Qwen2.5-VL | 28 | 448×448 | 224×336 | 336×224 |
> | InternVL3 | 448 | 448×448 | 448×896 | 896×448 |
> | MiniCPM-V | 448 | 448×448 | 448×896 | 896×448 |
> | SmolVLM2 | 364 | 364×364 | 364×728 | 728×364 |
>
> Use **portrait** for standing persons/workers, **landscape** for vehicles,
> **square** for faces, seated persons, or mixed objects (default).
>
> **Never upscale beyond the source region.** Choose the crop resolution
> closest to — but not larger than — the detected bounding box dimensions.
> Upscaling fabricated pixels adds no information and wastes VLM tokens;
> prefer a smaller tile with letterboxing over an oversized one.


## Pipeline Design Rules

### Caps & Format Negotiation

Let GStreamer and DL Streamer auto-negotiate memory type and pixel format.

- Do **not** insert explicit caps for `video/x-raw(memory:VAMemory)` or `format=NV12`
  between decode and AI elements — inference elements handle this automatically.
- Do **not** force pixel formats (e.g. `format=RGB`) unless an element requires it
  (e.g. custom Python element mapping buffers to numpy).
- Prefer `device=GPU` or `device=NPU`.

### Element & Device Selection

Use `gvadetect` for detection, `gvaclassify` for classification/OCR, `gvagenai` for VLMs.
Model-proc files are deprecated. Only fall back to a custom Python element when the model
requires custom pre/post-processing. Add `queue` after every inference element to decouple
threading.

| Model Type | Recommended Device |
|------------|-------------------|
| Object detection (YOLO, SSD) | **GPU** |
| Classification / OCR | **NPU** or **GPU** |
| VLM (gvagenai) | **GPU** |
| CV + VLM | **NPU** and **GPU** |

Use NPU for secondary models on Core Ultra 3. Prefer GPU for all models on Core Ultra 1/2.

> **Model precision selection:** Prefer **FP16** (or **INT8** if available) over FP32 for
> GPU/NPU inference. FP16 uses less memory bandwidth with negligible quality impact.
> Only use FP32 when lower-precision variants are unavailable.

### Output & Metadata

Publish analytics as JSON:

```
gvametaconvert ! gvametapublish file-format=json-lines file-path=results.jsonl
```

Use fragmented MP4 (`mp4mux fragment-duration=1000`) for long-running or containerized
pipelines. Add `flush-on-eos=true` to all `queue` elements in multi-branch pipelines.

```
vah264enc ! h264parse ! mp4mux fragment-duration=1000 ! filesink location=output.mp4
```

### Branching

- Use `tee` only when branches genuinely diverge in frame selection, processing rate,
  or sink type. Use a linear pipeline when all elements process the same frames at the
  same rate.
- Place a **single** `gvawatermark` **before** `tee` when multiple branches need overlays:

```
gvadetect ... ! queue ! gvawatermark ! tee name=t
  t. ! queue ! vapostproc ! ... ! comp.sink_N
  t. ! queue ! fakesink async=false sync=false
```

### Decode Robustness

Some containers (`.ts`, `.mkv`, some `.mp4`) have audio tracks. When only video is
processed, use `caps="video/x-raw(ANY)"` to expose only video and avoid `not-linked`
errors from unlinked audio pads:

```
decodebin3 caps="video/x-raw(ANY)"
```

The `ANY` feature ensures all memory types (including `VAMemory`) are accepted.

## Common Gotchas

See [Common Gotchas](./debugging-hints.md#common-gotchas) in the Debugging Hints Reference for
a table of known pitfalls (unplayable MP4, audio track crashes, EOS hangs, etc.) and their mitigations.
