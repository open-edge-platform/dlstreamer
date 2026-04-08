# Pipeline Construction Reference

This reference covers how to build DLStreamer command line pipelines or Python applications.

## DLStreamer GStreamer Elements

This section lists elements commonly used in DLStreamer pipelines. 
For full list of DLStreamer elements see also `../../../../docs/user-guide/elements/`.

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
| `decodebin3` | Auto-select decoder | Uses hardware decode when available |

### Video Processing

| Element | Purpose | Key Properties |
|---------|---------|----------------|
| `videoconvertscale` | Format conversion + scaling | Combined convert+scale |
| `videoconvert` | Pixel format conversion only | |
| `videoscale` | Resolution scaling only | |
| `videorate` | Frame rate adjustment | |
| `vapostproc` | VA-API hardware post-processing | Use before `video/x-raw(memory:VAMemory)` caps |

### AI Inference (DLStreamer-specific)

| Element | Purpose | Model Types | Key Properties |
|---------|---------|-------------|----------------|
| `gvadetect` | Object detection | YOLO, SSD, RT-DETR, D-FINE | `model`, `device`, `batch-size`, `threshold` |
| `gvaclassify` | Classification & OCR | ResNet, EfficientNet, CLIP, ViT, PaddleOCR | `model`, `device`, `batch-size` |
| `gvagenai` | VLM / GenAI inference | MiniCPM-V, Qwen2.5-VL, InternVL, SmolVLM | `model-path`, `device`, `prompt`, `generation-config`, `frame-rate`, `chunk-size` |

> **See Rule 3 below** for guidance on choosing the correct element for each model type.

### Tracking

| Element | Purpose | Key Properties |
|---------|---------|----------------|
| `gvatrack` | Object tracking across frames | `tracking-type=zero-term-imageless` |

### Overlay & Metrics

| Element | Purpose | Key Properties |
|---------|---------|----------------|
| `gvawatermark` | Draw bounding boxes and labels on video | `device=CPU`, `displ-cfg=...` |
| `gvafpscounter` | Print FPS to stdout | (no key properties) |

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
| `appsink` | Pull frames into application code | `emit-signals=true`, `name=<name>` |
| `jpegenc` | Encode frames as JPEG | |

### Custom Logic

If a user pipeline requires custom processing, add new Python GStreamer elements in:  
- `plugins/python/<element_name>.py`

For new development, prefer custom Python GStreamer elements in `plugins/python/` over `gvapython`.

## Common Pipeline Patterns

### Pattern 1: Decode → Detect → Watermark → Display

```
filesrc location=video.mp4 ! decodebin3 !
gvadetect model=model.xml device=GPU batch-size=4 ! queue !
gvawatermark ! videoconvertscale ! autovideosink
```

### Pattern 2: Decode → Detect → Classify → Encode → Save

```
filesrc location=video.mp4 ! decodebin3 !
gvadetect model=detect.xml device=GPU batch-size=4 ! queue !
gvaclassify model=classify.xml device=GPU batch-size=4 ! queue !
gvafpscounter ! gvawatermark !
gvametaconvert ! gvametapublish file-format=json-lines file-path=results.jsonl !
videoconvert ! vah264enc ! h264parse ! mp4mux !
filesink location=output.mp4
```

> **Multi-device tip:** Inference elements can use different devices. For example, run
> heavyweight detection on GPU and lightweight OCR/classification on NPU:
> `gvadetect ... device=GPU` → `gvaclassify ... device=NPU`. This balances load and
> avoids GPU contention.

### Pattern 3: VLM Alerting with JSON + Video Output

```
filesrc location=video.mp4 ! decodebin3 !
gvagenai model-path=model_dir device=GPU prompt-path=prompt.txt
    generation-config="max_new_tokens=1,num_beams=4"
    chunk-size=1 frame-rate=1.0 metrics=true !
gvametapublish file-format=json-lines file-path=results.jsonl ! queue !
gvafpscounter ! gvawatermark name=watermark ! videoconvert !
vah264enc ! h264parse ! mp4mux ! filesink location=output.mp4
```

### Pattern 4: Tee → Dual-Branch (display + analytics)

```
filesrc location=video.mp4 ! decodebin3 !
gvadetect model=model.xml device=GPU ! queue ! gvatrack !
tee name=t
  t. ! queue ! gvawatermark ! videoconvert ! autovideosink
  t. ! queue ! <analytics_branch> ! gvametapublish file-path=results.jsonl
```

### Pattern 5: Detect → Track → Custom Python Element

```
filesrc location=video.mp4 ! decodebin3 !
gvadetect model=model.xml device=GPU threshold=0.7 ! queue !
gvaanalytics_py distance=500 angle=-135,-45 !
gvafpscounter ! gvawatermark !
gvarecorder_py location=output.mp4 max-time=10
```

### Pattern 6: Detect + VLM (multi-branch with frame selection)

```
filesrc location=video.mp4 ! decodebin3 !
gvafpsthrottle target-fps=30 !
gvadetect model=detect.xml device=GPU threshold=0.4 ! queue !
gvatrack tracking-type=zero-term-imageless !
tee name=detect_tee

  detect_tee. ! queue !
  gvawatermark name=watermark ! gvafpscounter !
  vah264enc ! h264parse ! mp4mux ! filesink location=output.mp4

  detect_tee. ! queue !
  gvaframeselection_py name=selection threshold=1500 !
  vapostproc ! video/x-raw,format=NV12,width=640,height=360 !
  gvagenai name=vlm model-path=vlm_dir device=GPU
      prompt="Describe items" generation-config="max_new_tokens=50"
      chunk-size=1 metrics=true !
  gvametapublish file-format=json-lines file-path=results.jsonl !
  jpegenc ! multifilesink location=snapshots-%d.jpeg
```

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

Choose the correct DLStreamer inference element based on model type:

| Model Type | Element | Examples |
|------------|---------|----------|
| Object detection | `gvadetect` | YOLO, SSD, RT-DETR, D-FINE |
| Classification / OCR | `gvaclassify` | ResNet, EfficientNet, CLIP, ViT, PaddleOCR |
| Vision-Language Models | `gvagenai` | MiniCPM-V, Qwen2.5-VL, InternVL, SmolVLM |

Use `gvaclassify` for OCR models (e.g. PaddleOCR text recognition) and classification
models. DLStreamer handles pre/post-processing automatically via model metadata —
no model-proc files are needed (model-proc is deprecated). Only fall back to a custom
Python element (Pattern 6 in Design Patterns) when the model requires custom
pre/post-processing that DLStreamer cannot handle automatically.

### Rule 4 — Use queue element after Inference Elements

Inference elements like `gvadetect` or `gvaclassify` are asynchronous and they process output tensors in the context of OpenVINO inference engine threads. Use `queue` elements following inference elements to transfer processing to another thread. 

### Rule 5 — Use `gvametapublish` for JSON Output

Use `gvametaconvert` followed by `gvametapublish` as the standard way to export inference results to JSON:

```
gvametaconvert ! gvametapublish file-format=json-lines file-path=results.jsonl
```

Do not write custom file-output logic in pad probes or custom elements when
`gvametapublish` can handle the use case.

## Python Pipeline Construction Approaches

### Approach 1: `Gst.parse_launch` (preferred for most apps)

Build the pipeline from a string that mirrors `gst-launch-1.0` syntax. Use named elements
(`name=foo`) to retrieve references for probes or property changes later.

```python
pipeline = Gst.parse_launch(
    f'filesrc location="{video_file}" ! decodebin3 ! '
    f'gvadetect model="{model_file}" device=GPU batch-size=4 ! queue ! '
    f'gvawatermark name=watermark ! videoconvertscale ! autovideosink'
)
# Retrieve named elements for probes
watermark = pipeline.get_by_name("watermark")
```

Source: `samples/gstreamer/python/hello_dlstreamer/hello_dlstreamer.py`

**When to use:** Any pipeline assembled from known elements. Covers 90% of use cases.

### Approach 2: Programmatic element creation

Create elements individually with `Gst.ElementFactory.make`, set properties, add to pipeline,
and link manually. Required when linking must happen dynamically (e.g., `decodebin3` pad-added).

```python
pipeline = Gst.Pipeline()
source = Gst.ElementFactory.make("filesrc", "file-source")
decoder = Gst.ElementFactory.make("decodebin3", "media-decoder")
detect = Gst.ElementFactory.make("gvadetect", "object-detector")

source.set_property("location", video_file)
detect.set_property("model", model_file)
detect.set_property("device", "GPU")

pipeline.add(source)
pipeline.add(decoder)
pipeline.add(detect)
source.link(decoder)
decoder.connect("pad-added",
    lambda el, pad, sink: el.link(sink)
        if "video" in pad.get_name() and not pad.is_linked() else None,
    detect)
detect.link(queue)
```

Source: `samples/gstreamer/python/hello_dlstreamer/hello_dlstreamer_full.py`

**When to use:** Only when dynamic pad negotiation or runtime element insertion is needed.

## Pipeline Event Loop

Every DLStreamer Python app ends with a pipeline event loop. Two variants exist:

### Simple loop (file-based input):

```python
def pipeline_loop(pipeline):
    bus = pipeline.get_bus()
    pipeline.set_state(Gst.State.PLAYING)
    terminate = False
    while not terminate:
        msg = bus.timed_pop_filtered(
            Gst.CLOCK_TIME_NONE,
            Gst.MessageType.EOS | Gst.MessageType.ERROR)
        if msg:
            if msg.type == Gst.MessageType.ERROR:
                _, debug_info = msg.parse_error()
                print(f"Error from {msg.src.get_name()}: {debug_info}")
                terminate = True
            if msg.type == Gst.MessageType.EOS:
                print("Pipeline complete.")
                terminate = True
    pipeline.set_state(Gst.State.NULL)
```

### Interruptible loop (long-running / RTSP):

```python
import signal

def run_pipeline(pipeline):
    def _sigint_handler(signum, frame):
        pipeline.send_event(Gst.Event.new_eos())
    prev = signal.signal(signal.SIGINT, _sigint_handler)

    bus = pipeline.get_bus()
    pipeline.set_state(Gst.State.PLAYING)
    try:
        while True:
            msg = bus.timed_pop_filtered(
                100 * Gst.MSECOND,
                Gst.MessageType.ERROR | Gst.MessageType.EOS)
            if msg is None:
                continue
            if msg.type == Gst.MessageType.ERROR:
                err, debug = msg.parse_error()
                raise RuntimeError(f"Pipeline error: {err.message}\nDebug: {debug}")
            if msg.type == Gst.MessageType.EOS:
                break
    finally:
        signal.signal(signal.SIGINT, prev)
        pipeline.set_state(Gst.State.NULL)
```
