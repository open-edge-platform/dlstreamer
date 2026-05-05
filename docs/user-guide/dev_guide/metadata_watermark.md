# Watermark Metadata

`gvawatermark` automatically renders custom drawing primitives attached directly to GStreamer buffers
using the DLStreamer watermark metadata types. This is useful for drawing custom shapes, lines, and text from
a Python callback or a custom GStreamer element without modifying pixel data manually.

## Metadata Types

| Type                  | Python name                          | Description |
|-----------------------|--------------------------------------|-------------|
| `WatermarkDrawMeta`   | `DLStreamerWatermarkMeta.DrawMeta`   | Polygon or polyline defined by an ordered list of (x, y) coordinate pairs (max 128 pairs) |
| `WatermarkCircleMeta` | `DLStreamerWatermarkMeta.CircleMeta` | Circle defined by center (cx, cy), radius, color, and thickness |
| `WatermarkTextMeta`   | `DLStreamerWatermarkMeta.TextMeta`   | Text label at position (x, y) with font, scale, color, and optional background |

A thickness of `-1` fills the shape (circle or polygon). For `WatermarkDrawMeta`, two points draw a line segment;
three or more points draw a polygon.

## Python Usage

Use the `DLStreamerWatermarkMeta` GObject Introspection (GIR) bindings to attach metadata from Python:

```python
import gi
gi.require_version("DLStreamerWatermarkMeta", "1.0")
from gi.repository import DLStreamerWatermarkMeta

# Polygon (6 points) in green
DLStreamerWatermarkMeta.draw_meta_add(
    buffer,
    [100, 50, 200, 50, 250, 150, 200, 250, 100, 250, 50, 150],
    r=0, g=200, b=0, thickness=3)

# Line (2 points) in red
DLStreamerWatermarkMeta.draw_meta_add(
    buffer, [300, 80, 500, 200], r=220, g=20, b=20, thickness=4)

# Filled circle in blue  (thickness=-1 → filled)
DLStreamerWatermarkMeta.circle_meta_add(
    buffer, cx=570, cy=150, radius=50, r=30, g=80, b=220, thickness=-1)

# Text with background
DLStreamerWatermarkMeta.text_meta_add(
    buffer, x=50, y=300, text="Hello DLStreamer",
    font_scale=0.8, font_type=4, r=220, g=200, b=0, thickness=2, draw_bg=True)
```

Once attached, `gvawatermark` renders the primitives automatically alongside any standard inference metadata.

See the [watermark_meta Python sample](../../../samples/gstreamer/python/watermark_meta/README.md) for a complete
working example.
