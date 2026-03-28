# gvastreammux

Muxes multiple video streams into a single pipeline, attaching source-tracking metadata to each buffer.
The element enables multi-stream inference by interleaving frames from N input sources through a single
downstream processing chain (e.g., `gvadetect`), eliminating the need for N separate inference pipelines.

## Overview

The `gvastreammux` element collects video frames from multiple sink pads and outputs them through a single
source pad in **round-robin** order. Key design principles:

- **No Frame Dropping**: Every input frame is forwarded downstream. The element uses GStreamer's `GstCollectPads`
  mechanism which blocks until all active pads have at least one buffer available, then pops exactly one buffer
  from each pad per cycle.
- **One Frame Per Source Per Cycle**: Each batch cycle outputs exactly one frame from each input stream.
  The batch size always equals the number of input streams — it is not configurable separately.
- **Source Metadata**: Each output buffer is tagged with `GstGvaStreammuxMeta` containing `source_id` (pad index),
  `batch_id` (cycle counter), and `num_sources` (total input count). This metadata is required by
  `gvastreamdemux` to route buffers back to per-source branches.

## How It Works

1. The pipeline requests sink pads (`sink_0`, `sink_1`, ...) — one per input source.
2. `GstCollectPads` waits until every non-EOS sink pad has at least one buffer.
3. The `collected` callback pops one buffer from each pad, attaches `GstGvaStreammuxMeta`, and pushes
   it downstream through the single `src` pad.
4. The `batch_id` counter increments after each complete cycle.
5. When a source sends EOS, `GstCollectPads` stops waiting for that pad. The remaining sources continue
   to produce output until all sources have sent EOS.

## Properties

| Property  | Type   | Description                                                                                   | Default |
|-----------|--------|-----------------------------------------------------------------------------------------------|---------|
| max-fps   | Double | Maximum output frame rate (0 = unlimited). Only set for local file sources. Do not set for RTSP or live sources as it may cause pipeline stalls. | 0       |

## Pipeline Examples

### Local Files with max-fps (gvastreammux only)

Two local video files decoded and muxed for shared inference. Set `max-fps` to avoid processing
at unbounded speed:

```bash
gst-launch-1.0 \
  gvastreammux name=mux max-fps=30 \
  ! queue \
  ! gvadetect model=model.xml device=GPU \
    pre-process-backend=va-surface-sharing \
  ! queue ! gvafpscounter ! fakesink \
  filesrc location=video0.mp4 ! decodebin ! videoconvert ! mux.sink_0 \
  filesrc location=video1.mp4 ! decodebin ! videoconvert ! mux.sink_1
```

### RTSP Sources (gvastreammux only)

Two RTSP streams muxed for shared inference. No `max-fps` needed — live sources are naturally
rate-limited by the network:

```bash
gst-launch-1.0 \
  gvastreammux name=mux \
  ! queue \
  ! gvadetect model=model.xml device=GPU \
    pre-process-backend=va-surface-sharing \
  ! queue ! gvafpscounter ! fakesink \
  rtspsrc location=rtsp://host:8554/stream latency=200 \
  ! rtph265depay ! h265parse ! vah265dec ! mux.sink_0 \
  rtspsrc location=rtsp://host:8555/stream latency=200 \
  ! rtph265depay ! h265parse ! vah265dec ! mux.sink_1
```

### Local Files with gvastreamdemux (per-source output)

Mux two files for shared inference, then demux back to per-source branches:

```bash
gst-launch-1.0 \
  gvastreammux name=mux max-fps=30 \
  ! queue \
  ! gvadetect model=model.xml device=GPU \
    pre-process-backend=va-surface-sharing \
  ! gvastreamdemux name=demux \
  demux.src_0 ! queue ! gvafpscounter ! fakesink \
  demux.src_1 ! queue ! gvafpscounter ! fakesink \
  filesrc location=video0.mp4 ! decodebin ! videoconvert ! mux.sink_0 \
  filesrc location=video1.mp4 ! decodebin ! videoconvert ! mux.sink_1
```

### RTSP Sources with gvastreamdemux (per-source output)

Mux two RTSP streams, run shared inference, and demux for independent downstream processing:

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

## Metadata Structure

Each output buffer carries `GstGvaStreammuxMeta`:

| Field        | Type     | Description                                          |
|--------------|----------|------------------------------------------------------|
| source_id    | guint    | Pad index the buffer originated from (0, 1, 2, ...) |
| batch_id     | guint64  | Monotonically increasing batch cycle counter         |
| num_sources  | guint    | Total number of active input sources at batch time   |

## Element Details (gst-inspect-1.0)

```
Factory Details:
  Rank                     none (0)
  Long-name                GVA Stream Muxer
  Klass                    Video/Muxer
  Description              Muxes multiple video streams into a single pipeline with source metadata
  Author                   Intel Corporation

Pad Templates:
  SINK template: 'sink_%u'
    Availability: On request
    Capabilities:
      video/x-raw
               format: { BGRx, BGRA, BGR, NV12, I420, RGB, RGBA, RGBx }
      video/x-raw(memory:VAMemory)
               format: { NV12 }
      video/x-raw(memory:DMABuf)
               format: { DMA_DRM }

  SRC template: 'src'
    Availability: Always
    Capabilities:
      video/x-raw
               format: { BGRx, BGRA, BGR, NV12, I420, RGB, RGBA, RGBx }
      video/x-raw(memory:VAMemory)
               format: { NV12 }
      video/x-raw(memory:DMABuf)
               format: { DMA_DRM }

Element Properties:
  max-fps             : Maximum output frame rate (0 = unlimited).
                        Only set this when the video source is a local file.
                        Do not set for RTSP or live sources as it may cause pipeline stalls.
                        flags: readable, writable
                        Double. Range: 0 - 1.797693e+308  Default: 0
  name                : The name of the object
                        flags: readable, writable
                        String. Default: "gvastreammux0"
  parent              : The parent of the object
                        flags: readable, writable
                        Object of type "GstObject"
```
