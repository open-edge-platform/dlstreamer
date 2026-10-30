# Structured Metrics Messages

Some DL Streamer elements can post their measurements on the pipeline bus as GStreamer
element messages. An application then reads typed values from a `GstStructure` instead of
parsing log lines or JSON metadata. The values are the element's own measurements, so the
application does not need to recompute them.

Posting is opt-in. Each producer has a `post-metrics` setting, which is `false` by default.
When it is not set, nothing is posted and the element's other output (logs, tracer records,
JSON metadata) is unchanged.

Messages are posted once per request or once per aggregation window, never per frame or per
token.

## Message format

- The message type is `GST_MESSAGE_ELEMENT`, and its source is the element that measured
  the values.
- The structure name is `dlstreamer/<kind>`, and identifies which fields to expect.
- Every structure has a `version` field (`uint`). The version is increased when a field is
  removed or changes its meaning or type. Fields may be added without a version change.
- Units are part of the field name, for example `_ms` for milliseconds. Counts are integers.
- Messages carry no timestamp. Use the time the message is received.

A consumer should process the versions it knows, ignore fields it does not know, and skip
(and report) a message whose version it does not know.

## Messages

| Structure name | Posted by | Enable with | Posted |
|---|---|---|---|
| `dlstreamer/latency-window` | [latency_tracer](./latency_tracer.md) | `post-metrics=true` tracer parameter | Once per interval window per source-to-sink branch |

### `dlstreamer/latency-window`

One `latency_tracer` interval window of a source-to-sink branch. It is posted by the branch's
sink element when the tracer logs the matching `latency_tracer_pipeline_interval` record,
and only when the tracer `flags` include `pipeline`.

| Field | Type | Description |
|---|---|---|
| `version` | uint | Schema version, `1` |
| `pipeline` | string | Name of the pipeline |
| `source` | string | Name of the branch's source element |
| `sink` | string | Name of the branch's sink element |
| `window_ms` | double | Actual length of the window |
| `count` | uint | Number of frames that reached the sink in the window |
| `sum_ms` | double | Sum of the frame latencies in the window |
| `min_ms` | double | Minimum frame latency in the window |
| `max_ms` | double | Maximum frame latency in the window |

The frame latency is the time from the source element pushing a frame to the frame being
pushed into the sink. The mean frame latency of a window is `sum_ms / count`, and its frame
rate is `1000 * count / window_ms`. `window_ms / count`, the `latency` field of the log
record, is the time between output frames: it describes throughput, not how long a frame
takes.

The message carries sums and counts instead of averages, so windows of several intervals or
branches can be combined exactly: add up `count`, `sum_ms` and `window_ms`, and take the
minimum and maximum of `min_ms` and `max_ms`.

## Reading messages in Python

```python
import gi

gi.require_version("Gst", "1.0")
from gi.repository import GLib, Gst

# Tracers are created by Gst.init(), so set GST_TRACERS before it runs, e.g.
# GST_TRACERS="latency_tracer(flags=pipeline,interval=1000,post-metrics=true)"
Gst.init(None)
pipeline = Gst.parse_launch("videotestsrc num-buffers=300 ! video/x-raw,framerate=30/1 ! fakesink sync=true")
loop = GLib.MainLoop()


def on_message(bus, message):
    if message.type == Gst.MessageType.ELEMENT:
        s = message.get_structure()
        if s.get_name() == "dlstreamer/latency-window":
            if s.get_value("version") != 1:
                print(f"skipping unknown {s.get_name()} version {s.get_value('version')}")
            else:
                count = s.get_value("count")
                print(f"{s.get_value('sink')}: avg latency {s.get_value('sum_ms') / count:.2f} ms, "
                      f"{1000 * count / s.get_value('window_ms'):.1f} fps")
    elif message.type in (Gst.MessageType.EOS, Gst.MessageType.ERROR):
        loop.quit()


bus = pipeline.get_bus()
bus.add_signal_watch()
bus.connect("message", on_message)
pipeline.set_state(Gst.State.PLAYING)
loop.run()
pipeline.set_state(Gst.State.NULL)
```

From the command line, `gst-launch-1.0 -m` prints every posted message.
