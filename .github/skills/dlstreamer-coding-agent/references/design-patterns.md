# Design Patterns Reference

Patterns initially extracted from existing DLStreamer Python sample apps
and augmented later with learnings from using DLStreamer Coding Agent.
Patterns inlcude references to canonical source file to read when applicable.

## Pattern Selection Table

Map the user's description to one or more of these patterns:

| # | Pattern | When to Apply |
|---|---------|---------------|
| 1 | **Pipeline Core** | Always — every app needs source → decode → sink |
| 2 | **Pad Probe Callback** | User needs simple custom logic, like per-frame metadata inspection or adding overlays |
| 3 | **AppSink Callback** | User wants to continue processing of frames or metadata in their own application |
| 4 | **Dynamic Pipeline Control** | User wants conditional routing or branching (tee + valve) |
| 5 | **Custom Python Element (BaseTransform)** | User needs non-trivial per-frame analytics that reads/writes metadata inside the pipeline |
| 6 | **Custom Python Element (Bin/Sink)** | User needs to manage a secondary sub-pipeline or implement non-trivial handling of output stream |
| 7 | **Cross-Branch Signal Bridge** | User has a tee with branches that must exchange state |
| 8 | **VLM Inference (gvagenai)** | User wants Vision-Language Model inference with prompts |
| 9 | **Asset Resolution** | User expects auto-download of video or model files |
| 10 | **Multi-Stream / Multi-Camera** | User wants to process multiple camera streams in a single pipeline with shared model and cross-stream batching |
| 10v | **Multi-Stream Compositor** | User wants to merge multiple streams into a single composite mosaic view (Pattern 10 variant) |
| 11 | **Separate Model Download Script** | User references HuggingFace, Ultralytics, or optimum-cli models requiring a dedicated export step |
| 12 | **Pipeline Event Loop** | Always — every app needs a bus loop for EOS/ERROR handling; includes optional SIGINT handler, input looping, and stdin command control |

---

## Pattern 1: Pipeline Core

**Every app uses this.** Initialize GStreamer, construct a pipeline, run the event loop.

```python
import gi
gi.require_version("Gst", "1.0")
from gi.repository import Gst

Gst.init(None)
pipeline = Gst.parse_launch("filesrc location=... ! decodebin3 ! ... ! autovideosink")
# ... run event loop ...
pipeline.set_state(Gst.State.NULL)
```

**Read for reference:** `samples/gstreamer/python/hello_dlstreamer/hello_dlstreamer.py`

---

## Pattern 2: Pad Probe Callback

Attach a probe to an element's pad to inspect or modify per-frame metadata without pulling
frames out of the pipeline. Used for counting objects, adding overlay text, or making
runtime decisions such as dropping frames.

```python
def my_probe(pad, info, user_data):
    buffer = info.get_buffer()
    rmeta = GstAnalytics.buffer_get_analytics_relation_meta(buffer)
    if rmeta:
        for mtd in rmeta:
            if isinstance(mtd, GstAnalytics.ODMtd):
                label = GLib.quark_to_string(mtd.get_obj_type())
                # ... process detection ...
    return Gst.PadProbeReturn.OK

# Attach to sink pad of a named element
pipeline.get_by_name("watermark").get_static_pad("sink").add_probe(
    Gst.PadProbeType.BUFFER, my_probe, None)
```

**Required imports:**
```python
gi.require_version("GstAnalytics", "1.0")
from gi.repository import GLib, Gst, GstAnalytics
```

**Read for reference:** `samples/gstreamer/python/hello_dlstreamer/hello_dlstreamer.py`

---

## Pattern 3: AppSink Callback

Pull frames into Python via `appsink` when custom processing is needed outside the
GStreamer pipeline (e.g., logging to a database, calling external APIs).

```python
def on_new_sample(sink, user_data):
    sample = sink.emit("pull-sample")
    if sample:
        buffer = sample.get_buffer()
        rmeta = GstAnalytics.buffer_get_analytics_relation_meta(buffer)
        if rmeta:
            for mtd in rmeta:
                if isinstance(mtd, GstAnalytics.ODMtd):
                    label = GLib.quark_to_string(mtd.get_obj_type())
                    print(f"Detected {label} at pts={buffer.pts}")
        return Gst.FlowReturn.OK
    return Gst.FlowReturn.Flushing

# In pipeline string use:  appsink emit-signals=true name=appsink0
appsink = pipeline.get_by_name("appsink0")
appsink.connect("new-sample", on_new_sample, None)
```

**Key difference from Pad Probe:** AppSink is a terminal element (end of pipeline).
Pad Probes are mid-pipeline and don't consume the buffer.

**Read for reference:** `samples/gstreamer/python/prompted_detection/prompted_detection.py`

---

## Pattern 4: Dynamic Pipeline Control (Tee + Valve)

Use `tee` to split stream into branches and `valve` to conditionally block/allow
flow on a branch based on inference results from another branch.

```python
class Controller:
    def __init__(self):
        self.valve = None

    def create_pipeline(self):
        pipeline = Gst.parse_launch("""
            filesrc location=... ! decodebin3 ! ...
            tee name=main_tee
              main_tee. ! queue ! gvadetect ... ! gvaclassify name=classifier ! ...
              main_tee. ! queue ! valve name=control_valve drop=false ! ...
        """)
        self.valve = pipeline.get_by_name("control_valve")
        classifier = pipeline.get_by_name("classifier")
        classifier.get_static_pad("sink").add_probe(
            Gst.PadProbeType.BUFFER, self.on_detection, None)

    def on_detection(self, pad, info, user_data):
        # ... inspect metadata ...
        if should_open:
            self.valve.set_property("drop", False)
        else:
            self.valve.set_property("drop", True)
        return Gst.PadProbeReturn.OK
```

**Read for reference:** `samples/gstreamer/python/open_close_valve/open_close_valve_sample.py`

> **Preroll deadlock with `valve drop=true`:** When a valve starts with `drop=true`,
> no buffers reach downstream sinks, which blocks pipeline preroll indefinitely.
> Always add `async=false` to the terminal sink element (`filesink`, `splitmuxsink`)
> in valve-gated branches so the pipeline transitions to PLAYING without waiting
> for a buffer that will never arrive while the valve is closed.
>
> ```
> tee name=t
>   t. ! queue ! ...  # always-on branch
>   t. ! queue ! valve name=rec drop=true ! ... ! filesink location=out.mp4 async=false
> ```

---

## Pattern 5: Custom Python GStreamer Element (BaseTransform)

Create a custom in-pipeline analytics element by subclassing `GstBase.BaseTransform`.
The element processes each buffer in `do_transform_ip` and can read/write metadata.
Use Custom Python elements instead of Probes if custom logic is complex and/or when it modifies buffers or metadata.

Do NOT create a BaseTransform element whose only purpose is to read detection/classification
metadata, track simple state (e.g. label filtering, cooldown counters, hysteresis), and
expose the result as a GObject property or "fake" metadata for a downstream element.
This is a "glue element" anti-pattern — the downstream element (e.g. a Bin/Sink recorder)
should read GstAnalytics metadata directly from the buffer and implement such logic internally.

> **Rule of thumb:** A custom BaseTransform element is justified only when it implements
> **new derived analytics** (e.g. zone intersection, trajectory analysis, dwell-time
> calculation) that produces metadata not available from existing DLStreamer elements
> or introduces new behavior like dynamic selection of output pads or frame drop/pass.


```python
import gi
gi.require_version("GstBase", "1.0")
gi.require_version("GstAnalytics", "1.0")
from gi.repository import Gst, GstBase, GObject, GLib, GstAnalytics
Gst.init_python()

GST_BASE_TRANSFORM_FLOW_DROPPED = Gst.FlowReturn.CUSTOM_SUCCESS

class MyAnalytics(GstBase.BaseTransform):
    __gstmetadata__ = ("My Analytics", "Transform",
                       "Description of what it does",
                       "Author Name")

    __gsttemplates__ = (
        Gst.PadTemplate.new("src", Gst.PadDirection.SRC,
                            Gst.PadPresence.ALWAYS, Gst.Caps.new_any()),
        Gst.PadTemplate.new("sink", Gst.PadDirection.SINK,
                            Gst.PadPresence.ALWAYS, Gst.Caps.new_any()),
    )

    _my_param = 100

    @GObject.Property(type=int)
    def my_param(self):
        return self._my_param

    @my_param.setter
    def my_param(self, value):
        self._my_param = value

    def do_transform_ip(self, buffer):
        rmeta = GstAnalytics.buffer_get_analytics_relation_meta(buffer)
        if not rmeta:
            return Gst.FlowReturn.OK  # pass frame downstream

        for mtd in rmeta:
            if isinstance(mtd, GstAnalytics.ODMtd):
                # ... custom analytics logic ...
                return Gst.FlowReturn.OK  # pass frame downstream

        return GST_BASE_TRANSFORM_FLOW_DROPPED  # no relevant detections → drop

GObject.type_register(MyAnalytics)
__gstelementfactory__ = ("myanalytics_py", Gst.Rank.NONE, MyAnalytics)
```

**File location:** Place in `plugins/python/<element_name>.py`

**Registration:** See [Plugin Registration](./coding-conventions.md#plugin-registration) in the Coding Conventions Reference.

**Read for reference:** `samples/gstreamer/python/smart_nvr/plugins/python/gvaAnalytics.py`,
`samples/gstreamer/python/vlm_self_checkout/plugins/python/gvaFrameSelection.py`

---

## Pattern 6: Custom Python GStreamer Element (Bin / Sink)

Create a composite element that encapsulates an internal sub-pipeline (e.g., encoder +
muxer + file sink). Subclass `Gst.Bin` and expose a ghost pad.

```python
class MyRecorder(Gst.Bin):
    __gstmetadata__ = ("My Recorder", "Sink",
                       "Record video to chunked files", "Author")

    _location = "output.mp4"

    @GObject.Property(type=str)
    def location(self):
        return self._location

    @location.setter
    def location(self, value):
        self._location = value
        self._filesink.set_property("location", value)

    def __init__(self):
        super().__init__()
        self._convert = Gst.ElementFactory.make("videoconvert", "convert")
        self._encoder = Gst.ElementFactory.make("vah264enc", "encoder")
        self._filesink = Gst.ElementFactory.make("splitmuxsink", "sink")
        self.add(self._convert)
        self.add(self._encoder)
        self.add(self._filesink)
        self._convert.link(self._encoder)
        self._encoder.link(self._filesink)
        self.add_pad(Gst.GhostPad.new("sink", self._convert.get_static_pad("sink")))

GObject.type_register(MyRecorder)
__gstelementfactory__ = ("myrecorder_py", Gst.Rank.NONE, MyRecorder)
```

**Read for reference:** `samples/gstreamer/python/smart_nvr/plugins/python/gvaRecorder.py`

> **Decision shortcut — recording / conditional output:** If the user describes *event-triggered
> recording*, *conditional saving*, or *numbered output files*, go directly to this pattern.
> A `Gst.Bin` subclass with an internal `appsrc → encoder → mux → filesink` sub-pipeline is
> the only approach that can cleanly start/stop recordings and finalize MP4 containers (which
> require an EOS event to write the moov atom). Do **not** attempt this with pad probes,
> appsink callbacks, or tee+valve — those patterns cannot manage a secondary pipeline lifecycle.

---

## Pattern 7: Cross-Branch Signal Bridge

When a `tee` splits a pipeline into branches that must exchange state (e.g., detection
results from branch A control overlay in branch B), use a GObject signal bridge for low-frequency events.

```python
class SignalBridge(GObject.Object):
    def __init__(self):
        super().__init__()
        self._last_label = None

    @GObject.Signal(arg_types=(GObject.TYPE_UINT, GObject.TYPE_DOUBLE,
                                GObject.TYPE_UINT64, GObject.TYPE_UINT64))
    def detection_result(self, label_quark, confidence, pts, time_ns):
        self._last_label = label_quark

# Attach probes on both branches, passing the bridge as user_data:
bridge = SignalBridge()
pipeline.get_by_name("analytics").get_static_pad("src").add_probe(
    Gst.PadProbeType.BUFFER, analytics_cb, bridge)
pipeline.get_by_name("watermark").get_static_pad("sink").add_probe(
    Gst.PadProbeType.BUFFER, overlay_cb, bridge)
```

**Read for reference:** `samples/gstreamer/python/vlm_self_checkout/vlm_self_checkout.py`

---

## Pattern 8: VLM Inference (gvagenai)

Use the `gvagenai` element for Vision-Language Model inference. Prompt can be inline or
from a file. Results attach as GstGVATensorMeta, displayed by `gvawatermark`.

```python
pipeline_str = (
    f'filesrc location="{video}" ! decodebin3 ! '
    f'gvagenai model-path="{model_dir}" device=GPU '
    f'prompt-path="{prompt_file}" '
    f'generation-config="max_new_tokens=1,num_beams=4" '
    f'chunk-size=1 frame-rate=1.0 metrics=true ! '
    f'gvametapublish file-format=json-lines file-path="{output_json}" ! '
    f'queue ! gvafpscounter ! gvawatermark ! '
    f'videoconvert ! vah264enc ! h264parse ! mp4mux ! '
    f'filesink location="{output_video}"'
)
```

**Read for reference:** `samples/gstreamer/python/vlm_alerts/vlm_alerts.py`

---

## Pattern 9: Asset Resolution (Video + Model Download)

Add Python functions to download assets (such as input video files) and AI models.
Always cache downloaded files locally, so only first application run requires network connection.
For AI model download, prioritize using existing download scripts and generate inline only if simple.

> **Video download method:** Use `subprocess` + `curl` (not `urllib.request`) for video
> downloads. Many video hosting sites (Pexels, Pixabay, etc.) block Python's `urllib`
> with HTTP 403 even with a custom `User-Agent`. `curl` with `-L` (follow redirects)
> and a `Referer` header works reliably.

> **Pexels URLs:** Users often provide the Pexels *page* URL
> (e.g. `https://www.pexels.com/video/<slug>-<ID>/`). The actual video file is at
> `https://videos.pexels.com/video-files/<ID>/<ID>-hd_<W>_<H>_<FPS>fps.mp4`
> but the resolution and FPS **vary per video** — do **not** guess them.
> You **must** scrape the Pexels page to discover the exact `.mp4` URL.
> Use `subprocess` to run `curl -s` on the page URL and search the returned HTML
> for `videos.pexels.com/video-files/` links. The Canva "Edit" links on the page
> embed the direct video URL as the `file-url=` query parameter, e.g.:
> `https://www.canva.com/...&file-url=https%3A%2F%2Fvideos.pexels.com%2Fvideo-files%2F9492063%2F9492063-hd_1920_1080_30fps.mp4&...`
> URL-decode the `file-url` value to get the direct download link.
> If scraping fails, ask the user for the direct video-file URL.

> **Edge AI Resources videos:** If a user does not provide specific video files, prefer
> **Pexels direct video-file URLs** (e.g. `https://videos.pexels.com/video-files/<ID>/<ID>-hd_<W>_<H>_<FPS>fps.mp4`)
> as default test videos. These are reliable, direct-download, and do not require
> authentication or LFS resolution.
>
> As an alternative, videos from `https://github.com/open-edge-platform/edge-ai-resources/tree/main/videos`
> can be used, but **beware of Git LFS**: `curl -L` on
> `github.com/.../raw/main/videos/<file>.mp4` may return an HTML redirect page instead
> of the actual video data if the file is stored in Git LFS. Always verify the downloaded
> file is a valid video. Use a Python binary header check:
>
> ```python
> with open(local_path, "rb") as f:
>     header = f.read(64)
> if b"<html" in header.lower() or b"<!doctype" in header.lower():
>     # Downloaded file is HTML, not a video
> ```
>
> If LFS downloads fail, fall back to Pexels URLs or mount locally available video files.
>
> **Note:** `.ts` files contain audio tracks — apply [Rule 8](./pipeline-construction.md#rule-8--handle-audio-tracks-in-video-only-pipelines) to filter non-fatal audio errors.

```python
from pathlib import Path
import subprocess

VIDEOS_DIR = Path(__file__).resolve().parent / "videos"
MODELS_DIR = Path(__file__).resolve().parent / "models"

def download_video(url: str) -> Path:
    VIDEOS_DIR.mkdir(parents=True, exist_ok=True)
    filename = url.rstrip("/").split("/")[-1]
    local = VIDEOS_DIR / filename
    if not local.exists():
        print(f"Downloading video: {url}")
        subprocess.run([
            "curl", "-L", "-o", str(local),
            "-H", "Referer: https://www.pexels.com/",
            "-H", "User-Agent: Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36",
            url,
        ], check=True, timeout=300)
        print(f"Saved to: {local}")
    return local.resolve()

def download_model(model_name: str) -> Path:
    MODELS_DIR.mkdir(parents=True, exist_ok=True)
    model_path = MODELS_DIR / model_name
    if not model_path.exists():
        import subprocess
        script = Path(__file__).resolve().parents[3] / "download_public_models.sh"
        subprocess.run([str(script), model_name, str(MODELS_DIR)], check=True)
    return model_path.resolve()
```

**Read for reference:** `samples/gstreamer/python/vlm_self_checkout/vlm_self_checkout.py`

---

## Pattern 10: Multi-Stream / Multi-Camera (In-Process)

Run multiple camera streams within a **single GStreamer pipeline** so they share model
instances and benefit from cross-stream batching. This is the preferred approach for
multi-camera analytics — it maximizes GPU utilization and reduces memory footprint
compared to per-camera subprocesses.

### Model Sharing and Cross-Stream Batching

- **Shared model instance:** Set `model-instance-id=<name>` on inference elements
  to share the same OpenVINO model instance across all streams.
  own `model-instance-id`.
- **Cross-stream batching:** Set `batch-size=<stream_count>` to batch frames from
  different streams in a single inference call; requires shared model instance.
- **Scheduling policy for cross-stream batching:** A model shared by `model-instance-id`
  serves incoming stream on first-in / first-out basis to achieve highest throuhput.
  This may result in temporal bubbles across streams, expecially during startup phase
  (some streams may start sooner than others and will get more frames processed).
  A temporal bubble may create an issue if streams are synchronized later
  in a pipeline using elements like `vacompositor`. In such , you **must** set
  `scheduling-policy=latency`  on all inference which use common model-instance-id
  The `latency` policy processes frames in order of their presentation timestamp,
  which effectively resolves to round-robin policy.

```python
from pathlib import Path

def build_pipeline(sources: list, model_xml: str, device: str) -> str:
    """Build a multi-stream pipeline with shared model and per-stream output."""
    n = len(sources)
    parts = []
    for i, src in enumerate(sources):
        s = (
            f'filesrc location="{src}" ! decodebin3 ! '
            f'gvadetect model="{model_xml}" device={device} '
            f'model-instance-id=detect_instance0 batch-size={n} ! '
            f'queue flush-on-eos=true ! '
            f'gvafpscounter ! fakesink'
        )
        parts.append(s)
    return " ".join(parts)

pipeline = Gst.parse_launch(build_pipeline(cameras, model, "GPU"))
```

### Variant: Multi-Stream Compositor (mosaic output)

To merge all streams into a single composite view, use `vacompositor` to perform
GPU-accelerated composition entirely in VA memory. This avoids expensive CPU-side
`videoconvertscale` and achieves significantly higher FPS than the CPU `compositor`.

```python
def build_compositor_pipeline(sources, model_xml, device, tw=640, th=360):
    n = len(sources)
    cols = 2
    rows = (n + cols - 1) // cols

    # VA compositor with programmatic pad positions (GPU memory throughout)
    comp = "vacompositor name=comp "
    for i in range(n):
        comp += f"sink_{i}::xpos={i % cols * tw} sink_{i}::ypos={i // cols * th} "
    comp += (
        "! vah264enc ! h264parse "
        "! mp4mux fragment-duration=1000 ! filesink location=mosaic.mp4 "
    )

    # Per-stream branches: decode → infer → scale (VA memory) → compositor
    for i, src in enumerate(sources):
        comp += (
            f'filesrc location="{src}" ! decodebin3 ! '
            f'gvadetect model="{model_xml}" device={device} '
            f'model-instance-id=instance0 batch-size={n} '
            f'scheduling-policy=latency ! '
            f'queue flush-on-eos=true ! gvafpscounter ! '
            f'gvametaconvert ! gvametapublish file-format=json-lines '
            f'file-path="results/cam{i}.jsonl" ! '
            f'vapostproc ! video/x-raw(memory:VAMemory),width={tw},height={th} ! '
            f'queue ! comp.sink_{i} '
        )
    return comp
```

> **When to use subprocess orchestration instead:** Only when streams must run as
> fully independent processes (e.g. different models per camera, fault isolation
> between cameras, or separate machines). For that approach, see
> `samples/gstreamer/python/onvif_cameras_discovery/dls_onvif_sample.py`.

---

## Pattern 11: Separate Model Download Script

When an application uses models from Ultralytics, HuggingFace Transformers, PaddlePaddle,
or other frameworks with long list of run-time dependencies, create a **separate `download_models.py`**
script that handles all model download and export. Users run it once before starting the pipeline application.

In addition, model export dependencies may clash with model inference dependencies which further
justifies splitting these two phases.

---

## Pattern 12: Pipeline Event Loop

Every DLStreamer Python app ends with a pipeline event loop that listens for EOS and
ERROR messages on the GStreamer bus. The single `run_pipeline()` function below is the
**canonical implementation** — it includes all optional blocks, each marked with
`[Optional]`. Remove or keep them based on your application's needs.

```python
import signal
import sys
import threading
from gi.repository import GLib, Gst


# ── [Optional] Pattern 13: Runtime Command Control (stdin) ───────────────────
# Accept user commands while the pipeline is running.
# A daemon thread reads sys.stdin and dispatches to the GLib main loop
# via GLib.idle_add() — the only thread-safe way to mutate pipeline state.

class CommandReader:
    """Read commands from stdin and dispatch to the GLib main loop."""

    def __init__(self, pipeline):
        self.pipeline = pipeline
        self.shutdown_requested = False
        self._commands = {
            "quit": self._quit,
            # Add app-specific commands here, e.g.:
            # "record": self._record,
            # "stop":   self._stop,
        }

    def start(self):
        thread = threading.Thread(target=self._read_loop, daemon=True)
        thread.start()

    def _read_loop(self):
        try:
            for line in sys.stdin:
                parts = line.strip().lower().split()
                if not parts:
                    continue
                handler = self._commands.get(parts[0])
                if handler:
                    GLib.idle_add(handler, *parts[1:])
                else:
                    print(f"Unknown command: {parts[0]}")
        except EOFError:
            pass

    def _quit(self, *args):
        self.shutdown_requested = True
        self.pipeline.send_event(Gst.Event.new_eos())
        return GLib.SOURCE_REMOVE


# ── Pipeline event loop ─────────────────────────────────────────────────────

def run_pipeline(pipeline, cmd_reader=None, loop_count=1):
    """Unified event loop with optional SIGINT handling, looping, and command control.

    Args:
        cmd_reader:  [Optional] A CommandReader instance. Pass None to disable
                     stdin command control (Pattern 13).
        loop_count:  [Optional] 1 = play once (default), N = play N times,
                     0 = infinite. On EOS, seeks back to start. Ignored for RTSP.
    """
    remaining = loop_count - 1  # -1 means infinite when loop_count == 0

    # [Optional] SIGINT → EOS handler for graceful Ctrl+C shutdown.
    # For long-running pipelines you may prefer SIGINT → set_state(NULL)
    # for immediate stop, or omit this and rely on the quit command.
    def _sigint_handler(signum, frame):
        nonlocal remaining
        remaining = 0  # stop looping on Ctrl+C
        pipeline.send_event(Gst.Event.new_eos())

    prev = signal.signal(signal.SIGINT, _sigint_handler)
    bus = pipeline.get_bus()
    pipeline.set_state(Gst.State.PLAYING)
    try:
        while True:
            # [Optional] Pump GLib default context so GLib.idle_add() callbacks
            # fire. Required when using CommandReader (Pattern 13) or any
            # thread-safe dispatch via GLib.idle_add(). No-op otherwise.
            while GLib.MainContext.default().iteration(False):
                pass

            msg = bus.timed_pop_filtered(
                100 * Gst.MSECOND,
                Gst.MessageType.ERROR | Gst.MessageType.EOS,
            )

            # [Optional] Check if shutdown was requested via command or SIGINT
            if cmd_reader and cmd_reader.shutdown_requested and msg is None:
                break

            if msg is None:
                continue
            if msg.type == Gst.MessageType.ERROR:
                err, debug = msg.parse_error()
                raise RuntimeError(f"Pipeline error: {err.message}\nDebug: {debug}")
            if msg.type == Gst.MessageType.EOS:
                # [Optional] Loop file inputs by seeking back to start.
                # Remove this block for single-pass pipelines.
                if remaining != 0:
                    if remaining > 0:
                        remaining -= 1
                    print(f"Looping input ({remaining if remaining >= 0 else '∞'} remaining)...")
                    pipeline.seek_simple(
                        Gst.Format.TIME,
                        Gst.SeekFlags.FLUSH | Gst.SeekFlags.KEY_UNIT,
                        0,
                    )
                    continue
                print("Pipeline complete.")
                break
    finally:
        signal.signal(signal.SIGINT, prev)
        pipeline.set_state(Gst.State.NULL)
```

### Usage examples

**Minimal (file-based, single pass):**
```python
run_pipeline(pipeline)
```

**Long-running with looping:**
```python
run_pipeline(pipeline, loop_count=0)  # loop input videos infinitly, Ctrl+C to stop
```

**With stdin command control:**
```python
cmd_reader = CommandReader(pipeline)
cmd_reader.start()
run_pipeline(pipeline, cmd_reader=cmd_reader, loop_count=3)
```

### Key rules for CommandReader

- **Never** mutate GStreamer element properties or state from the reader thread.
  Always use `GLib.idle_add(callback, ...)` to schedule work on the main loop.
- Return `GLib.SOURCE_REMOVE` from `idle_add` callbacks (one-shot execution).
- Use a `daemon=True` thread so it doesn't block process exit.
- For Docker testing, pipe commands via a FIFO:
  `mkfifo /tmp/ctrl && (sleep 10; echo "record 0") > /tmp/ctrl & python3 app.py < /tmp/ctrl`

> **GLib context pump:** `bus.timed_pop_filtered()` does **not** pump the GLib default
> main context. Without the `GLib.MainContext.default().iteration(False)` call,
> `GLib.idle_add()` callbacks will be silently queued but **never executed**.

**Read for reference:** `samples/gstreamer/python/hello_dlstreamer/hello_dlstreamer.py`

