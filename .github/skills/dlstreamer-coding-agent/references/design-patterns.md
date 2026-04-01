# Design Patterns Reference

Patterns extracted from existing DLStreamer Python sample apps. Each pattern includes
the canonical source file to read for the latest API usage.

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
runtime decisions.

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

## Pattern 4: AI Inference Chain (Detect → Classify)

Chain `gvadetect` and `gvaclassify` to first detect objects, then classify attributes
of each detected region.

```python
pipeline_str = (
    f"filesrc location={video} ! decodebin3 ! "
    f"gvadetect model={detect_model} device=GPU batch-size=4 ! queue ! "
    f"gvaclassify model={classify_model} device=GPU batch-size=4 ! queue ! "
    f"gvafpscounter ! gvawatermark ! "
    f"videoconvert ! vah264enc ! h264parse ! mp4mux ! filesink location={output}"
)
```

**Read for reference:** `samples/gstreamer/python/face_detection_and_classification/face_detection_and_classification.py`

---

## Pattern 5: Dynamic Pipeline Control (Tee + Valve)

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

---

## Pattern 6: Custom Python GStreamer Element (BaseTransform)

Create a custom in-pipeline analytics element by subclassing `GstBase.BaseTransform`.
The element processes each buffer in `do_transform_ip` and can read/write metadata.

```python
import gi
gi.require_version("GstBase", "1.0")
gi.require_version("GstAnalytics", "1.0")
from gi.repository import Gst, GstBase, GObject, GLib, GstAnalytics
Gst.init_python()

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
        if rmeta:
            for mtd in rmeta:
                if isinstance(mtd, GstAnalytics.ODMtd):
                    # ... custom analytics logic ...
                    pass
        return Gst.FlowReturn.OK

GObject.type_register(MyAnalytics)
__gstelementfactory__ = ("myanalytics_py", Gst.Rank.NONE, MyAnalytics)
```

**File location:** Place in `plugins/python/<element_name>.py`

**Registration:** Add the plugins directory to `GST_PLUGIN_PATH`:
```python
plugins_dir = str(Path(__file__).resolve().parent / "plugins")
os.environ["GST_PLUGIN_PATH"] = f"{os.environ.get('GST_PLUGIN_PATH', '')}:{plugins_dir}"
Gst.init(None)
```

**Read for reference:** `samples/gstreamer/python/smart_nvr/plugins/python/gvaAnalytics.py`

---

## Pattern 7: Custom Python GStreamer Element (Bin / Sink)

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

---

## Pattern 8: Cross-Branch Signal Bridge

When a `tee` splits a pipeline into branches that must exchange state (e.g., detection
results from branch A control overlay in branch B), use a GObject signal bridge.

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

## Pattern 9: VLM Inference (gvagenai)

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

> **Note:** No explicit caps filter between `decodebin3` and `gvagenai`. Let GStreamer
> auto-negotiate memory type and pixel format — `gvagenai` handles this natively.

**Read for reference:** `samples/gstreamer/python/vlm_alerts/vlm_alerts.py`

---

## Pattern 10: Asset Resolution (Video + Model Download)

Auto-download video files and models if not cached locally.

```python
from pathlib import Path
import urllib.request

VIDEOS_DIR = Path(__file__).resolve().parent / "videos"
MODELS_DIR = Path(__file__).resolve().parent / "models"

def download_video(url: str) -> Path:
    VIDEOS_DIR.mkdir(parents=True, exist_ok=True)
    filename = url.rstrip("/").split("/")[-1]
    local = VIDEOS_DIR / filename
    if not local.exists():
        req = urllib.request.Request(url, headers={"User-Agent": "Mozilla/5.0"})
        with urllib.request.urlopen(req, timeout=60) as resp:
            local.write_bytes(resp.read())
    return local.resolve()
```

**Read for reference:** `samples/gstreamer/python/vlm_self_checkout/vlm_self_checkout.py`

---

## Pattern 11: File Output (Video + JSON + Snapshots)

Combine output elements for multi-format results:

| Output type | Pipeline elements |
|-------------|-------------------|
| Annotated video | `gvawatermark ! videoconvert ! vah264enc ! h264parse ! mp4mux ! filesink` |
| JSON metadata | `gvametapublish file-format=json-lines file-path=results.jsonl` |
| JPEG snapshots | `jpegenc ! multifilesink location=snap-%d.jpeg` |
| Chunked video | `gvarecorder_py location=output.mp4 max-time=10` (custom element) |

---

## Pattern 12: Multi-Camera / RTSP

For RTSP sources, replace `filesrc ! decodebin3` with `rtspsrc`:

```python
# Single camera in pipeline string:
f'rtspsrc location={rtsp_url} ! decodebin3 ! ...'

# Multiple cameras via subprocess orchestration:
for camera in cameras:
    cmd = prepare_commandline(camera.rtsp_url, pipeline_elements)
    process = subprocess.Popen(cmd, shell=False, stdout=subprocess.PIPE, ...)
```

**Read for reference:** `samples/gstreamer/python/onvif_cameras_discovery/dls_onvif_sample.py`

---

## Composing Patterns

When building a new app, identify which patterns apply and compose them:

| User wants... | Patterns to combine |
|---------------|---------------------|
| Simple detection + display | 1 + 4 (detect only) |
| Detection + classification + save | 1 + 4 + 11 |
| VLM alerting on video file | 1 + 9 + 10 + 11 |
| Detection with conditional recording | 1 + 4 + 5 + 7 |
| Custom analytics + chunked storage | 1 + 4 + 6 + 7 |
| Detection + VLM on selected frames | 1 + 4 + 5 + 6 + 8 + 9 + 11 |
| Multi-camera with per-camera AI | 12 + (any above per camera) |
| Detection + custom model (e.g. OCR) | 1 + 4 + 13 + 14 + 11 |

---

## Pattern 13: Custom OpenVINO Inference in Python Element

> **Prefer `gvaclassify`** for second-stage models (classification, OCR) whenever the
> model's input/output can be described by a model-proc file. Use this custom-element
> pattern only as a **fallback** when the model requires pre/post-processing that
> `gvaclassify` cannot express (e.g. CTC decoding, custom crop logic, sequence output).

When you need a model that is **not natively supported** by `gvadetect` / `gvaclassify`
(e.g. OCR text recognition with CTC decoding, depth estimation, pose refinement), create
a custom `BaseTransform` element that uses OpenVINO Python API directly.

```python
import openvino as ov

class MyCustomInference(GstBase.BaseTransform):
    _model_path = ""
    _device = "CPU"

    @GObject.Property(type=str)
    def model_path(self):
        return self._model_path

    @model_path.setter
    def model_path(self, value):
        self._model_path = value

    def __init__(self):
        super().__init__()
        self._compiled_model = None

    def _load_model(self):
        if self._compiled_model is not None:
            return
        core = ov.Core()
        model = core.read_model(self._model_path)

        # CRITICAL: Handle dynamic shapes — many converted models have dynamic dims.
        # Set static shape before compiling for efficient inference.
        input_info = model.input(0)
        if input_info.partial_shape.is_dynamic:
            model.reshape({input_info.any_name: [1, 3, 48, 320]})  # set your shape

        self._compiled_model = core.compile_model(model, self._device)

    def do_transform_ip(self, buffer):
        self._load_model()
        # ... crop regions, preprocess, infer, post-process ...
        return Gst.FlowReturn.OK
```

**⚠ Common mistake:** Accessing `compiled_model.input(0).shape` on a dynamic-shape model
raises `RuntimeError: to_shape was called on a dynamic shape`. Always check
`input_info.partial_shape.is_dynamic` and use `model.reshape()` to set static dimensions
**before** calling `core.compile_model()`.

**When to use this instead of `gvadetect`/`gvaclassify`:**
- Models requiring CTC or other sequence decoding not supported by model-proc
- Models that need custom pre/post-processing not handled by DLStreamer model-proc
- Two-stage pipelines where the second model processes crops from the first **and**
  the crop/post-processing logic cannot be expressed through `gvaclassify` model-proc

> **Note:** For OCR models like PaddleOCR, first check if `gvaclassify` with a model-proc
> file can handle the task. Only use this custom element approach if `gvaclassify`
> cannot express the required pre/post-processing.

**Read for reference:** `samples/gstreamer/python/license_plate_recognition/plugins/python/gvaocr_py.py`

---

## Pattern 14: Pixel Access in Custom Element

When a custom element needs to **read actual pixel values** (not just metadata) — e.g. to
crop detected regions for a second model — the pipeline must deliver frames in a
CPU-accessible format.

> **This is an intentional exception** to the auto-negotiation rules. Standard DLStreamer
> elements (`gvadetect`, `gvaclassify`, `gvagenai`, `gvawatermark`) handle formats
> natively — only force a format when a custom element calls `buffer.map()`.

**Insert a format conversion before the custom element:**

```
gvadetect model=... device=GPU ! queue !
videoconvertscale ! video/x-raw,format=BGRx !
my_custom_element_py ! ...
```

**Map buffer pixels in `do_transform_ip`:**

```python
def _get_frame_bgr(self, buffer):
    caps = self.sinkpad.get_current_caps()
    s = caps.get_structure(0)
    width = s.get_value("width")
    height = s.get_value("height")
    fmt = s.get_string("format")

    success, map_info = buffer.map(Gst.MapFlags.READ)
    if not success:
        return None
    try:
        data = np.frombuffer(map_info.data, dtype=np.uint8)
        if fmt in ("BGRx", "BGRA"):
            frame = data.reshape(height, width, 4)[:, :, :3].copy()
        elif fmt == "NV12":
            yuv = data.reshape(height * 3 // 2, width)
            frame = cv2.cvtColor(yuv, cv2.COLOR_YUV2BGR_NV12)
        # ... handle other formats ...
    finally:
        buffer.unmap(map_info)
    return frame
```

**⚠ Common mistake:** Without `videoconvertscale ! video/x-raw,format=BGRx`, frames may
arrive in GPU memory (VA surface) or YUV formats that cannot be mapped to numpy arrays.
Always force a CPU-accessible format before custom elements that read pixels.

**⚠ Common mistake:** Forgetting `buffer.unmap(map_info)` causes memory leaks and pipeline
stalls. Always use `try/finally` to ensure unmap.

**When to use:** Any custom element that crops sub-regions, applies OpenCV transforms,
feeds pixels to an OpenVINO model, or saves images.

**Read for reference:** `samples/gstreamer/python/license_plate_recognition/plugins/python/gvaocr_py.py`
