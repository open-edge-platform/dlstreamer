# Design Patterns Reference

Design patterns for building Python and C++ sample applications.


## Python reference patterns
Patterns for custom Python elements also apply to command-line apps that reuse them.

### Python Selection Table

Map the user's description to one or more of these patterns:

| # | Pattern | When to Apply |
|---|---------|---------------|
| 1 | **Pipeline Core** | Always — every app needs source → decode → sink |
| 2 | **Pipeline Event Loop** | Always — every app needs an event loop to advance execution |
| 3 | **Multi-Stream / Multi-Camera** | User wants to process multiple camera streams in a single pipeline with shared model, cross-stream batching, or composite mosaic output |
| 4 | **Reading Analytics Metadata** | Always read when using Patterns 5–8 — defines the API for accessing GstAnalytics and DLStreamerMeta types from a buffer |
| 5 | **Pad Probe Callback** | User needs per-frame custom logic: metadata inspection, counting, throttling/dropping frames, logging — without the need to modify buffers or add new metadata |
| 6 | **AppSink Callback** | User wants to continue processing of frames or metadata in their own application |
| 7 | **Custom Python Element (BaseTransform)** | User needs a reusable per-frame element with GObject properties settable from the pipeline string, or complex analytics that warrant a self-contained, shareable component |
| 8 | **Custom Python Element (Bin/Sink)** | User needs to manage a secondary sub-pipeline or implement non-trivial handling of the output stream |
| 9 | **Dynamic Pipeline Control** | User wants conditional routing, branching (tee + valve), or multi-stream selective recording |
| 10 | **Cross-Branch Signal Bridge** | User has a tee with branches that must exchange state |

---

### Pattern 1: Pipeline Core

**Every app uses this.** Initialize GStreamer, construct a pipeline, then run the event loop.
See [Pipeline Construction Reference](./pipeline-construction.md) for element tables and examples.

#### Approach 1: `Gst.parse_launch` (preferred)

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

#### Approach 2: Programmatic element creation

Required when linking must happen dynamically (e.g., `decodebin3` pad-added).

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
# ... add and link remaining elements (queues, sinks, etc.) ...
```

**Read for reference:** `samples/gstreamer/python/hello_dlstreamer/hello_dlstreamer_full.py`

---

### Pattern 2: Pipeline Event Loop

Every app needs a pipeline event loop for EOS and ERROR messages.
The `run_pipeline()` function below is the **canonical implementation** — optional
blocks are marked `[Optional]`.

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

    # NULL → PAUSED: wait for caps negotiation and preroll to complete
    pipeline.set_state(Gst.State.PAUSED)
    ret = pipeline.get_state(Gst.CLOCK_TIME_NONE)
    if ret[0] == Gst.StateChangeReturn.FAILURE:
        pipeline.set_state(Gst.State.NULL)
        raise RuntimeError("Pipeline failed to reach PAUSED")

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

#### Usage examples

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

#### Key rules for CommandReader

- **Never** mutate pipeline **state or topology** from the reader thread
  (e.g. `set_state()`, `send_event()`, element linking) — use `GLib.idle_add()`.
- Simple **element property changes** like `valve.set_property("drop", ...)`
  are GObject-lock-protected and safe from any thread.
- Return `GLib.SOURCE_REMOVE` from `idle_add` callbacks.
- Use a `daemon=True` thread.

**Read for reference:** `samples/gstreamer/python/hello_dlstreamer/hello_dlstreamer.py`

---

### Pattern 3: Multi-Stream / Multi-Camera

Run multiple camera streams in a **single GStreamer pipeline** with shared model
instances and cross-stream batching.

#### Model Sharing and Cross-Stream Batching

- Set `model-instance-id=<name>` to share model instances across streams.
- Set `batch-size=<stream_count>` for cross-stream batching.
- For highest throughput, use default `scheduling-policy=throughput`
- For minimal latency, set `scheduling-policy=latency`

See the Pipeline Construction Reference for GStreamer pipeline syntax:
- [Multi-Stream Analytics](./pipeline-construction.md#example-multi-stream-analytics-n-streams) — N parallel streams with shared model
- [Multi-Stream Compositor](./pipeline-construction.md#example-multi-stream-compositor-n-streams--22-grid-gpu-memory-path) — N streams merged into a 2×2 mosaic via `vacompositor`

#### Python: Building Multi-Stream Pipelines Programmatically

Construct the pipeline string in a loop using `Gst.parse_launch`.

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

> **Subprocess orchestration:** Only when streams need independent processes
> (different models, fault isolation). See `samples/gstreamer/python/onvif_cameras_discovery/dls_onvif_sample.py`.

#### Mosaic Output (vacompositor)

Merge all streams into a single composite view using `vacompositor`. Requires
`scheduling-policy=latency` to keep streams in lockstep.
See [Multi-Stream Compositor](./pipeline-construction.md#example-multi-stream-compositor-n-streams--22-grid-gpu-memory-path)
for the full pipeline template.

---

### Pattern 4: Reading Analytics Metadata

All per-frame patterns (Pad Probe · AppSink · BaseTransform) use the same API to read analytics
metadata from a buffer. The key difference is what they can do with it after reading:

| Pattern | Read metadata | Modify pixel data | Add new GstMeta |
|---------|:-------------:|:-----------------:|:---------------:|
| 5 — Pad Probe | ✓ | ✗ | ✗ |
| 6 — AppSink | ✓ | ✓ | ✗ |
| 7 — BaseTransform | ✓ | ✓ | ✓ |

**Get the analytics relation metadata handle:**
```python
rmeta = GstAnalytics.buffer_get_analytics_relation_meta(buffer)
if not rmeta:
    return  # no analytics metadata on this buffer
```

**Standard GstAnalytics types — generic `for mtd in rmeta` with `isinstance()`:**
```python
for mtd in rmeta:
    if isinstance(mtd, GstAnalytics.ODMtd):
        label = GLib.quark_to_string(mtd.get_obj_type())
        success, x, y, w, h, rotation = mtd.get_location()
        confidence = mtd.get_confidence_lvl()
    elif isinstance(mtd, GstAnalytics.TrackingMtd):
        success, tracking_id, _, _, _ = mtd.get_info()
    elif isinstance(mtd, GstAnalytics.ClsMtd):
        quark = mtd.get_quark(0)
        level = mtd.get_level(0)
```

**DLStreamerMeta types (TripwireMtd, ZoneMtd) — required module-level registration:**

Add the block below at module level (before `Gst.init([])`) in any plugin or probe that
reads metadata from a pipeline containing `gvaanalytics`.

```python
gi.require_version("DLStreamerMeta", "1.0")
from gi.repository import DLStreamerMeta
from gstgva import meta_registry

# ... inside do_transform_ip / pad probe / appsink callback:
for mtd in rmeta:
    if isinstance(mtd, DLStreamerMeta.ZoneMtd):
        success, zone_id = mtd.get_info()                     # (bool, str)
    elif isinstance(mtd, DLStreamerMeta.TripwireMtd):
        success, tripwire_id, direction = mtd.get_info()      # (bool, str, int)

# Alternatively, use iter_on_type for type-filtered iteration:
for mtd in rmeta.iter_on_type(DLStreamerMeta.ZoneMtd):
    success, zone_id = mtd.get_info()
for mtd in rmeta.iter_on_type(DLStreamerMeta.TripwireMtd):
    success, tripwire_id, direction = mtd.get_info()
```

**Adding new analytics metadata:**
```python
rmeta.add_od_mtd(GLib.quark_from_string("label"), x, y, w, h, confidence)
```

**Adding watermark text:**
```python
gi.require_version("DLStreamerWatermarkMeta", "1.0")
from gi.repository import DLStreamerWatermarkMeta

DLStreamerWatermarkMeta.text_meta_add(buffer, x=10, y=30, text="...",
    font_scale=0.8, font_type=0, r=0, g=255, b=255, thickness=1, draw_bg=True)
```

---

### Pattern 5: Pad Probe Callback

Attach a probe to inspect metadata per frame: count objects, log events, or selectively drop frames.
Pad probes are **read-only** — they cannot modify pixel data or add new GstMeta.
See [Reading Analytics Metadata](#reading-analytics-metadata) for the iteration API.
For logic that must write metadata, use Pattern 7 (BaseTransform) instead.

**Return values:**
- `Gst.PadProbeReturn.OK` — pass the frame downstream
- `Gst.PadProbeReturn.DROP` — drop the frame (do not forward downstream)

```python
def my_probe(pad, info, user_data):
    buffer = info.get_buffer()
    rmeta = GstAnalytics.buffer_get_analytics_relation_meta(buffer)
    if not rmeta:
        return Gst.PadProbeReturn.OK
    for mtd in rmeta:
        if isinstance(mtd, GstAnalytics.ODMtd):
            label = GLib.quark_to_string(mtd.get_obj_type())
            # ... inspect, count, log ...
    return Gst.PadProbeReturn.OK

pipeline.get_by_name("my_element").get_static_pad("sink").add_probe(
    Gst.PadProbeType.BUFFER, my_probe, None)
```

**Read for reference:** `samples/gstreamer/python/hello_dlstreamer/hello_dlstreamer.py`

---

### Pattern 6: AppSink Callback

Pull frames into Python via `appsink` for processing outside the pipeline.
Unlike a pad probe, the appsink callback holds a sample reference and can modify the buffer.
See [Reading Analytics Metadata](#reading-analytics-metadata) for the metadata iteration API.

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

**Read for reference:** `samples/gstreamer/python/prompted_detection/prompted_detection.py`

---

### Pattern 7: Custom Python GStreamer Element (BaseTransform)

Subclass `GstBase.BaseTransform` for per-frame analytics that reads metadata, modifies pixel data,
or adds new GstMeta to the buffer. Use when a pad probe (Pattern 5) is insufficient — e.g. when
writing watermark text. See [Reading Analytics Metadata](#reading-analytics-metadata) for the
metadata iteration API.

#### Conventions

- **File:** `plugins/python/<element_name>.py`
- **Class:** PascalCase (e.g., `FrameSelection`)
- **Factory name:** lowercase with `_py` suffix (e.g., `gvaframeselection_py`)
- End file with: `GObject.type_register(Cls)` and `__gstelementfactory__ = (...)`
- Call `Gst.init_python()` after imports
- Properties: `@GObject.Property` decorator
- Do not drop frames before PLAYING state (sinks need preroll)

#### Template: Metadata-Only Element

```python
import gi
gi.require_version("GstBase", "1.0")
gi.require_version("GstAnalytics", "1.0")
gi.require_version("DLStreamerMeta", "1.0")
from gi.repository import Gst, GstBase, GObject, GLib, GstAnalytics, DLStreamerMeta
Gst.init_python()

GST_BASE_TRANSFORM_FLOW_DROPPED = Gst.FlowReturn.CUSTOM_SUCCESS

class MyAnalytics(GstBase.BaseTransform):
    __gstmetadata__ = ("My Analytics", "Transform", "Description", "Author")
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
        _, state, _ = self.get_state(0)
        if state != Gst.State.PLAYING:
            return Gst.FlowReturn.OK

        rmeta = GstAnalytics.buffer_get_analytics_relation_meta(buffer)
        if not rmeta:
            return Gst.FlowReturn.OK

        # Standard types — generic iteration (see Reading Analytics Metadata)
        for mtd in rmeta:
            if isinstance(mtd, GstAnalytics.ODMtd):
                pass  # ... custom analytics logic ...

        # DLStreamerMeta types — use iter_on_type (see Reading Analytics Metadata)
        for mtd in rmeta.iter_on_type(DLStreamerMeta.TripwireMtd):
            success, tripwire_id, direction = mtd.get_info()

        # Can also add new GstMeta — buffer is writable in do_transform_ip
        # DLStreamerWatermarkMeta.text_meta_add(buffer, ...)

        return Gst.FlowReturn.OK  # or GST_BASE_TRANSFORM_FLOW_DROPPED to silently discard

GObject.type_register(MyAnalytics)
__gstelementfactory__ = ("myanalytics_py", Gst.Rank.NONE, MyAnalytics)
```

#### Plugin Registration

Add before `Gst.init(None)`. Only needed when the app uses custom Python elements:

```python
plugins_dir = str(Path(__file__).resolve().parent / "plugins")
if plugins_dir not in os.environ.get("GST_PLUGIN_PATH", ""):
    os.environ["GST_PLUGIN_PATH"] = f"{os.environ.get('GST_PLUGIN_PATH', '')}:{plugins_dir}"
os.environ.setdefault("GST_REGISTRY_FORK", "no")  # required for Python elements

Gst.init(None)
reg = Gst.Registry.get()
if not reg.find_plugin("python"):
    raise RuntimeError("GStreamer 'python' plugin not found. "
                       "Install gst-python / python3-gst-1.0 and clear "
                       "~/.cache/gstreamer-1.0/registry.*.bin if needed.")
```

> Do not set `GST_REGISTRY_FORK=no` in apps without custom Python elements —
> it produces noisy `undefined symbol` warnings on stderr.
>
> If custom elements are not found after registration, clear only the GStreamer
> registry cache files in `~/.cache/gstreamer-1.0/` (for example
> `registry.x86_64.bin`) and then restart the application.

#### Elements That Modify Pixel Data

- `buffer.map(READ | WRITE)` in `do_transform_ip` returns `success=False` (read-only pools)
- Override `do_prepare_output_buffer` instead: read-only map input, create new buffer
- Avoid `buffer.copy_deep()` for pixel data writes in this scenario;
  allocate a new output buffer in `do_prepare_output_buffer` instead

```python
def do_prepare_output_buffer(self, inbuf):
    success, map_info = inbuf.map(Gst.MapFlags.READ)
    if not success:
        return Gst.FlowReturn.OK, inbuf
    try:
        frame = np.ndarray((self._h, self._w, 3), dtype=np.uint8, buffer=map_info.data)
        result = self._process(frame)
    finally:
        inbuf.unmap(map_info)

    outbuf = Gst.Buffer.new_wrapped(result.tobytes())
    outbuf.pts = inbuf.pts
    outbuf.dts = inbuf.dts
    outbuf.duration = inbuf.duration

    in_rmeta = GstAnalytics.buffer_get_analytics_relation_meta(inbuf)
    if in_rmeta:
        out_rmeta = GstAnalytics.buffer_add_analytics_relation_meta(outbuf)
        # Re-add required metadata entries to out_rmeta

    return Gst.FlowReturn.OK, outbuf

def do_transform_ip(self, buffer):
    return Gst.FlowReturn.OK  # no-op
```

#### Caps Negotiation for Resolution-Changing Elements

Override when output dimensions differ from input. Not needed for same-resolution transforms.

```python
def do_transform_caps(self, direction, caps, filter_caps):
    if direction == Gst.PadDirection.SINK:
        out = Gst.Caps.from_string(
            f"video/x-raw,format=RGB,width={self._out_width},height={self._out_height}")
    else:
        out = Gst.Caps.new_any()
    return out.intersect(filter_caps) if filter_caps else out

def do_fixate_caps(self, direction, caps, othercaps):
    if direction == Gst.PadDirection.SINK:
        return Gst.Caps.from_string(
            f"video/x-raw,format=RGB,width={self._out_width},height={self._out_height}").fixate()
    return othercaps.fixate()
```

**Read for reference:** `samples/gstreamer/python/smart_nvr/plugins/python/gvaAnalytics.py`,
`samples/gstreamer/python/vlm_self_checkout/plugins/python/gvaFrameSelection.py`

---

### Pattern 8: Custom Python GStreamer Element (Bin / Sink)

Subclass `Gst.Bin` to encapsulate an internal sub-pipeline (e.g., encoder + muxer + sink).
Expose a ghost pad.

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

> **Decision shortcut — recording / conditional output:** For *event-triggered recording*
> or *conditional saving* on a **single stream**, use this pattern with an internal
> `appsrc → encoder → mux → filesink` sub-pipeline.
> For **multi-stream selective recording**, use
> [Pattern 9](#pattern-9-dynamic-pipeline-control-tee--valve) (inline valve
> per stream — zero-copy, no sub-pipelines).

---

### Pattern 9: Dynamic Pipeline Control (Tee + Valve)

Use `tee` + `valve` to conditionally block/allow flow on a branch.
Toggle recording by setting `valve.set_property("drop", True/False)`.
See [Tee + Valve](./pipeline-construction.md#example-tee--valve-conditional-recording)
for single-stream pipeline syntax.

**Key rules:**
- Valves start with `drop=false` so downstream sinks negotiate caps and complete preroll
- `async=false` on the terminal sink in valve-gated branches — prevents preroll deadlock
- `fragment-duration=1000` on `mp4mux` — ensures playable output without EOS
- `valve.set_property("drop", ...)` is thread-safe

**Read for reference:** `samples/gstreamer/python/open_close_valve/open_close_valve_sample.py`

For multi-stream pipelines, add a per-stream `valve → encoder → mux → filesink`
recording branch. This builds on [Pattern 9](#pattern-9-dynamic-pipeline-control-tee--valve).
See [Multi-Stream Selective Recording](./pipeline-construction.md#example-multi-stream-selective-recording-per-stream-tee--valve)
for the pipeline topology.


```python
class RecordingController:
    def __init__(self, pipeline, num_streams):
        self._valves = [pipeline.get_by_name(f"rec_valve_{i}") for i in range(num_streams)]
        self._active = -1

    def close_all_valves(self):
        for v in self._valves:
            v.set_property("drop", True)

    def start(self, idx):
        self.stop()
        self._active = idx
        self._valves[idx].set_property("drop", False)

    def stop(self):
        if self._active >= 0:
            self._valves[self._active].set_property("drop", True)
            self._active = -1

# Connect to bus before setting PLAYING:
def _on_state_changed(bus, msg):
    if msg.src == pipeline:
        _, new, pending = msg.parse_state_changed()
        if new == Gst.State.PLAYING and pending == Gst.State.VOID_PENDING:
            controller.close_all_valves()
```

For per-session timestamped filenames, use `splitmuxsink` instead of `filesink`.

---

### Pattern 10: Cross-Branch Signal Bridge

Use a GObject signal bridge when `tee` branches must exchange state.

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

## C and C++ reference patterns
The following patterns cover GStreamer C/C++ application development basics applicable to both standalone apps and custom element plugins.

### Pattern 11: c/cpp: Gstreamer initialization
https://gstreamer.freedesktop.org/documentation/application-development/basics/init.html?gi-language=c

### Pattern 12: c/cpp: The GOption interface
https://gstreamer.freedesktop.org/documentation/application-development/basics/init.html?gi-language=c

### Pattern 13: c/cpp: Creating a GstElement
https://gstreamer.freedesktop.org/documentation/application-development/basics/elements.html?gi-language=c

### Pattern 14: c/cpp: Using an element as a GObject
https://gstreamer.freedesktop.org/documentation/application-development/basics/elements.html?gi-language=c

### Pattern 15: c/cpp: Getting information about an element using a factory
https://gstreamer.freedesktop.org/documentation/application-development/basics/elements.html?gi-language=c

### Pattern 16: c/cpp: Creating a bin
https://gstreamer.freedesktop.org/documentation/application-development/basics/bins.html?gi-language=c

### Pattern 17: c/cpp: Custom bin
https://gstreamer.freedesktop.org/documentation/application-development/basics/bins.html?gi-language=c

### Pattern 18: c/cpp: How to use a bus
https://gstreamer.freedesktop.org/documentation/application-development/basics/bus.html?gi-language=c

### Pattern 19: c/cpp: Pads and capabilities
https://gstreamer.freedesktop.org/documentation/application-development/basics/pads.html?gi-language=c

### Pattern 20: c/cpp: Buffers and events
https://gstreamer.freedesktop.org/documentation/application-development/basics/data.html?gi-language=c

### Pattern 21: c/cpp: Sample application
https://gstreamer.freedesktop.org/documentation/application-development/basics/helloworld.html?gi-language=c