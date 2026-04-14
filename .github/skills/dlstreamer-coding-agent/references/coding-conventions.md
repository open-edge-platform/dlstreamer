# Coding Conventions Reference

Conventions extracted from existing DLStreamer Python sample applications.

## File Header

Every Python file begins with the Intel copyright and MIT license:

```python
# ==============================================================================
# Copyright (C) 2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================
```

## Imports

GObject Introspection imports follow this exact pattern (order matters):

```python
import gi
gi.require_version("Gst", "1.0")
gi.require_version("GstAnalytics", "1.0")     # only if reading analytics metadata
gi.require_version("GstBase", "1.0")           # only in custom elements
gi.require_version("GstPbutils", "1.0")        # only if using Discoverer
from gi.repository import GLib, Gst, GstAnalytics  # pylint: disable=no-name-in-module, wrong-import-position
```

The `gi.require_version()` calls MUST appear before any `from gi.repository` import.

## GStreamer Initialization

Call `Gst.init(None)` exactly once, before creating any pipeline or element.

## Argument Parsing

**Simple apps (1–2 args):** Use `sys.argv` directly.

```python
if len(args) != 3:
    sys.stderr.write(f"usage: {args[0]} <VIDEO_FILE> <MODEL_FILE>\n")
    sys.exit(1)
```

**Complex apps (3+ args):** Use `argparse`.

```python
def parse_args():
    parser = argparse.ArgumentParser(description="DLStreamer Sample")
    parser.add_argument("--video-path", help="Path to local video")
    parser.add_argument("--video-url", help="URL to download video")
    parser.add_argument("--device", default="GPU")
    return parser.parse_args()
```

## Custom Python Element Conventions

- File goes in `plugins/python/<element_name>.py`
- Class name: PascalCase (e.g., `FrameSelection`)
- Element factory name: lowercase with `_py` suffix (e.g., `gvaframeselection_py`)
- Must end with: `GObject.type_register(ClassName)` and `__gstelementfactory__ = (...)`
- Must call `Gst.init_python()` after imports
- Properties use `@GObject.Property` decorator
- Transform elements subclass `GstBase.BaseTransform` and implement `do_transform_ip`
- Bin/Sink elements subclass `Gst.Bin` and use `Gst.GhostPad`

## Plugin Registration

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

## Error Handling

- Pipeline parse errors: catch `GLib.Error`
- Model export failures: check subprocess return codes
- Missing files: validate paths before pipeline construction
- Pipeline runtime errors: handle in the event loop via `Gst.MessageType.ERROR`

## Metadata Access (GstAnalytics API)

The standard pattern for reading detection metadata from a buffer:

```python
rmeta = GstAnalytics.buffer_get_analytics_relation_meta(buffer)
if rmeta:
    for mtd in rmeta:
        if isinstance(mtd, GstAnalytics.ODMtd):
            label = GLib.quark_to_string(mtd.get_obj_type())
            _, x, y, w, h, confidence = mtd.get_location()
            _, conf = mtd.get_confidence_lvl()
```

Writing overlay metadata:

```python
rmeta.add_od_mtd(GLib.quark_from_string("label text"), x, y, w, h, confidence)
```

Reading classification metadata (from gvagenai):

```python
for mtd in rmeta:
    if isinstance(mtd, GstAnalytics.ClsMtd):
        quark = mtd.get_quark(0)
        level = mtd.get_level(0)
```

Reading tracking metadata:

```python
for mtd in rmeta:
    if isinstance(mtd, GstAnalytics.TrackingMtd):
        success, tracking_id, _, _, _ = mtd.get_info()
```

## Buffer Mutability in Custom Elements or Pads

When a custom element adds new metadata, use `buffer.copy()` which does a **shallow copy**
with an immutable read-only data pointer, no change to underlying buffer data.

Use `buffer.copy_deep()` only when you need to modify acutal buffer data or its timestamp.
Allocating a new buffer data is a time- and resource-consuming operation and may affect performance.

## Device Availability Check

Check for GPU/NPU availability before constructing the pipeline. Use the fallback
chain NPU → GPU → CPU so the app works on any Intel system:

```python
def check_device(requested, label):
    """Check device availability with fallback chain: NPU → GPU → CPU."""
    if requested == "NPU" and not os.path.exists("/dev/accel/accel0"):
        print(f"Warning: NPU not available for {label}, falling back to GPU")
        requested = "GPU"
    if requested == "GPU" and not os.path.exists("/dev/dri/renderD128"):
        print(f"Warning: GPU not available for {label}, falling back to CPU")
        requested = "CPU"
    return requested
```
