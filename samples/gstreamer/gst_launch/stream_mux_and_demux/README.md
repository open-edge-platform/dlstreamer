# Multi-Stream Inference with gvastreammux and gvastreamdemux

This directory contains sample scripts demonstrating how to use the `gvastreammux` and `gvastreamdemux`
elements for multi-stream video inference through a single shared pipeline.

## How It Works

The `gvastreammux` element collects video frames from multiple input sources and interleaves them
through a single downstream processing chain (e.g., `gvadetect`). The optional `gvastreamdemux`
element then routes the processed frames back to per-source branches based on metadata.

This enables running inference on N video streams using a single model instance, significantly
reducing GPU memory usage and improving throughput compared to N independent pipelines.

The sample utilizes GStreamer command-line tool `gst-launch-1.0` which can build and run a GStreamer
pipeline described in a string format.

### Key Features

- **Round-Robin Scheduling**: One frame from each source per batch cycle — no frame dropping
- **Batch Size = Input Streams**: Each cycle outputs exactly one frame per input source
- **Source Tracking**: Each buffer carries `GstAnalyticsBatchMeta` — `streams[0].index` holds the source id, `n_streams` holds the total source count
- **Optional Demux**: Use `gvastreamdemux` when you need per-source downstream processing (watermark, encoding, etc.)

### Pipeline Architecture

**Mux only (shared output):**
```
Source 0 ─┐                                              ┌─ gvafpscounter ─ fakesink
          ├─ gvastreammux ─ queue ─ gvadetect ─ queue ──┤
Source 1 ─┘                                              └─ (all frames interleaved)
```

**Mux + Demux (per-source output):**
```
Source 0 ─┐                                                          ┌─ queue ─ gvafpscounter ─ fakesink
          ├─ gvastreammux ─ queue ─ gvadetect ─ gvastreamdemux ────┤
Source 1 ─┘                                                          └─ queue ─ gvafpscounter ─ fakesink
```

## Prerequisites

### 1. Verify DL Streamer Installation

Ensure DL Streamer is properly compiled and the elements are available:

```bash
gst-inspect-1.0 gvastreammux
gst-inspect-1.0 gvastreamdemux
```

### 2. Prepare Video Sources

The script supports two source types:

- **Local files**: Any video file decodable by GStreamer (e.g., MP4, H.264, H.265)
- **RTSP streams**: Live RTSP sources (e.g., from IP cameras or an RTSP test server)

### 3. Prepare an Inference Model

Download or provide an OpenVINO IR model (`.xml` + `.bin`). For example, using Open Model Zoo:

```bash
# Download a YOLOv11 or SSD model
# Place model.xml and model.bin in a known path
```

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
- `--demux`: Enable gvastreamdemux for per-source output
- `--max-fps FPS`: Set max-fps for local file sources (default: 0, only used with `file` source)
- `-d, --device DEVICE`: Inference device: GPU, CPU, NPU (default: GPU)
- `-h, --help`: Show help message

**Examples:**

1. **Two RTSP streams, shared inference (mux only)**
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

3. **Two local files with max-fps throttling**
   ```bash
   ./stream_mux_demux_sample.sh \
     --model /path/to/model.xml \
     --source file \
     --input1 /path/to/video0.h265 \
     --input2 /path/to/video1.h265 \
     --max-fps 30
   ```

4. **Local files with demux and debug logging**
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
- FPS measurements from `gvafpscounter` printed to console
- When using `--demux`, each source stream gets its own FPS counter showing independent throughput
- Without `--demux`, a single FPS counter shows the combined interleaved throughput

## Troubleshooting

| Issue | Solution |
|-------|----------|
| `gvastreammux` not found | Rebuild DL Streamer: `make build && sudo -E make install` |
| Pipeline stalls with RTSP + max-fps | Remove `max-fps` — live sources are naturally rate-limited |
| `gvastreamdemux` reports source count mismatch | Ensure the number of `demux.src_*` pads matches `mux.sink_*` pads |
| Low FPS with GPU inference | Try `pre-process-backend=va-surface-sharing` and increase `nireq` |
