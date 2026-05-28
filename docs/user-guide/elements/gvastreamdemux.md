# gvastreamdemux

Routes a single muxed stream back to per-source output pads based on `GstAnalyticsBatchMeta`
attached upstream by `gvastreammux`. The element is the companion to `gvastreammux` and
**must** be paired with it; it cannot work standalone.

## Overview

`gvastreamdemux` reads `GstAnalyticsBatchMeta.streams[0].index` from each incoming buffer and
forwards the buffer to `src_<index>`. This enables the common multi-stream pattern:

```
N sources → gvastreammux → shared inference → gvastreamdemux → N independent outputs
```

Key design points:

- **Metadata-driven routing** — every buffer is forwarded to exactly one source pad based on
  `streams[0].index`. Buffers without `GstAnalyticsBatchMeta` cause a pipeline error.
- **No frame dropping** — every input buffer is pushed to its target pad.
- **Sparse src indices** — names like `demux.src_5` work even if `src_0..src_4` were never
  created, mirroring the mux side. Indices must be in `[0, 256)`.
- **No strict count match** — the upstream `n_streams` and the number of requested `src_*` pads
  do not have to match. Buffers whose `streams[0].index` exceeds the highest requested src pad
  are dropped with a `GST_ERROR`. Match the indices to the mux indices to receive every frame.

> **Important**: as with `gvastreammux`, downstream `gvadetect` should keep
> `inference-interval=1` (default). Higher values would skip whole batches in arrival order,
> meaning some source ids consistently miss inference results.

## How It Works

1. The pipeline requests source pads (`src_0`, `src_1`, ...) — typically one per upstream
   sink pad. Pad index is parsed from the requested name; sparse indices are allowed up to 255.
2. When a buffer arrives, the element reads `GstAnalyticsBatchMeta` and uses
   `streams[0].index` as the destination source id.
3. The buffer is pushed to `src_<source_id>`. If no such pad exists, the buffer is dropped
   with `GST_FLOW_ERROR`.
4. EOS on the sink pad is forwarded to all source pads.

## Properties

| Property  | Type   | Default | Description |
|-----------|--------|---------|-------------|
| `max-fps` | Double | `0`     | Output rate cap shared across all source pads (0 = unlimited). Only set for local file sources; setting on RTSP/live sources can stall the pipeline. |

## Pipeline Examples

### Local files with per-source FPS counters

```bash
gst-launch-1.0 \
  gvastreammux name=mux max-fps=30 \
  ! queue \
  ! gvadetect model=model.xml device=GPU \
    pre-process-backend=va-surface-sharing \
  ! gvastreamdemux name=demux \
  demux.src_0 ! queue ! gvafpscounter ! fakesink \
  demux.src_1 ! queue ! gvafpscounter ! fakesink \
  filesrc location=video0.h265 ! h265parse ! vah265dec ! mux.sink_0 \
  filesrc location=video1.h265 ! h265parse ! vah265dec ! mux.sink_1
```

### RTSP sources

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

### Per-source watermark and display

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

`gvastreamdemux` requires `GstAnalyticsBatchMeta` on every incoming buffer. The mux populates
two fields:

| Field                | Type     | Description |
|----------------------|----------|-------------|
| `streams[0].index`   | guint    | Source pad index this buffer originated from. |
| `n_streams`          | gsize    | Number of contributing pads in the batch. |

## Error Conditions

| Condition                             | Behavior |
|---------------------------------------|----------|
| Buffer missing `GstAnalyticsBatchMeta`| `GST_FLOW_ERROR` (pipeline stops). |
| `streams[0].index` out of range       | Buffer dropped with `GST_FLOW_ERROR`. |
| No `src_<index>` pad created          | Buffer dropped with `GST_FLOW_ERROR`. Add the missing pad to receive frames from that source. |
| Pad index ≥ 256 on request            | `request_new_pad` returns `NULL` (rejected by the element). |

## Element Details (gst-inspect-1.0)

```
Factory Details:
  Long-name                GVA Stream Demuxer
  Klass                    Video/Demuxer
  Description              Demuxes a single stream into multiple output pads based on
                           GstAnalyticsBatchMeta streams[0].index. Must be used with
                           gvastreammux.
  Author                   Intel Corporation

Pad Templates:
  SINK template: 'sink'    (Always, video/x-raw, video/x-raw(memory:VAMemory) NV12,
                             video/x-raw(memory:DMABuf) DMA_DRM)
  SRC template:  'src_%u'  (On request, same caps as sink)

Element Properties:
  max-fps  Double, range 0-Inf, default 0
```

Run `gst-inspect-1.0 gvastreamdemux` against your installation for the authoritative output.
