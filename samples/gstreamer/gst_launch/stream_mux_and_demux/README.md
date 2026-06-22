# Multi-Stream Inference with gvastreammux and gvastreamdemux

This directory contains sample scripts demonstrating how to use the `gvastreammux` and
`gvastreamdemux` elements for multi-stream video inference through a single shared pipeline.

## How It Works

`gvastreammux` collects video frames from multiple input pads into per-pad queues, then
assembles **PTS-aligned batches** that are pushed through one downstream chain (typically
inference). The optional `gvastreamdemux` element reads `GstAnalyticsBatchMeta` to route
the processed frames back to per-source branches.

Running inference on N streams with one mux+inference instance significantly reduces GPU
memory usage and improves throughput compared to N independent pipelines.

The sample uses GStreamer's command-line tool `gst-launch-1.0`, which can build and run a
pipeline described in a string format.

### Key Behavior

- **PTS-anchor batch assembly**: each batch picks the earliest PTS across non-EOS pads as the
  anchor; the mux waits up to `max-wait-time` for other pads to deliver a buffer with
  `|pts - anchor| ≤ pts-tolerance`. Pads that miss the window do not block the batch — a
  partial batch is pushed downstream once the wait expires.
- **Back-pressure on upstream**: each pad has a bounded queue (`max-queue-size`). When full,
  upstream blocks instead of dropping frames.
- **Late-frame dropping**: a buffer whose PTS is too far behind the most recent pushed batch
  is dropped (the batch already moved on).
- **Per-pad PTS normalization (`sync-mode`)**: when sources have unrelated PTS timelines
  (multiple files, multiple cameras with independent clocks), the mux can normalize PTS
  before scheduling. See [Sync Mode](#sync-mode) below.
- **Source tracking via `GstAnalyticsBatchMeta`**: every output buffer carries this meta —
  `streams[0].index` is the source pad id; `n_streams` is the batch size for that buffer.
- **Optional demux**: use `gvastreamdemux` only if you need per-source downstream processing
  (watermark, encoding, etc.). Otherwise the mux's single src pad already carries every frame
  from every source.

### Pipeline Architecture

**Mux only (shared output):**
```
Source 0 ─┐                                              ┌─ gvafpscounter ─ fakesink
          ├─ gvastreammux ─ queue ─ gvadetect ─ queue ──┤
Source 1 ─┘                                              └─ (all source frames in one stream)
```

**Mux + Demux (per-source output):**
```
Source 0 ─┐                                                        ┌─ queue ─ gvafpscounter ─ fakesink
          ├─ gvastreammux ─ queue ─ gvadetect ─ gvastreamdemux ──┤
Source 1 ─┘                                                        └─ queue ─ gvafpscounter ─ fakesink
```

### Sync Mode

`gvastreammux` exposes a `sync-mode` property to control how PTS are normalized across pads
before batching:

| Mode        | What it does                                                                         | Use case |
|-------------|--------------------------------------------------------------------------------------|----------|
| `none`      | Raw PTS (default). Assumes upstream has already aligned timestamps.                  | Single source, or upstream-aligned files. |
| `first-pts` | Subtract each pad's first PTS so all pads start at 0.                                | Multiple files whose PTS bases differ. |
| `segment`   | Subtract each pad's `GST_EVENT_SEGMENT.start`.                                       | Standard GStreamer pipelines including seek/trick mode. |
| `pipeline`  | Overwrite PTS with pipeline running time at arrival.                                 | Single-machine multi-live source where source PTS are unreliable. |
| `ntp`       | Replace PTS with `GstReferenceTimestampMeta.timestamp` if present.                   | Cross-device cameras with NTP/PTP, paired with `rtspsrc add-reference-timestamp-meta=true`. |

## Prerequisites

### 1. Verify DL Streamer Installation

Make sure the elements are available:

```bash
gst-inspect-1.0 gvastreammux
gst-inspect-1.0 gvastreamdemux
```

### 2. Prepare Video Sources

The sample script supports two source types:

- **Local files**: any file decodable by GStreamer (e.g. MP4, H.264, H.265).
- **RTSP streams**: live RTSP sources (e.g. IP cameras or an RTSP test server).

### 3. Prepare an Inference Model

Provide an OpenVINO IR model (`.xml` + `.bin`).

## Running the Sample

**Usage:**
```bash
./stream_mux_demux_sample.sh [OPTIONS]
```

**Options:**
- `-m, --model PATH`: Path to inference model XML file (required)
- `-s, --source TYPE`: Source type: `file` or `rtsp` (default: `rtsp`)
- `-i, --input1 URI`: First input source URI
- `-j, --input2 URI`: Second input source URI
- `--demux`: Enable `gvastreamdemux` for per-source output
- `--max-fps FPS`: Output rate cap (only for `file` sources; default `0` = unlimited)
- `--sync-mode MODE`: PTS normalization (`none|first-pts|segment|pipeline|ntp`, default `none`)
- `-d, --device DEVICE`: Inference device: `GPU`, `CPU`, `NPU` (default: `GPU`)
- `-h, --help`: Show help

**Examples:**

1. **Two RTSP streams, shared inference**
   ```bash
   ./stream_mux_demux_sample.sh \
     --model /path/to/model.xml \
     --source rtsp \
     --input1 rtsp://localhost:8554/stream \
     --input2 rtsp://localhost:8555/stream
   ```

2. **Two RTSP streams with per-source demux output**
   ```bash
   ./stream_mux_demux_sample.sh \
     --model /path/to/model.xml \
     --source rtsp \
     --input1 rtsp://localhost:8554/stream \
     --input2 rtsp://localhost:8555/stream \
     --demux
   ```

3. **Two local files with `max-fps` throttling**
   ```bash
   ./stream_mux_demux_sample.sh \
     --model /path/to/model.xml \
     --source file \
     --input1 /path/to/video0.h265 \
     --input2 /path/to/video1.h265 \
     --max-fps 30
   ```

4. **Two local files with different PTS bases (`sync-mode=first-pts`)**
   ```bash
   ./stream_mux_demux_sample.sh \
     --model /path/to/model.xml \
     --source file \
     --input1 /path/to/clip-recorded-at-10am.mp4 \
     --input2 /path/to/clip-recorded-at-2pm.mp4 \
     --sync-mode first-pts
   ```

5. **NTP-synchronized IP cameras (`sync-mode=ntp`)**

   For `sync-mode=ntp` to take effect, every input buffer must carry a
   `GstReferenceTimestampMeta`. The standard way to provide it is:

   ```
   rtspsrc location=rtsp://cam ntp-sync=true add-reference-timestamp-meta=true
   ```

   This requires the camera to advertise an RFC7273 reference clock in its SDP. The sample
   script does not enable these options by default — to use the NTP path, modify the script
   or run a custom `gst-launch-1.0` based on the example in
   [docs/user-guide/elements/gvastreammux.md](../../../../docs/user-guide/elements/gvastreammux.md).

   ```bash
   ./stream_mux_demux_sample.sh \
     --model /path/to/model.xml \
     --source rtsp \
     --sync-mode ntp
   ```

6. **Local files with demux + debug logging**
   ```bash
   GST_DEBUG=gvastreammux:4,gvastreamdemux:4 ./stream_mux_demux_sample.sh \
     --model /path/to/model.xml \
     --source file \
     --input1 /path/to/video0.h265 \
     --input2 /path/to/video1.h265 \
     --max-fps 30 \
     --demux
   ```

**Output:**
- `gvafpscounter` prints FPS to the console.
- With `--demux`, each source has its own counter (independent throughput).
- Without `--demux`, a single counter shows the combined throughput.

## Tuning

| Property        | When to tune                                                                              |
|-----------------|-------------------------------------------------------------------------------------------|
| `pts-tolerance` | Increase if multi-source jitter exceeds the default 20 ms (e.g. RTSP over a noisy network). |
| `max-wait-time` | Increase if a slower pad regularly misses the batch window (default 40 ms).               |
| `max-queue-size`| Increase to absorb upstream bursts; decrease to react to back-pressure faster.            |
| `sync-mode`     | See [Sync Mode](#sync-mode) above.                                                        |

## Troubleshooting

| Issue | Solution |
|-------|----------|
| `gvastreammux` not found | Rebuild DL Streamer: `make build && sudo -E make install` |
| Pipeline stalls with RTSP + `max-fps` | Remove `max-fps` — live sources are already rate-limited. |
| Some sources never appear in output / starvation | One pad's PTS is far from the others. Pick a `sync-mode` (commonly `first-pts`) or widen `pts-tolerance`. |
| `gvastreamdemux` reports out-of-range source id | Add a `demux.src_<index>` pad for each source id produced by the mux, or remove the orphan source upstream. |
| Low FPS with GPU inference | Set `pre-process-backend=va-surface-sharing` and increase `nireq` on `gvadetect`. |
