# gvastreamdemux

Routes a single muxed stream back to per-source output pads based on `GstAnalyticsBatchMeta`
attached upstream by `gvastreammux`. The element is the companion to `gvastreammux` and
**must** be paired with it; it cannot work standalone.

## Overview

`gvastreamdemux` reads the `GstAnalyticsBatchMeta` attached by `gvastreammux` and routes each
source's buffer to `src_<index>`. This enables the common multi-stream pattern:

```
N sources → gvastreammux → shared inference → gvastreamdemux → N independent outputs
```

It transparently handles both of the mux's [output modes](gvastreammux.md#output-modes):

- **PASSTHROUGH input** (plain video buffers): each buffer carries a single-stream meta; the
  demux forwards it to `src_<streams[0].index>` as-is.
- **CONTAINER input** (caps `multistream/x-analytics-batch`): each buffer is a container holding
  `n_streams` sources. The demux unpacks every `streams[i]`, recovers that source's original caps
  from its `GST_EVENT_CAPS` sticky event, forwards those caps to `src_<index>` (lazily, once per
  pad), and pushes the source's buffer (`objects[0]`). This is how heterogeneous video + lidar
  batches are split back into a `video/x-raw` branch and an `application/x-lidar` branch.

Key design points:

- **Metadata-driven routing** — buffers are forwarded based on stream index. Buffers without a
  usable `GstAnalyticsBatchMeta` cause a pipeline error.
- **Per-pad caps in CONTAINER mode** — each `src_*` pad emits its own stream's caps, so different
  pads can carry different media types (e.g. `src_0` video, `src_2` lidar).
- **No frame dropping** — every source buffer is pushed to its target pad.
- **Sparse src indices** — names like `demux.src_5` work even if `src_0..src_4` were never
  created, mirroring the mux side. Indices must be in `[0, 256)`.
- **No strict count match** — the upstream `n_streams` and the number of requested `src_*` pads
  do not have to match. Buffers whose stream index exceeds the highest requested src pad
  are dropped with a `GST_ERROR`. Match the indices to the mux indices to receive every frame.

> **Important**: as with `gvastreammux`, downstream `gvadetect` should keep
> `inference-interval=1` (default). Higher values would skip whole batches in arrival order,
> meaning some source ids consistently miss inference results.

## How It Works

1. The pipeline requests source pads (`src_0`, `src_1`, ...) — typically one per upstream
   sink pad. Pad index is parsed from the requested name; sparse indices are allowed up to 255.
2. On the sink `CAPS` event the demux detects whether the input is plain video (PASSTHROUGH) or
   the `multistream/x-analytics-batch` container (CONTAINER). In CONTAINER mode it does **not**
   forward the container caps downstream — per-stream caps come from the buffers instead.
3. When a buffer arrives:
   - **PASSTHROUGH:** read `streams[0].index` and push the buffer to `src_<index>`.
   - **CONTAINER:** for each `streams[i]`, forward that stream's caps to `src_<index>` the first
     time the pad is used, then push `objects[0]` (the source's buffer) to that pad.
   If no matching `src_<index>` pad exists, the buffer is dropped with `GST_FLOW_ERROR`.
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

### Heterogeneous video + lidar (CONTAINER input)

When the mux runs in CONTAINER mode, `gvastreamdemux` unpacks each batch back into per-source
pads, restoring each source's own caps. Note there is **no** `gvadetect` between the mux and the
demux (a container buffer is not raw video). Per-stream inference (`gvadetect` for video,
`g3dinference` for lidar) would attach after the corresponding demux branch; here each branch
ends in `fakesink`:

```bash
gst-launch-1.0 -e \
  gvastreammux name=mux sync-mode=first-pts \
  ! queue \
  ! gvastreamdemux name=demux \
  demux.src_0 ! queue ! gvafpscounter ! fakesink \
  demux.src_1 ! queue ! gvafpscounter ! fakesink \
  demux.src_2 ! queue ! fakesink \
  filesrc location=video0.h265 ! h265parse ! vah265dec ! mux.sink_0 \
  filesrc location=video1.h265 ! h265parse ! vah265dec ! mux.sink_1 \
  multifilesrc location=velodyne/%06d.bin start-index=0 caps=application/octet-stream \
  ! g3dlidarparse frame-rate=10 ! mux.sink_2
```

Here `src_0` and `src_1` emit `video/x-raw`, while `src_2` emits `application/x-lidar`.

## Required Metadata

`gvastreamdemux` requires a usable `GstAnalyticsBatchMeta` on every incoming buffer. The fields it
reads depend on the input mode:

**PASSTHROUGH input:**

| Field                | Type     | Description |
|----------------------|----------|-------------|
| `streams[0].index`   | guint    | Source pad index this buffer originated from. |
| `n_streams`          | gsize    | Number of contributing pads in the batch. |

**CONTAINER input:**

| Field                       | Type      | Description |
|-----------------------------|-----------|-------------|
| `n_streams`                 | gsize     | Number of sources packed in this container. |
| `streams[i].index`          | guint     | Destination `src_*` pad for the i-th stream. |
| `streams[i].objects[0]`     | GstBuffer | The buffer pushed to that pad. |
| `streams[i].sticky_events`  | GstEvent  | The source caps (`GST_EVENT_CAPS`) set on that pad. |

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
                             video/x-raw(memory:DMABuf) DMA_DRM,
                             multistream/x-analytics-batch(meta:GstAnalyticsBatchMeta))
  SRC template:  'src_%u'  (On request, the video caps above OR application/x-lidar)

Element Properties:
  max-fps  Double, range 0-Inf, default 0
```

Run `gst-inspect-1.0 gvastreamdemux` against your installation for the authoritative output.
