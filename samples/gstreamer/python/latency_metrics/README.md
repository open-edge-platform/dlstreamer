# Latency Metrics

This sample shows how a Python application reads pipeline latency from the GStreamer bus,
without parsing log output.

[latency_tracer](../../../../docs/user-guide/dev_guide/latency_tracer.md) is started with
`post-metrics=true`. For every interval window of every source-to-sink branch, it posts a
`dlstreamer/latency-window` element message with typed fields: the window length, the frame
count, and the sum, minimum, maximum, sum of squares and histogram of the frame latency.
The frame latency is the time from the source element pushing a frame to the frame being
pushed into the sink. The fields are described in
[Structured Metrics Messages](../../../../docs/user-guide/dev_guide/metrics_messages.md).

## How It Works

1. `GST_TRACERS` requests the tracer before `Gst.init()`, which creates the tracers:

   ```python
   os.environ.setdefault("GST_TRACERS", "latency_tracer(flags=pipeline,interval=1000,post-metrics=true)")
   ```

2. A bus watch receives every `GST_MESSAGE_ELEMENT` named `dlstreamer/latency-window`, checks
   its `version`, and derives the window's values from it:

   ```python
   mean_latency_ms = sum_ms / count
   stddev_ms = sqrt(sum_sq_ms / count - mean_latency_ms ** 2)
   p95_ms = percentile(bucket_bounds_ms, bucket_counts, 0.95, min_ms, max_ms)
   fps = 1000 * count / window_ms
   ```

   The percentile is estimated from the histogram by interpolating within the bucket that
   holds the requested rank, so it is accurate to within one bucket width. The other values
   are exact. `window_ms / count` is the time between output frames (`1000 / fps`), not a
   latency, so the sample does not report it.

3. Each window is added to the totals of its branch: counts, sums and bucket counts are
   added up. At EOS the sample prints a summary per branch. Because the messages carry sums
   and counts, the totals are exact; an average of the per-window averages would not be.

No Python code runs per frame: the tracer posts one message per window, by default one per
second.

## Running

```sh
python3 latency_metrics.py
```

By default the sample runs a `videotestsrc` pipeline that needs no models. Pass any
pipeline description to measure it instead, for example:

```sh
python3 latency_metrics.py "filesrc location=video.mp4 ! decodebin3 ! \
    gvadetect model=yolo11n.xml device=GPU ! queue ! fakesink sync=false"
```

To change the tracer parameters, set `GST_TRACERS` yourself; the sample then uses your value.
Keep `flags=pipeline` and `post-metrics=true` in it.

## Sample Output

```text
videotestsrc0 -> fakesink0: 30 frames in 1000 ms, latency mean 1.84 ms, p95 1.86, max 1.86; 30.0 fps
...
videotestsrc0 -> fakesink0: 22 frames in 763 ms, latency mean 1.86 ms, p95 2.00, max 2.10; 28.8 fps
Total videotestsrc0 -> fakesink0: 299 frames, latency mean 1.84 ms, stddev 0.03, p50 1.90, p95 1.99, p99 2.00, min 1.79, max 2.10; 30.0 fps
```

The last window ends at EOS and is shorter than the interval.

Frame latency includes the time frames wait in queues. In
`videotestsrc ! video/x-raw,framerate=30/1 ! queue ! identity sleep-time=12000 ! fakesink sync=true`,
the source is not live and produces frames faster than the sink plays them at 30 fps, so
the queue fills up and every frame waits in it. The frame latency grows to about 650 ms,
while the frame rate stays at 30 fps.
