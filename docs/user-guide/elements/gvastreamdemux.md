# gvastreamdemux

Demuxes a single interleaved video stream back into multiple per-source output pads based on
`GstGvaStreammuxMeta` metadata. This element is the companion to `gvastreammux` and **must** be
used together with it — it cannot function standalone.

## Overview

The `gvastreamdemux` element reads `GstGvaStreammuxMeta` from each incoming buffer and routes it
to the corresponding source pad (`src_0`, `src_1`, ...) based on the `source_id` field. This enables
a common multi-stream pattern:

```
N sources → gvastreammux → shared inference → gvastreamdemux → N independent outputs
```

Key design principles:

- **Metadata-Driven Routing**: Each buffer is routed to `src_{source_id}` using the metadata attached
  by `gvastreammux`. Buffers without `GstGvaStreammuxMeta` cause a pipeline error.
- **No Frame Dropping**: Every buffer received on the sink pad is forwarded to exactly one source pad.
- **Batch Ordering Validation**: The element tracks `batch_id` per source pad to detect out-of-order
  delivery. If a buffer arrives with a `batch_id` lower than the previous one for the same source,
  a warning is logged.
- **Source Count Validation**: On the first buffer, the element checks that the number of requested
  source pads matches `num_sources` in the metadata. A mismatch causes a pipeline error.

> **Important**: Because `gvastreammux` interleaves frames in round-robin order, the downstream
> inference element (e.g., `gvadetect`) **must** have `inference-interval=1`. Setting
> `inference-interval` to N > 1 causes certain sources to be consistently skipped from inference.
> A future enhancement will add frame-dropping logic inside `gvastreammux` to apply the interval
> uniformly across all input streams.

## How It Works

1. The pipeline requests source pads (`src_0`, `src_1`, ...) — one per original input source.
   The number of `src` pads **must** match the number of `sink` pads on the upstream `gvastreammux`.
2. When a buffer arrives on the sink pad, the element reads `GstGvaStreammuxMeta`.
3. The buffer is pushed to `src_{source_id}`.
4. On the first buffer, the element validates `num_sources == num_src_pads`.
5. EOS on the sink pad is forwarded to all source pads.

## Properties

| Property  | Type   | Description                                                                                   | Default |
|-----------|--------|-----------------------------------------------------------------------------------------------|---------|
| max-fps   | Double | Maximum output frame rate per source (0 = unlimited). Only set for local file sources. Do not set for RTSP or live sources as it may cause pipeline stalls. | 0       |

The `max-fps` throttle is applied globally (shared across all source pads), not per individual pad.

## Pipeline Examples

### Local Files with Per-Source Output

Mux two local files for shared inference, then demux for independent downstream processing.
Set `max-fps` on both mux and demux to control throughput:

```bash
gst-launch-1.0 \
  gvastreammux name=mux max-fps=30 \
  ! queue \
  ! gvadetect model=model.xml device=GPU \
    pre-process-backend=va-surface-sharing \
  ! gvastreamdemux name=demux max-fps=30 \
  demux.src_0 ! queue ! gvafpscounter ! fakesink \
  demux.src_1 ! queue ! gvafpscounter ! fakesink \
  filesrc location=video0.mp4 ! decodebin ! videoconvert ! mux.sink_0 \
  filesrc location=video1.mp4 ! decodebin ! videoconvert ! mux.sink_1
```

### RTSP Sources with Per-Source Output

Two RTSP streams muxed for shared inference, then demuxed. No `max-fps` needed for live sources:

```bash
gst-launch-1.0 \
  gvastreammux name=mux \
  ! queue \
  ! gvadetect model=model.xml device=GPU \
    pre-process-backend=va-surface-sharing \
  ! gvastreamdemux name=demux \
  demux.src_0 ! queue ! gvafpscounter ! fakesink \
  demux.src_1 ! queue ! gvafpscounter ! fakesink \
  rtspsrc location=rtsp://host:8554/stream latency=200 \
  ! rtph265depay ! h265parse ! vah265dec ! mux.sink_0 \
  rtspsrc location=rtsp://host:8555/stream latency=200 \
  ! rtph265depay ! h265parse ! vah265dec ! mux.sink_1
```

### Per-Source Watermark and Display

Demux after inference, then apply per-source watermark and render to screen:

```bash
gst-launch-1.0 \
  gvastreammux name=mux \
  ! queue \
  ! gvadetect model=model.xml device=GPU \
    pre-process-backend=va-surface-sharing \
  ! gvastreamdemux name=demux \
  demux.src_0 ! queue ! gvawatermark ! videoconvert ! autovideosink \
  demux.src_1 ! queue ! gvawatermark ! videoconvert ! autovideosink \
  rtspsrc location=rtsp://host:8554/stream latency=200 \
  ! rtph265depay ! h265parse ! vah265dec ! mux.sink_0 \
  rtspsrc location=rtsp://host:8555/stream latency=200 \
  ! rtph265depay ! h265parse ! vah265dec ! mux.sink_1
```

## Required Metadata

This element requires `GstGvaStreammuxMeta` on every incoming buffer. The metadata is attached
by `gvastreammux` and contains:

| Field        | Type     | Description                                          |
|--------------|----------|------------------------------------------------------|
| source_id    | guint    | Pad index the buffer originated from (0, 1, 2, ...) |
| batch_id     | guint64  | Monotonically increasing batch cycle counter         |
| num_sources  | guint    | Total number of active input sources at batch time   |

## Error Conditions

| Condition                             | Behavior                                |
|---------------------------------------|-----------------------------------------|
| Buffer missing GstGvaStreammuxMeta    | Returns `GST_FLOW_ERROR`                |
| `num_src_pads != num_sources`         | Returns `GST_FLOW_ERROR` (first buffer) |
| `source_id` out of range              | Returns `GST_FLOW_ERROR`                |
| `batch_id` out of order for a pad     | `GST_WARNING` (continues processing)    |

## Element Details (gst-inspect-1.0)

```
Factory Details:
  Rank                     none (0)
  Long-name                GVA Stream Demuxer
  Klass                    Video/Demuxer
  Description              Demuxes a single stream into multiple output pads
                           based on GstGvaStreammuxMeta source_id.
                           Must be used with gvastreammux.
  Author                   Intel Corporation

Pad Templates:
  SINK template: 'sink'
    Availability: Always
    Capabilities:
      video/x-raw
               format: { BGRx, BGRA, BGR, NV12, I420, RGB, RGBA, RGBx }
      video/x-raw(memory:VAMemory)
               format: { NV12 }
      video/x-raw(memory:DMABuf)
               format: { DMA_DRM }

  SRC template: 'src_%u'
    Availability: On request
    Capabilities:
      video/x-raw
               format: { BGRx, BGRA, BGR, NV12, I420, RGB, RGBA, RGBx }
      video/x-raw(memory:VAMemory)
               format: { NV12 }
      video/x-raw(memory:DMABuf)
               format: { DMA_DRM }

Element Properties:
  max-fps             : Maximum output frame rate per source (0 = unlimited).
                        Only set this when the video source is a local file.
                        Do not set for RTSP or live sources as it may cause pipeline stalls.
                        flags: readable, writable
                        Double. Range: 0 - 1.797693e+308  Default: 0
  name                : The name of the object
                        flags: readable, writable
                        String. Default: "gvastreamdemux0"
  parent              : The parent of the object
                        flags: readable, writable
                        Object of type "GstObject"
```
