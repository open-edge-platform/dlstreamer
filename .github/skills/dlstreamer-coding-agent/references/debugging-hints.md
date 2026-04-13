# Debugging Hints Reference

Common debugging patterns, execution hints, and pitfalls encountered when developing and testing DLStreamer pipelines.

## Start with Self-contained Validation

If an application uses external inputs/outputs (USB or RTPS cameras, WebRTC output, MQTT bus),
ALWAYS start with simulating external components by local input / otput files.
Only if an application works in self-contained environment, then start full e2e validation with external elements.

## Docker Testing Conventions

When testing applications inside Docker containers, follow these conventions to avoid
common signal-handling and output-finalization issues:

- **Always use `--init`** for proper signal forwarding (`docker run --init ...`).
  Without it, signals like SIGINT are not delivered to the Python process.
- **Use `timeout` inside the container** (not outside) for predictable termination:
  ```bash
  docker run --init --rm ... timeout -k 5 --signal=KILL 15 python3 app.py
  ```
- **Close stdin for non-interactive validation** runs with `< /dev/null`. Guard
  `input()` / `sys.stdin.readline()` calls with `try/except EOFError`.
- **Use fragmented MP4** (`mp4mux fragment-duration=1000`) so output files are valid
  regardless of how the container is stopped — Docker `stop`, `kill`, or `timeout`.
- **For interactive stdin apps**, pipe commands via a FIFO:
  ```bash
  docker run --init --rm ... bash -c '
    mkfifo /tmp/ctrl
    (sleep 10; echo "record 0"; sleep 5; echo "stop"; sleep 2; echo "quit") > /tmp/ctrl &
    python3 app.py < /tmp/ctrl
    rm -f /tmp/ctrl'
  ```
  Note: FIFO-based stdin is unreliable across Docker's PTY layer. Prefer non-interactive
  validation first, then test interactive features separately if needed.
  **Always test interactive applications with simulated user input scripts — do not ask
  users to manually interact with applications while they are being developed.**

## Common Gotchas

Known pitfalls that frequently cause debugging time during pipeline development.
See also [Pipeline Design Rules](./pipeline-construction.md#pipeline-design-rules) for
the prescriptive rules (Rules 7 and 8) that prevent many of these issues.

| Gotcha | Impact | Mitigation |
|--------|--------|------------|
| `mp4mux` without EOS | Unplayable output — missing `moov` atom | Use `mp4mux fragment-duration=1000` ([Rule 7](./pipeline-construction.md#rule-7--use-fragmented-mp4-for-robust-output)) |
| `.ts` / `.mkv` files with audio tracks | Pipeline crash on missing audio codec | Filter non-fatal decodebin errors ([Rule 8](./pipeline-construction.md#rule-8--handle-audio-tracks-in-video-only-pipelines)) |
| `queue` blocking EOS propagation | Pipeline hangs on shutdown in multi-branch pipelines | Add `flush-on-eos=true` to all queues |
| `webrtcsink` not on host | Element creation fails at runtime | Runtime check with `Gst.ElementFactory.find()` + fallback |
| `webrtcsink` signaling "Connection refused" | Built-in signaling server not reachable | Set `run-signalling-server=true run-web-server=true` (both default to `false`). Set `signalling-server-port=8443`. Use `--network host` in Docker |
| Docker stdin closed (`< /dev/null`) | `input()` / `sys.stdin.readline()` raises `EOFError` | Guard stdin reads with `try/except EOFError` |
| Multi-stream shared model without batching | Frames serialized, low GPU utilization | Set `model-instance-id=shared` + `batch-size=N` on all streams |
| `buffer.copy()` immutable in GStreamer ≥ 1.26 | Cannot modify PTS/DTS on copied buffer | Use `buffer.copy_deep()` for writable copies |
| Short input video finishes too fast | Not enough data to validate long-running features (e.g. event-based recording, chunked output) | Add a `--loop N` CLI argument that seeks back to the start on EOS instead of stopping — see implementation below |
| `valve drop=true` blocks preroll | Pipeline hangs at READY→PLAYING because downstream sinks never receive a buffer | Add `async=false` to the terminal sink (`filesink`, `splitmuxsink`) in valve-gated branches so it does not wait for preroll |
| `GLib.idle_add` callbacks never fire | Commands dispatched via `GLib.idle_add()` are silently queued but never executed | When using `bus.timed_pop_filtered()` instead of a GLib main loop, pump the default context each iteration — see [Pattern 12: Pipeline Event Loop](./design-patterns.md#pattern-12-pipeline-event-loop) |
| GitHub LFS video URLs return HTML | `curl -L` on `github.com/.../raw/main/file.mp4` may return an HTML redirect page instead of video data for Git LFS-hosted files | Use Pexels direct video-file URLs or local files from existing samples for default test videos. Fall back to `edge-ai-resources` only if confirmed to work with `curl -L` |

## Pipeline Event Loop

See [Pattern 12: Pipeline Event Loop](./design-patterns.md#pattern-12-pipeline-event-loop)
in the Design Patterns Reference for ready-to-use event loop code (simple + interruptible variants).

## Looping Short Input Videos

When a test video is too short to exercise features like event-based recording or multi-segment
output, use the `loop_count` parameter of `run_pipeline()` to replay the file multiple times
without restarting the pipeline. See [Pattern 12: Pipeline Event Loop](./design-patterns.md#pattern-12-pipeline-event-loop)
for the implementation.

## Validation Checklist

Verify each item after the first successful run of a new application:

1. **Output video playable:** `gst-discoverer-1.0 results/output.mp4` should report codec, resolution, and duration
2. **JSONL non-empty:** `wc -l results/*.jsonl` should show detection/classification lines
3. **FPS reasonable:** check `gvafpscounter` stdout output for expected throughput
4. **For Docker runs:** use fragmented MP4 and `< /dev/null` for first validation pass, then test interactive features separately if applicable
