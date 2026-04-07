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
Use Custom Python elements instead of Probes if custom logic is complex and/or when it modifies buffers or metadata. 

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

**Read for reference:** `samples/gstreamer/python/vlm_alerts/vlm_alerts.py`

---

## Pattern 10: Asset Resolution (Video + Model Download)

Add Python functions to download assets (such as input video files) and AI models.
Always cache downloaded files locally, so only first application run requires network connection.
For AI model download, prioritize using existing download scripts and generate inline only if simple. 

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

## Pattern 13: Separate Model Download Script

When an application uses models from Ultralytics, HuggingFace Transformers, PaddlePaddle,
or other frameworks with long list of run-time dependencies, create a **separate `download_models.py`**
script that handles all model download and export. Users run it once before starting the pipeline application.

In addition, model export dependencies may clash with model inference dependencies which further
justifies splitting these two phases.

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
| Detection + OCR (license plates, text) | 1 + 4 + 10 + 11 + 13 |
| Detection + custom model (non-OCR) | 1 + 4 + 6 + 11 |

---
