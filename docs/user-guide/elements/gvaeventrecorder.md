# gvaeventrecorder

Caches pre/event/post video frames, triggers on detected classes with a per-class cooldown, writes full event clips to disk, and forwards PRE/EVENT/POST buffers downstream after a trigger. Pair it with `gvaframeselect` and `gvaroiscale` to prepare frames for a VLM.

## Overview

The `gvaeventrecorder` element is a `GstBaseTransform` that sits between an object-detection
element (e.g. `gvadetect`) and a downstream consumer such as `gvagenai`. It reads
`GstAnalyticsODMtd` detections already attached to each buffer and acts as a **valve**:

- **Pre-event ring buffer**: a single, continuously fed, fixed-size window of the last
  `pre-event-frames` frames is always kept, independent of any trigger. It is never reset
  by a trigger. If a trigger occurs before the ring fills (for example, at the start of a
  stream), the clip includes all pre-event frames available at that moment; later triggers
  include the full configured PRE window.
- **Trigger**: when a detection matches one of `target-classes` with confidence at or above
  `confidence-threshold`, and that class is not in cooldown, a new recording session starts.
  The session snapshots the current pre-event ring and keeps collecting frames for the full
  time the class remains detected. After the event ends, it collects `post-event-frames`
  more frames before finalizing the clip.
- **Detection gaps**: up to `event-gap-frames` consecutive frames without a matching
  detection are tolerated as part of the same event. This helps prevent detector flicker
  from splitting one event into several clips.
- **Cooldown**: each class has its own independent cooldown window of `cooldown-value`
  frames, restarted from the first qualifying detection in the
  window, to avoid a burst of repeat triggers for the same ongoing event.
- **Filtering to downstream**: nothing is forwarded until an event is accepted. At that
  moment, buffered PRE frames are replayed oldest first, followed immediately by the trigger
  frame. Subsequent EVENT and POST frames are forwarded as they arrive. Overlapping windows
  share a single chronological stream: a buffer already forwarded is never replayed again.
- **Disk write**: each session is encoded and muxed to its own MP4 file by a background
  worker thread through an internal `appsrc ! videoconvert ! encoder ! h264parse ! mp4mux !
  filesink` pipeline. This never blocks the main streaming thread. With the default
  `flush-interval-ms`, the clip is written incrementally while the event is being recorded.
  See [Incremental clip writing](#incremental-clip-writing).
- **Metadata**: forwarded frames get frame-level `GstAnalyticsClsMtd` entries describing the
  event state and, on start frames, the triggering class(es). See [Metadata](#metadata).

## Properties

| Property | Type | Range | Default | Description |
|---|---|---|---|---|
| `target-classes` | String | — | `person,vehicle` | Comma-separated detection labels that can trigger a recording. |
| `confidence-threshold` | Double | [0.0, 1.0] | 0.5 | Minimum detection confidence required to trigger. |
| `pre-event-frames` | Unsigned Integer | [0, 4294967295] | 30 | Number of frames preceding a trigger kept in the shared ring buffer. |
| `post-event-frames` | Unsigned Integer | [0, 4294967295] | 30 | Number of frames to keep capturing after the detected event ends. |
| `event-gap-frames` | Unsigned Integer | [0, 4294967295] | 3 | Consecutive frames without a matching detection tolerated before the event is considered ended. |
| `cooldown-value` | Unsigned Integer64 | [0, 18446744073709551615] | 150 | Per-class cooldown duration in frames. |
| `max-concurrent-events` | Unsigned Integer | [1, 4294967295] | 4 | Maximum number of recording sessions active at the same time. New triggers beyond this limit are dropped and throttle-logged. |
| `event-clip-output-dir` | String | — | `.` | Directory where full PRE/EVENT/POST clips are written. |
| `encoder-mode` | Enum | `software` / `vaapi` / `auto` | `software` | Video encoder backend used to write triggered clips. See [Encoder Modes](#encoder-modes). |
| `drop-log-interval-ms` | Unsigned Integer | [0, 4294967295] | 1500 | Minimum interval between aggregated "events dropped" log messages. |
| `flush-interval-ms` | Unsigned Integer | [0, 4294967295] | 3000 | Maximum stream time between partial writes of a clip that is still being recorded. `0` writes the whole clip when the event ends. Applies to sessions started after the change. See [Incremental clip writing](#incremental-clip-writing). |

All properties are `readable, writable` and can be changed at runtime (e.g. from a Python
application via `element.set_property(...)`), not just from the command line.

## Configuration

### Trigger rule and cooldown

Each target class has its own independent cooldown state. When a qualifying detection for a
class arrives and that class is not currently in cooldown, a new trigger fires: the class
enters cooldown starting from that frame, and cooldown is *not* restarted by any further
detections of the same class until `cooldown-value` frames have elapsed since the *first*
detection that started the window.

```
frame:       0        10       150       200
person:   trigger   (in cooldown, ignored)  trigger   (in cooldown, ignored)
```

With `cooldown-value=100`, a trigger at frame 0 suppresses further "person" triggers until
frame 100; a detection at frame 150 fires a new trigger and restarts the window from 150, so
a detection at frame 200 is suppressed again.

### Pre/event/post-event window

The frames written to disk for one session are, in order: exactly `pre-event-frames`
preceding frames, the triggering frame, all frames while the target class is detected, and
`post-event-frames` after the event ends. A matching detection resets the event-gap counter;
the event ends only after more than `event-gap-frames` consecutive frames lack a qualifying
detection. Those tolerated gap frames remain part of the clip, and POST starts on the first
frame beyond the gap allowance.

The element does not wait for the pre-event ring to fill. If a target is detected during
startup, it is recorded immediately with a partial (possibly empty) PRE section. Once the
ring is full, subsequent sessions include the complete configured PRE window.

The shared ring buffer keeps sliding throughout, so overlapping sessions for different
classes are supported and do not interfere with each other.

### Downstream event stream

The recorder starts forwarding as soon as a trigger is accepted, without waiting for the
event or POST to finish. It first replays the available PRE frames, then forwards the trigger
frame and subsequent EVENT and POST frames. It drops unrelated input frames after POST ends.
There is no one-frame forwarding delay.

A DISCONT flag marks the first forwarded buffer of a new window, including PRE, so
`gvaframeselect` resets its sub-stream sampling window. Original buffer timestamps and
durations are preserved. For clock-synchronized downstream sinks, account for the PRE delay
in the application; `sync=false` is suitable for offline consumers.

If recording sessions overlap, each disk clip still contains its own complete PRE/EVENT/POST
sequence. Downstream receives their chronological union, without duplicate buffers or
timestamp rewinds. PRE frames already sent for an earlier window are not sent again.
The recorder writes no separate EVENT-only files; `downstream-output-dir` has been removed.

```sh
... ! gvadetect ... ! gvaeventrecorder target-classes=person ! \
  gvaframeselect sub-stream-frames=900 frame-budget=8 ! \
  videoconvert ! video/x-raw,format=RGB ! \
  gvaroiscale out-width=448 out-height=448 ! gvagenai ...
```

### Clip filenames

Each clip is an MP4 file whose name contains the triggering class, the UTC session start time, and a six-digit
per-element counter. For example, the full clip of a `person` event is written to
`event-clip-output-dir` as:

```text
event_person_20261015T142301Z_000003.mp4
```

### Encoder modes

| Value | Behavior |
|---|---|
| `software` (default) | Uses a software encoder (`x264enc`). Keeps the GPU completely free, useful when the GPU is dedicated to a downstream VLM. |
| `vaapi` | Uses the hardware VA-API encoder (`vah264enc`), which runs on the GPU's fixed-function media engine rather than its compute/EU array. |
| `auto` | Uses `vaapi` if the `vah264enc` factory is available, falls back to `software` otherwise. |

## Metadata

Forwarded PRE/EVENT/POST buffers carry frame-level `GstAnalyticsClsMtd` entries in their
`GstAnalyticsRelationMeta`. They are not related to any `GstAnalyticsODMtd` and are identified by
their semantic tag:

| Semantic tag | Labels | Present on |
|---|---|---|
| `event` | `start`, `continue` or `stop` (confidence 1.0) | every forwarded PRE/EVENT/POST buffer |
| `event_trigger` | class(es) that started a recording session, with their confidence | frames that start one or more sessions |

`start` marks the first forwarded frame of a window (normally its oldest PRE frame),
`continue` the frames after it, and `stop` the final POST frame when no other session remains
active. Trigger metadata remains on the actual detection frame, not the first PRE frame.
If a window has only one frame, that frame gets `stop`. Buffers are sent immediately; if EOS
interrupts an active window, its already-forwarded last buffer cannot be relabeled `stop`.
The EOS event terminates downstream in that case, and the partial clip is saved on stop.

Place `gvametaconvert` after the recorder to serialize the entries to JSON; each frame then contains a
full-frame object such as:

```json
{
  "x": 0, "y": 0, "w": 1920, "h": 1080,
  "classification/event": { "label": "start", "confidence": 1.0 },
  "classification/event_trigger": { "label": "person", "confidence": 0.87 }
}
```

If one frame triggers several classes (e.g. `person` and `vehicle`), the `event_trigger` entry holds
all of them; `gvametaconvert` joins their labels and reports the highest confidence.

## Pipeline Examples

### Detection on NPU, event recording, VLM on GPU

```bash
gst-launch-1.0 -v \
    filesrc location=input.mp4 ! decodebin3 ! videoconvert ! \
    gvadetect model=d-fine-int8.xml device=NPU ! queue ! \
    gvaeventrecorder target-classes=person,vehicle confidence-threshold=0.6 \
             pre-event-frames=30 post-event-frames=30 event-gap-frames=3 \
                     cooldown-value=150 \
                     event-clip-output-dir=./events ! \
    gvagenai model-path=$mVLM device=GPU prompt="Describe what is happening." ! \
    gvametaconvert format=json ! gvametapublish file-format=2 file-path=results.json ! \
    fakesink
```

### Inspecting dropped vs. forwarded frames

```bash
gst-launch-1.0 -v \
    filesrc location=input.mp4 ! decodebin3 ! videoconvert ! \
    gvadetect model=$mDetect device=GPU ! queue ! \
    gvaeventrecorder target-classes=person max-concurrent-events=2 ! \
    fakesink
```

  For a runnable command-line example, see the [gvaeventrecorder sample](../../../samples/gstreamer/gst_launch/gvaeventrecorder/README.md).

## Logging

To write debug messages from `gvaeventrecorder` to a file, prefix your pipeline
command with:

```bash
GST_DEBUG=gvaeventrecorder:5 \
GST_DEBUG_FILE="/path/to/gvaeventrecorder.log" \
gst-launch-1.0 ...
```

Replace `/path/to/gvaeventrecorder.log` with the desired log file path and `...`
with your pipeline. The parent directory must already exist and be writable.

- `GST_DEBUG=gvaeventrecorder:5` enables messages up to DEBUG level for the
  `gvaeventrecorder` category.
- `GST_DEBUG_FILE` sends GStreamer debug messages to the specified file instead
  of standard error. It does not redirect regular `gst-launch-1.0` output.

## Notes

- **The pre-event ring is never reset by a trigger.** A trigger only takes a snapshot of it;
  the ring keeps accumulating frames so the element is ready for the next event. A trigger
  during startup uses the frames available so far; later triggers get all `pre-event-frames`.
- **GPU stays free by default.** `encoder-mode` defaults to `software` so clip encoding does
  not compete with a GPU-resident VLM; switch to `vaapi` deliberately if that trade-off is
  acceptable for your deployment.
- **Disk writes never block the streaming thread.** A single background worker opens,
  feeds, and finalizes the clip pipelines in order; the number of simultaneously recorded
  sessions is bounded by `max-concurrent-events`, not by one thread per session.
- **Overflow policy**: once `max-concurrent-events` is reached, new triggers are dropped
  (not queued) and aggregated into a single throttled `GST_WARNING` log line at most every
  `drop-log-interval-ms`, with a final flush when the element stops.
- **On EOS/stop**, sessions still in their post-event window are finalized and written with
  whatever frames they collected so far, rather than discarded.

## Element Details (gst-inspect-1.0)

```
Pad Templates:
  SINK template: 'sink'
    Availability: Always
    Capabilities:
      video/x-raw
                 format: { (string)BGRx, (string)BGRA, (string)BGR, (string)NV12, (string)I420 }
      video/x-raw(memory:DMABuf)
                 format: { (string)DMA_DRM }
      video/x-raw(memory:VASurface)
                 format: { (string)NV12 }
      video/x-raw(memory:VAMemory)
                 format: { (string)NV12 }

  SRC template: 'src'
    Availability: Always
    Capabilities:
      video/x-raw
                 format: { (string)BGRx, (string)BGRA, (string)BGR, (string)NV12, (string)I420 }
      video/x-raw(memory:DMABuf)
                 format: { (string)DMA_DRM }
      video/x-raw(memory:VASurface)
                 format: { (string)NV12 }
      video/x-raw(memory:VAMemory)
                 format: { (string)NV12 }

Element has no clocking capabilities.
Element has no URI handling capabilities.

Pads:
  SINK: 'sink'
    Pad Template: 'sink'
  SRC: 'src'
    Pad Template: 'src'

Element Properties:

  confidence-threshold: Minimum detection confidence required to trigger
                        flags: readable, writable
                        Double. Range:               0 -               1 Default:             0.5
  cooldown-value      : Per-class cooldown duration in frames, counted from the frame that started the window
                        flags: readable, writable
                        Unsigned Integer64. Range: 0 - 18446744073709551615 Default: 150
  drop-log-interval-ms: Minimum interval between aggregated 'events dropped' log messages
                        flags: readable, writable
                        Unsigned Integer. Range: 0 - 4294967295 Default: 1500
  event-gap-frames    : Consecutive frames without a matching detection tolerated before the event is considered ended
                        flags: readable, writable
                        Unsigned Integer. Range: 0 - 4294967295 Default: 3
  encoder-mode        : Video encoder backend used to write triggered clips
                        flags: readable, writable
                        Enum "GstGvaEventRecorderEncoderMode" Default: 0, "software"
                           (0): software          - Software encoder (x264), keeps the GPU free
                           (1): vaapi             - Hardware VA-API encoder (GPU media engine)
                           (2): auto              - VA-API if available, software fallback otherwise
  flush-interval-ms   : Write event clips incrementally as fragmented MP4: PRE frames are flushed to disk at the
                        trigger, then the clip is flushed at least every this many milliseconds of stream time
                        while recording. 0 = write the whole clip as a regular MP4 when the event ends.
                        Applies to sessions started after the change
                        flags: readable, writable
                        Unsigned Integer. Range: 0 - 4294967295 Default: 3000
  max-concurrent-events: Maximum number of recording sessions active at the same time; new triggers
                        beyond this limit are dropped (and throttle-logged)
                        flags: readable, writable
                        Unsigned Integer. Range: 1 - 4294967295 Default: 4
  event-clip-output-dir: Directory where full PRE/EVENT/POST clips are written
                        flags: readable, writable
                        String. Default: "."
  post-event-frames   : Number of frames to keep capturing after the detected event has ended
                        flags: readable, writable
                        Unsigned Integer. Range: 0 - 4294967295 Default: 30
  pre-event-frames    : Number of frames preceding a trigger to keep in the shared ring buffer
                        flags: readable, writable
                        Unsigned Integer. Range: 0 - 4294967295 Default: 30
  target-classes      : Comma-separated detection labels that trigger a recording (e.g. "person,vehicle")
                        flags: readable, writable
                        String. Default: "person,vehicle"
  name                : The name of the object
                        flags: readable, writable
                        String. Default: "gvaeventrecorder0"
  parent              : The parent of the object
                        flags: readable, writable
                        Object of type "GstObject"
  qos                 : Handle Quality-of-Service events
                        flags: readable, writable
                        Boolean. Default: false
```
