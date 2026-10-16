# gvatrigger GOP Gate Sample

This sample demonstrates `gvatrigger`, a coded-size motion GOP gate for H.264/H.265 streams.

`gvatrigger` reads only NAL unit sizes (no decoding, no pixels) to identify motion at the GOP level.
It buffers access units from the start of each GOP; if motion is detected, the buffer is flushed and
the rest of the GOP is forwarded live. If no motion is detected before the next IDR, the entire buffer
is permanently discarded. This achieves bandwidth reduction by gating out quiet video segments while
preserving motion video in full.

An exponential moving average (EMA) tracks the size of a quiet frame, and a frame larger than
`max(k * baseline, baseline + margin)` is flagged as motion. Unlike `gvatagger`, which tags every
frame, `gvatrigger` decides whether to buffer or forward entire GOPs.

## How It Works

| Phase | Behavior |
|-------|----------|
| Startup (first 60 frames) | Buffer all AUs; build baseline from quiet frame sizes |
| Quiet GOP | Buffer AUs; update baseline (EMA) with frame sizes |
| Motion Detected | Flush buffer; mark as forwarding (sticky) |
| Forwarding Phase | Forward all AUs live until next IDR |
| Next IDR (no motion) | Discard entire buffered GOP; resume buffering |
| Cache Trim | Drop oldest buffered AUs if cache exceeds max-cache-time-ms or buffer count |

## Bandwidth Reduction Metrics

The sample tracks:
- **Input frames**: Total decoded frames before gvatrigger
- **Forwarded frames**: Frames from motion GOPs that passed through
- **Dropped frames**: Frames from discarded quiet GOPs
- **Bandwidth reduction**: Percentage of frames eliminated by gating

```mermaid
graph LR
    A["videotestsrc (long scene)"] --> B["encoder"]
    B --> C["h264parse / h265parse"]
    C --> D["gvatrigger GOP gate"]
    D --> E["decodebin"]
    E --> F["frame counter"]
    F --> G["statistics"]
```

## Test Scene

By default, a 60-second synthetic scene is generated:

| Time | Phase | Content | Expected Behavior |
|------|-------|---------|-------------------|
| 0-15 s | static | `smpte75` color bars | Quiet GOP, buffered then discarded |
| 15-30 s | ball | small moving ball | Motion detected, flushed and forwarded |
| 30-45 s | static | `smpte75` color bars | Quiet GOP, buffered then discarded |
| 45-60 s | noise | full-frame noise | Motion detected, flushed and forwarded |

Expected result: ~50% bandwidth reduction (half the scene is static and gated out).

## Running

### Prerequisites

DL Streamer must be installed. No extra Python packages are needed.

### Run the Sample

The quickest way is the shell script:

```bash
cd samples/gstreamer/python/gvatrigger_motion
./gvatrigger_motion.sh                    # Synthetic scene (no file output)
./gvatrigger_motion.sh synthetic /tmp/out.mp4  # Synthetic scene → output MP4
./gvatrigger_motion.sh /path/to/input.mp4 /tmp/out.mp4  # Input file → output MP4
```

Or specify custom input, encoder, or gvatrigger parameters:

```bash
./gvatrigger_motion.sh /path/to/input.mp4  # Custom input (no output)
ENCODER=vah265 ./gvatrigger_motion.sh synthetic /tmp/gop_gate.mp4  # VA encoder
K=2.0 ./gvatrigger_motion.sh synthetic /tmp/out.mp4   # More sensitive (lower threshold)
K=5.0 ./gvatrigger_motion.sh                          # Less sensitive (higher threshold)
ALPHA=0.1 ./gvatrigger_motion.sh synthetic /tmp/out.mp4  # Faster baseline adaptation
MAX_CACHE_TIME_MS=2000 ./gvatrigger_motion.sh         # Tighter cache control
```

## Usage Examples

### Synthetic Scene with Video Output

Generate the test scene and save the output:

```bash
./gvatrigger_motion.sh synthetic /tmp/gop_gate_demo.mp4
```

This creates a 60-second video where:
- Frames from quiet GOPs are **dropped** (missing in output)
- Frames from motion GOPs are **forwarded** (present in output)
- Expected result: ~50% bandwidth reduction

### Synthetic Scene with Different Encoder

Use VA hardware encoders for encoding the synthetic scene:

```bash
ENCODER=vah264 ./gvatrigger_motion.sh synthetic /tmp/output.mp4
ENCODER=vah265 ./gvatrigger_motion.sh synthetic /tmp/output.mp4
```

### Real Input with Output

Process an existing video file and save the gated output:

```bash
./gvatrigger_motion.sh /path/to/input.mp4 /tmp/output.mp4
./gvatrigger_motion.sh rtsp://camera/stream /tmp/output.mp4
```

### Tune Motion Detection

Adjust sensitivity while outputting video:

```bash
K=2.0 ./gvatrigger_motion.sh synthetic /tmp/sensitive.mp4    # More sensitive
K=5.0 ./gvatrigger_motion.sh synthetic /tmp/lenient.mp4      # Less sensitive
ALPHA=0.1 ./gvatrigger_motion.sh synthetic /tmp/fast.mp4     # Fast baseline adaptation
```

### Environment Variables

- `INPUT`: Input video file or RTSP stream (default: `synthetic`)
- `OUTPUT`: Output MP4 file (default: no file output)
- `ENCODER`: Encoder for synthetic scene: `openh264`, `vah264`, `vah265` (default: `openh264`)
- `K`: Motion threshold multiplier, size > k * baseline → motion (default: `3.0`)
- `ALPHA`: EMA update rate (0-1), lower = slower baseline adaptation (default: `0.03`)
- `MARGIN`: Minimum threshold margin in bytes (default: `64`)
- `MAX_CACHE_TIME_MS`: Max buffered time before oldest AUs are dropped (default: `4000`)
- `MIN_BASELINE`: Floor for EMA baseline in bytes (default: `100`)
- `GST_DEBUG`: GStreamer debug level (e.g., `gvatrigger:6`)

### Run Directly with Python

```bash
python3 gvatrigger_motion.py                                    # Synthetic scene
python3 gvatrigger_motion.py --output /tmp/gop_gate.mp4        # Synthetic → output file
python3 gvatrigger_motion.py --input /path/to/video.mp4 --output /tmp/out.mp4
python3 gvatrigger_motion.py --k 2.5 --alpha 0.1
python3 gvatrigger_motion.py --encoder vah265 --output /tmp/out.mp4
python3 gvatrigger_motion.py --gst-debug "gvatrigger:6"
```

### Tuning the Gate

- **Miss motion**: Increase `k` or decrease `alpha` (slower baseline growth)
- **Gate out motion**: Decrease `k` or increase `alpha` (faster baseline growth)
- **Memory issues**: Decrease `max-cache-time-ms`
- **Jerky playback**: Increase `max-cache-time-ms`

### Debug Logging

Per-frame motion detection logs (requires `--gst-debug "gvatrigger:6"`):

```bash
GST_DEBUG=gvatrigger:6 python3 gvatrigger_motion.py 2>&1 | grep "motion="
```

Example output:
```
... gvatrigger gvaTrigger:gst_gva_trigger_chain:<trigger> ... motion=1 baseline=1234.5 size=5678 threshold=...
... gvatrigger gvaTrigger:gst_gva_trigger_chain:<trigger> ... motion=0 baseline=1200.0 size=456 threshold=...
```

## Output Statistics

The sample prints a summary at the end:

```
=== gvatrigger GOP gate summary ===
input frames        : 1800
forwarded frames    : 900 (50.0%)
dropped frames      : 900 (50.0%)
bandwidth reduction : 50.0%
data forwarded (KB) : 1234.5
```

## Technical Notes

- **No entropy decoding**: gvatrigger reads only NAL unit sizes; no pixel data is accessed
- **Memory efficient**: Buffers entire GOPs (typically 60 frames = ~2-5 MB), not full streams
- **Cross-platform**: Works with H.264 (byte-stream, avc) and H.265 (byte-stream, hvc1, hev1)
- **Live compatible**: Designed for real-time streams; forwarding is sticky after first motion
- **Codec agnostic**: Only parses H.26x syntax; works with any encoder output
