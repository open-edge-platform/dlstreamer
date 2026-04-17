# Design Patterns Reference

Design patterns to follow when building Python sample applications.
Patterns related to custom Python elements also apply to command-line applications that reuse custom Python elements.

## Pattern Selection Table

Map the user's description to one or more of these patterns:

| # | Pattern | When to Apply |
|---|---------|---------------|
| 1 | **Pipeline Core** | Always — every app needs source → decode → sink |
| 2 | **Pipeline Event Loop** | Always — every app needs an event loop to advance execution |
| 3 | **Multi-Stream / Multi-Camera** | User wants to process multiple camera streams in a single pipeline with shared model and cross-stream batching |
| 4 | **Multi-Stream Compositor** | User wants to merge multiple streams into a single composite mosaic view |
| 5 | **Pad Probe Callback** | User needs simple custom logic, like per-frame metadata inspection or adding overlays |
| 6 | **AppSink Callback** | User wants to continue processing of frames or metadata in their own application |
| 7 | **Custom Python Element (BaseTransform)** | User needs non-trivial per-frame analytics that reads/writes metadata inside the pipeline |
| 8 | **Custom Python Element (Bin/Sink)** | User needs to manage a secondary sub-pipeline or implement non-trivial handling of output stream |
| 9 | **Dynamic Pipeline Control** | User wants conditional routing or branching (tee + valve) |
| 10 | **Cross-Branch Signal Bridge** | User has a tee with branches that must exchange state |
| 11 | **Asset Resolution** | User expects auto-download of video or model files as part of a Python application |
| 12 | **Model Resolution** | User expects AI model download and export as part of a Python application |

---

## Pattern 1: Pipeline Core

**Every app uses this.** Initialize GStreamer, construct a pipeline from a GStreamer syntax
string (see [Pipeline Construction Reference](./pipeline-construction.md) for element tables
and example pipelines), then run the event loop.

### Approach 1: `Gst.parse_launch` (preferred for most apps)

Build the pipeline from a string that mirrors `gst-launch-1.0` syntax and instantiate
pipeline with `Gst.parse_launch()` method.

```python
import gi
gi.require_version("Gst", "1.0")
from gi.repository import Gst

Gst.init(None)
pipeline = Gst.parse_launch("filesrc location=... ! decodebin3 ! ... ! autovideosink")
# ... run event loop ...
pipeline.set_state(Gst.State.NULL)
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

**Read for reference:** `samples/gstreamer/python/hello_dlstreamer/hello_dlstreamer.py`

---

## Pattern 2: Pipeline Event Loop

Every DL Streamer Python app ends with a pipeline event loop that listens for EOS and
ERROR messages on the GStreamer bus. The single `run_pipeline()` function below is the
**canonical implementation** — it includes all optional blocks, each marked with
`[Optional]`. Remove or keep them based on your application's needs.

```python
import signal
import sys
import threading
from gi.repository import GLib, Gst


# ── [Optional] Runtime Command Control (stdin) ──────────────────────────────
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
                     stdin command control.
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
            # fire. Required when using CommandReader or any
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

                # [Optional] Filter non-fatal audio errors — see Rule 8 in
                # Pipeline Construction Reference. Remove this block if the
                # pipeline never encounters audio-track containers (.ts, .mkv).
                src_name = msg.src.get_name().lower()
                err_text = err.message.lower()
                if "missing" in err_text or "audio" in src_name:
                    print(f"Warning (non-fatal): {err.message} from {msg.src.get_name()}")
                    continue  # Do NOT terminate the pipeline

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
run_pipeline(pipeline, loop_count=0)  # loop input videos infinitely, Ctrl+C to stop
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

---

## Pattern 3: Multi-Stream / Multi-Camera (In-Process)

Run multiple camera streams within a **single GStreamer pipeline** so they share model
instances and benefit from cross-stream batching. This is the preferred approach for
multi-camera analytics — it maximizes GPU utilization and reduces memory footprint
compared to per-camera subprocesses.

### Model Sharing and Cross-Stream Batching

- **Shared model instance:** Set `model-instance-id=<name>` on inference elements
  to share the same OpenVINO model instance across all streams. Each distinct model
  should have its own `model-instance-id`.
- **Cross-stream batching:** Set `batch-size=<stream_count>` to batch frames from
  different streams in a single inference call; requires a shared model instance.
- **Scheduling policy for cross-stream batching:** A model shared via `model-instance-id`
  serves incoming streams on a first-in / first-out basis to achieve the highest throughput.
  This may result in temporal bubbles across streams, especially during the startup phase
  (some streams may start sooner than others and will get more frames processed).
  A temporal bubble may create an issue if streams are synchronized later
  in the pipeline using elements like `vacompositor`. In such cases, you **must** set
  `scheduling-policy=latency` on all inference elements that use a common `model-instance-id`.
  The `latency` policy processes frames in order of their presentation timestamp,
  which effectively resolves to a round-robin policy.

See the Pipeline Construction Reference for GStreamer pipeline syntax:
- [Multi-Stream Analytics](./pipeline-construction.md#example-multi-stream-analytics-n-streams) — N parallel streams with shared model
- [Multi-Stream Compositor](./pipeline-construction.md#example-multi-stream-compositor-n-streams--22-grid-gpu-memory-path) — N streams merged into a 2×2 mosaic via `vacompositor`

### Python: Building Multi-Stream Pipelines Programmatically

When the number of streams is dynamic, construct the pipeline string in a loop.
Use Approach 1 (`Gst.parse_launch`) from Pattern 1 — concatenate per-stream pipeline
fragments that follow the multi-stream examples in the Pipeline Construction Reference.

```python
from pathlib import Path

def build_pipeline(sources: list, model_xml: str, device: str) -> str:
    """Build a multi-stream pipeline with shared model and per-stream output."""
    n = len(sources)
    parts = []
    for i, src in enumerate(sources):
        # Each stream fragment follows the Multi-Stream Analytics example
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

> **When to use subprocess orchestration instead:** Only when streams must run as
> fully independent processes (e.g. different models per camera, fault isolation
> between cameras, or separate machines). For that approach, see
> `samples/gstreamer/python/onvif_cameras_discovery/dls_onvif_sample.py`.

---

## Pattern 4: Multi-Stream Compositor (Mosaic Output)

To merge all streams into a single composite view, use `vacompositor` to perform
GPU-accelerated composition entirely in VA memory. This avoids expensive CPU-side
`videoconvertscale` and achieves significantly higher FPS than the CPU `compositor`.

This pattern builds on [Pattern 3: Multi-Stream / Multi-Camera](#pattern-3-multi-stream--multi-camera-in-process)
— all model sharing, cross-stream batching, and scheduling-policy guidance from Pattern 3 applies here.

Follow the same programmatic loop approach as Pattern 3, but use the
[Multi-Stream Compositor](./pipeline-construction.md#example-multi-stream-compositor-n-streams--22-grid-gpu-memory-path)
example from the Pipeline Construction Reference as the per-stream template.

---

## Pattern 5: Pad Probe Callback

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

**Reading classification metadata** (e.g. from `gvagenai`):
```python
for mtd in rmeta:
    if isinstance(mtd, GstAnalytics.ClsMtd):
        quark = mtd.get_quark(0)
        level = mtd.get_level(0)
```

**Reading tracking metadata:**
```python
for mtd in rmeta:
    if isinstance(mtd, GstAnalytics.TrackingMtd):
        success, tracking_id, _, _, _ = mtd.get_info()
```

**Writing overlay metadata:**
```python
rmeta.add_od_mtd(GLib.quark_from_string("label text"), x, y, w, h, confidence)
```

**Read for reference:** `samples/gstreamer/python/hello_dlstreamer/hello_dlstreamer.py`

---

## Pattern 6: AppSink Callback

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

## Pattern 7: Custom Python GStreamer Element (BaseTransform)

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
> calculation) that produces metadata not available from existing DL Streamer elements
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
        # Do not drop frames before pipeline reaches PLAYING state —
        # sinks need at least one buffer for preroll to complete.
        _, state, _ = self.get_state(0)
        if state != Gst.State.PLAYING:
            return Gst.FlowReturn.OK

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

### Custom Python Element Conventions

- **File location:** `plugins/python/<element_name>.py`
- **Class name:** PascalCase (e.g., `FrameSelection`)
- **Element factory name:** lowercase with `_py` suffix (e.g., `gvaframeselection_py`)
- Must end with: `GObject.type_register(ClassName)` and `__gstelementfactory__ = (...)`
- Must call `Gst.init_python()` after imports
- Properties use `@GObject.Property` decorator
- Transform elements subclass `GstBase.BaseTransform` and implement `do_transform_ip`
- Bin/Sink elements subclass `Gst.Bin` and use `Gst.GhostPad`

### Plugin Registration

The main app must add the plugins directory to `GST_PLUGIN_PATH`, disable the forked
plugin scanner, and verify the Python plugin loader is available:

```python
plugins_dir = str(Path(__file__).resolve().parent / "plugins")
if plugins_dir not in os.environ.get("GST_PLUGIN_PATH", ""):
    os.environ["GST_PLUGIN_PATH"] = f"{os.environ.get('GST_PLUGIN_PATH', '')}:{plugins_dir}"

# Prevent GStreamer from forking gst-plugin-scanner (a C subprocess that cannot
# resolve Python symbols). Scanning in-process lets libgstpython.so find the
# Python runtime that is already loaded.
os.environ.setdefault("GST_REGISTRY_FORK", "no")

Gst.init(None)

reg = Gst.Registry.get()
if not reg.find_plugin("python"):
    raise RuntimeError(
        "GStreamer 'python' plugin not found. "
        "Ensure GST_PLUGIN_PATH includes the path to libgstpython.so. "
        "If error persists: rm ~/.cache/gstreamer-1.0/registry.x86_64.bin"
    )
```

### Buffer Mutability

When a custom element adds new metadata, use `buffer.copy()` which does a **shallow copy**
with an immutable read-only data pointer — no change to underlying buffer data.

Use `buffer.copy_deep()` only when you need to modify actual buffer data or its timestamp.
Allocating new buffer data is resource-consuming and may affect performance.

**Read for reference:** `samples/gstreamer/python/smart_nvr/plugins/python/gvaAnalytics.py`,
`samples/gstreamer/python/vlm_self_checkout/plugins/python/gvaFrameSelection.py`

---

## Pattern 8: Custom Python GStreamer Element (Bin / Sink)

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

## Pattern 9: Dynamic Pipeline Control (Tee + Valve)

Use `tee` to split stream into branches and `valve` to conditionally block/allow
flow on a branch based on inference results from another branch.
See [Tee + Valve with Async Sink](./pipeline-construction.md#example-tee--valve-with-async-sink-conditional-recording)
in the Pipeline Construction Reference for the GStreamer pipeline syntax.

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

---

## Pattern 10: Cross-Branch Signal Bridge

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

## Pattern 11: Asset Resolution (Video + Model Download)

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

## Pattern 12: Model Resolution (Separate Export Script)

When an application uses models from Ultralytics, HuggingFace Transformers, PaddlePaddle,
or other frameworks with a long list of runtime dependencies, create a **separate `export_models.py`**
script that handles all model download and export. Users run it once before starting the pipeline application.

Model export dependencies may clash with pipeline runtime dependencies, which further
justifies splitting these two phases.
