# gvastreammux

Muxes multiple video streams into a single pipeline so that one downstream inference instance
(e.g. `gvadetect`) can serve N input sources, replacing N independent pipelines and significantly
reducing GPU memory usage. Each output buffer is tagged with `GstAnalyticsBatchMeta` so that
`gvastreamdemux` can later route it back to the correct source.

## Overview

`gvastreammux` collects video frames from multiple sink pads into per-pad queues, assembles
**PTS-aligned batches**, and emits one downstream buffer per source per batch. Frames are pushed
through a single `src` pad in source-id order, with each buffer carrying a `GstAnalyticsBatchMeta`
whose `streams[0].index` identifies the originating pad and `n_streams` records the batch size.

Key design points:

- **PTS-anchor batch assembly** — for each batch the element picks the earliest PTS across all
  non-EOS pads as the *anchor*, then waits up to `max-wait-time` for the other pads to deliver
  buffers whose PTS lies within `pts-tolerance` of that anchor. Pads that miss the window do
  not block the batch indefinitely; they simply do not contribute to that cycle.
- **Back-pressure on upstream** — each pad has its own bounded queue (`max-queue-size`).
  When a queue is full, the upstream chain function blocks until the output loop drains it.
  This prevents unbounded memory growth without dropping frames.
- **Late-frame dropping** — if a buffer's PTS is more than `pts-tolerance` *behind* the most
  recent pushed batch, it is dropped (the batch already moved past that time and there is no
  way to insert it).
- **Per-pad PTS normalization (`sync-mode`)** — when sources have unrelated PTS timelines
  (e.g. files starting at different timestamps, multiple cameras with independent clocks),
  the element can normalize PTS before scheduling. See the [Sync Mode](#sync-mode) section.
- **Sparse pad indices** — names like `mux.sink_5` work even if `sink_0..sink_4` were never
  created. Indices must be in `[0, 256)`.

> **Important**: each source contributes one buffer per batch, so a downstream
> `gvadetect inference-interval=N` (with N > 1) skips every Nth buffer in arrival order, which
> means certain source ids will always be skipped. Use `inference-interval=1` (the default)
> when the goal is uniform coverage across all input streams.

## How It Works

1. The pipeline requests sink pads (`sink_0`, `sink_1`, ...) — one per input source. Pad
   index is parsed from the requested name; sparse indices are allowed up to 255.
2. Upstream chain functions push buffers into per-pad queues. If a pad's queue is full, the
   chain function blocks until the output task drains it (back-pressure to upstream).
3. The output task on the src pad assembles batches in three phases:
   - **Phase 1 (anchor selection)** — pick the earliest valid PTS across all non-EOS pad
     queue heads as `batch_anchor_pts`.
   - **Phase 2 (wait for contributors)** — wait up to `max-wait-time` for every non-EOS
     pad to have a head buffer with `|pts - anchor| ≤ pts-tolerance`. Exit early once all
     eligible pads contribute.
   - **Phase 3 (collect & push)** — pop one matching buffer per pad, attach
     `GstAnalyticsBatchMeta`, push downstream in pad-index order.
4. EOS on a sink pad excludes that pad from future batches. When all pads are EOS and all
   queues are drained, the src pad emits EOS and the task pauses.
5. Per-pad `FLUSH_START` / `FLUSH_STOP` events are coalesced — the downstream flush, task
   pause, queue reset and task restart each happen exactly once per flush cycle no matter
   how many sink pads receive the events.

## Properties

| Property        | Type     | Default     | Description |
|-----------------|----------|-------------|-------------|
| `max-fps`       | Double   | `0`         | Output rate cap (0 = unlimited). Only set for local file sources; setting on RTSP/live sources can stall the pipeline. |
| `pts-tolerance` | UInt64 (ns) | `20000000` (20 ms) | Max `\|pts - anchor\|` for a buffer to count as contributing to the current batch. |
| `max-wait-time` | UInt64 (ns) | `40000000` (40 ms) | Max time the output task waits for late pads after the anchor is set. After timeout the partial batch is pushed. |
| `max-queue-size`| UInt    | `2`         | Maximum buffers per pad queue. When reached, upstream blocks (back-pressure). |
| `sync-mode`     | Enum    | `none`      | How to normalize PTS across pads. See [Sync Mode](#sync-mode). |

### Sync Mode

Multi-source pipelines often deliver buffers with PTS values that cannot be compared directly
(different file starts, multiple cameras with independent clocks, etc.). Without normalization
the anchor + tolerance scheduler either deadlocks (a far-future pad never matches the window
and back-pressures upstream) or oscillates (anchor jumps each round, batches end up
single-stream). `sync-mode` selects the normalization policy.

| Mode        | What it does                                                                 | Typical use case |
|-------------|------------------------------------------------------------------------------|------------------|
| `none`      | Use buffer PTS as-is. Default; preserves legacy behavior.                    | Single source, or upstream already aligned (e.g. `videorate`). |
| `first-pts` | Subtract each pad's first observed PTS so every pad starts at 0.             | Multiple file sources whose PTS bases differ. |
| `segment`   | Subtract each pad's `GST_EVENT_SEGMENT` start.                               | Standard GStreamer pipelines, including seek/trick mode. |
| `pipeline`  | Overwrite PTS with `clock.now() - base_time` (pipeline running time).        | Single-machine multi-live source where source PTS are unreliable. |
| `ntp`       | Replace PTS with `GstReferenceTimestampMeta.timestamp` if the buffer carries one. | Cross-device cameras synchronized via NTP/PTP, paired with `rtspsrc add-reference-timestamp-meta=true`. |

Per-pad normalization state (first PTS, segment start) is reset on `FLUSH_STOP` and on
`PAUSED → READY`, so seek and pipeline restart establish fresh baselines.

## Buffer Metadata

Each output buffer carries a `GstAnalyticsBatchMeta` with these fields populated:

| Field                  | Type     | Description |
|------------------------|----------|-------------|
| `streams[0].index`     | guint    | Source pad index this buffer originated from. |
| `n_streams`            | gsize    | Number of contributing pads in the batch this buffer is part of. |

`gvastreamdemux` reads `streams[0].index` to decide which `src_*` pad to forward the buffer to.

## Pipeline Examples

### Local files with `max-fps`

```bash
gst-launch-1.0 \
  gvastreammux name=mux max-fps=30 \
  ! queue \
  ! gvadetect model=model.xml device=GPU \
    pre-process-backend=va-surface-sharing \
  ! queue ! gvafpscounter ! fakesink \
  filesrc location=video0.h265 ! h265parse ! vah265dec ! mux.sink_0 \
  filesrc location=video1.h265 ! h265parse ! vah265dec ! mux.sink_1
```

### Multiple files with different PTS bases (`sync-mode=first-pts`)

```bash
gst-launch-1.0 \
  gvastreammux name=mux sync-mode=first-pts max-fps=30 \
  ! queue \
  ! gvadetect model=model.xml device=GPU \
  ! queue ! gvafpscounter ! fakesink \
  filesrc location=clip-recorded-at-10am.mp4 ! qtdemux ! h265parse ! vah265dec ! mux.sink_0 \
  filesrc location=clip-recorded-at-2pm.mp4  ! qtdemux ! h265parse ! vah265dec ! mux.sink_1
```

### RTSP sources, no `max-fps`

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

### Multi-camera NTP/PTP synchronization (`sync-mode=ntp`)

When sources are real IP cameras synchronized via NTP (and the SDP advertises an RFC7273
clock), set `rtspsrc add-reference-timestamp-meta=true` so each buffer carries a
`GstReferenceTimestampMeta` with absolute time, then let the mux use that as PTS:

```bash
gst-launch-1.0 \
  gvastreammux name=mux sync-mode=ntp pts-tolerance=33000000 \
  ! queue ! gvadetect model=model.xml device=GPU \
  ! queue ! gvafpscounter ! fakesink \
  rtspsrc location=rtsp://cam1 ntp-sync=true add-reference-timestamp-meta=true \
  ! rtph265depay ! h265parse ! avdec_h265 ! mux.sink_0 \
  rtspsrc location=rtsp://cam2 ntp-sync=true add-reference-timestamp-meta=true \
  ! rtph265depay ! h265parse ! avdec_h265 ! mux.sink_1
```

`pts-tolerance` is widened above the typical inter-frame interval to accommodate per-camera
network jitter; tune it for your environment.

### Pair with `gvastreamdemux` for per-source output

```bash
gst-launch-1.0 \
  gvastreammux name=mux max-fps=30 \
  ! queue \
  ! gvadetect model=model.xml device=GPU \
  ! gvastreamdemux name=demux \
  demux.src_0 ! queue ! gvafpscounter ! fakesink \
  demux.src_1 ! queue ! gvafpscounter ! fakesink \
  filesrc location=video0.h265 ! h265parse ! vah265dec ! mux.sink_0 \
  filesrc location=video1.h265 ! h265parse ! vah265dec ! mux.sink_1
```

## Tuning Notes

- `pts-tolerance` should comfortably accommodate the largest expected inter-source PTS drift
  *after* normalization. For 30 fps sources a value of 16–33 ms is a reasonable starting point.
- `max-wait-time` bounds the worst-case batch latency when one pad stops delivering. Set it
  somewhat above one frame interval (e.g. 40–50 ms for 30 fps) so transient stalls do not
  immediately break the batch.
- `max-queue-size` controls the back-pressure depth. Larger values absorb more upstream
  bursts at the cost of memory; 2–4 is usually enough.

## Element Details (gst-inspect-1.0)

```
Factory Details:
  Long-name                GVA Stream Muxer
  Klass                    Video/Muxer
  Description              Muxes multiple video streams with PTS-based synchronization
  Author                   Intel Corporation

Pad Templates:
  SINK template: 'sink_%u'  (On request, video/x-raw, video/x-raw(memory:VAMemory) NV12,
                             video/x-raw(memory:DMABuf) DMA_DRM)
  SRC template:  'src'      (Always, same caps as sink)

Element Properties:
  max-fps         Double, range 0-Inf, default 0
  pts-tolerance   UInt64 ns, default 20000000   (20 ms)
  max-wait-time   UInt64 ns, default 40000000   (40 ms)
  max-queue-size  UInt, range 1-..., default 2
  sync-mode       Enum (none, first-pts, segment, pipeline, ntp), default none
```

Run `gst-inspect-1.0 gvastreammux` against your installation for the authoritative output.
