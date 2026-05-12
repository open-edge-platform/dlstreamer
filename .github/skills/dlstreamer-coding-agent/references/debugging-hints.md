# Debugging Hints Reference

Common debugging patterns, execution hints, and pitfalls encountered when developing and testing DL Streamer pipelines.

## Start with Self-contained Validation

If an app uses external inputs/outputs (cameras, WebRTC, MQTT), start by simulating
them with local files. Only move to full e2e validation after the self-contained test passes.

## Docker Testing Conventions

- **Always use `--init`** for signal forwarding.
- **Use `timeout` inside the container** for predictable termination:
  ```bash
  docker run --init --rm ... timeout -k 5 --signal=KILL 15 python3 app.py
  ```
- **Close stdin** for non-interactive runs with `< /dev/null`. Guard `input()` with `try/except EOFError`.
- **Use fragmented MP4** (`mp4mux fragment-duration=1000`).
- **For interactive stdin apps**, pipe commands via a FIFO:
  ```bash
  docker run --init --rm ... bash -c '
    mkfifo /tmp/ctrl
    (sleep 10; echo "record 0"; sleep 5; echo "stop"; sleep 2; echo "quit") > /tmp/ctrl &
    python3 app.py < /tmp/ctrl
    rm -f /tmp/ctrl'
  ```
  **Always test interactive apps with simulated input scripts.**

## First-Run Model Compilation

The first run with a new model on GPU triggers OpenVINO kernel compilation — expected, not a hang.

| Model Type | First Run | Subsequent |
|------------|----------|------------|
| Detection (YOLO, RT-DETR, D-FINE) | 1–3 min | < 10 sec |
| VLM (MiniCPM-V, Qwen2.5-VL, InternVL) | 5–10 min | < 30 sec |
| Both in same pipeline | 7–12 min | < 30 sec |

## Common Gotchas

See also [Pipeline Design Rules](./pipeline-construction.md#pipeline-design-rules).

| Gotcha | Impact | Mitigation |
|--------|--------|------------|
| `mp4mux` without EOS | Unplayable output — missing `moov` atom | Use `mp4mux fragment-duration=1000` (see [Output & Metadata](./pipeline-construction.md#output--metadata)) |
| `.ts` / `.mkv` files with audio tracks | `not-linked` error on demuxer when audio pad is unlinked | Use `decodebin3 caps="video/x-raw(ANY)"` to suppress audio pads (see [Decode Robustness](./pipeline-construction.md#decode-robustness)) |
| `queue` blocking EOS propagation | Pipeline hangs on shutdown in multi-branch pipelines | Add `flush-on-eos=true` to all queues |
| `webrtcsink` not on host | Element creation fails at runtime | Runtime check with `Gst.ElementFactory.find()` + fallback |
| `webrtcsink` signaling "Connection refused" | Built-in signaling server not reachable | Set `run-signalling-server=true run-web-server=true` (both default to `false`). Set `signalling-server-port=8443`. Use `--network host` in Docker |
| Docker stdin closed (`< /dev/null`) | `input()` / `sys.stdin.readline()` raises `EOFError` | Guard stdin reads with `try/except EOFError` |
| Multi-stream shared model without batching | Frames serialized, low GPU utilization | Set `model-instance-id=shared` + `batch-size=N` on all streams |
| `buffer.copy()` immutable in GStreamer ≥ 1.26 | Cannot modify PTS/DTS on copied buffer | Use `buffer.copy_deep()` for writable copies |
| `buffer.map(READ\|WRITE)` fails in `do_transform_ip` | Custom element cannot modify pixel data — returns `success=False` | Override `do_prepare_output_buffer` to allocate a new buffer — see [Pattern 7: Buffer Mutability](./design-patterns.md#elements-that-modify-pixel-data) |
| Short input video finishes too fast | Not enough data to validate long-running features (e.g. event-based recording, chunked output) | Use the `loop_count` parameter of `run_pipeline()` to replay the file — see [Pattern 2: Pipeline Event Loop](./design-patterns.md#pattern-2-pipeline-event-loop) |
| `multifilesrc loop=true` with MP4/MKV | `no 'moov' atom` or demuxer errors on second iteration — demuxer cannot re-initialize from a raw byte restart | Do not use `multifilesrc loop=true` with MP4 or MKV. Use `filesrc` + EOS-seek-to-start in the bus loop (`loop_count` in `run_pipeline()`). |
| `valve drop=true` blocks preroll | Pipeline hangs at READY→PLAYING because downstream sinks never receive a buffer | Add `async=false` to the terminal sink (`filesink`, `splitmuxsink`) in valve-gated branches so it does not wait for preroll |
| `GLib.idle_add` callbacks never fire | Commands dispatched via `GLib.idle_add()` are silently queued but never executed | When using `bus.timed_pop_filtered()` instead of a GLib main loop, pump the default context each iteration — see [Pattern 2: Pipeline Event Loop](./design-patterns.md#pattern-2-pipeline-event-loop) |
| GitHub LFS video URLs return HTML | `curl -L` on `github.com/.../raw/main/file.mp4` may return an HTML redirect page instead of video data for Git LFS-hosted files | Use Pexels direct video-file URLs or local files from existing samples for default test videos. Fall back to `edge-ai-resources` only if confirmed to work with `curl -L` |
| First-run GPU model compilation appears hung | No output for 5–10 minutes, process in `D` state | Expected behavior — OpenVINO compiles GPU kernels on first use. See [First-Run Model Compilation](#first-run-model-compilation) |

## Validation Checklist

Verify each item after the first successful run of a new application:

1. **Output video playable:** `gst-discoverer-1.0 results/output.mp4` should report codec, resolution, and duration
2. **JSONL non-empty:** `wc -l results/*.jsonl` should show detection/classification lines
3. **FPS reasonable:** check `gvafpscounter` stdout output for expected throughput
4. **For Docker runs:** use fragmented MP4 and `< /dev/null` for first validation pass, then test interactive features separately if applicable
