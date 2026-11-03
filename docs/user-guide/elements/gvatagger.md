# gvatagger

Reads only H.264/H.265 NAL unit sizes (no entropy decoding, no pixel access) and tags every access unit with `GvaTriggerMeta` (`motion`, `keyframe`) based on an exponential moving average (EMA) of quiet-frame size. Nothing is dropped or delayed: the whole stream passes through unchanged, and the meta survives `GstVideoDecoder`-based decoders so a downstream element can skip inference on quiet frames.

```bash
Pad Templates:
  SINK template: 'sink'
    Availability: Always
    Capabilities:
      video/x-h264
                stream-format: { (string)byte-stream, (string)avc } (GstValueList)
                    alignment: au (gchararray)
      video/x-h265
                stream-format: { (string)byte-stream, (string)hvc1, (string)hev1 } (GstValueList)
                    alignment: au (gchararray)

  SRC template: 'src'
    Availability: Always
    Capabilities:
      video/x-h264
                stream-format: { (string)byte-stream, (string)avc } (GstValueList)
                    alignment: au (gchararray)
      video/x-h265
                stream-format: { (string)byte-stream, (string)hvc1, (string)hev1 } (GstValueList)
                    alignment: au (gchararray)

Element Flags:

Element has no clocking capabilities.
Element has no URI handling capabilities.

Pads:
  SINK: 'sink'
    Pad Template: 'sink'
  SRC: 'src'
    Pad Template: 'src'

Element Properties:
  alpha                : Baseline update rate, applied only to quiet, non-IDR access units.
                        flags: readable, writable
                        Double. Range: 0 - 1 Default: 0.03
  k                    : Access unit tagged as motion when size > k * baseline.
                        flags: readable, writable
                        Double. Range: 1 - 1.797693e+308 Default: 1.5
  margin               : Minimum absolute byte margin added on top of k * baseline.
                        flags: readable, writable
                        Unsigned Integer64. Range: 0 - 18446744073709551615 Default: 8
  min-baseline         : Floor for the EMA baseline, in bytes, so it can never collapse to ~0.
                        flags: readable, writable
                        Unsigned Integer64. Range: 1 - 18446744073709551615 Default: 10
  name                 : The name of the object.
                        flags: readable, writable
                        String. Default: "gvatagger0"
  parent               : The parent of the object.
                        flags: readable, writable
                        Object of type "GstObject"
  qos                  : Handle Quality-of-Service events.
                        flags: readable, writable
                        Boolean. Default: false
```

## GvaTriggerMeta

`gvatagger` attaches a `GstCustomMeta` named `GvaTriggerMeta` to every access unit, with two boolean fields:

| Field      | Type    | Meaning                                                              |
|------------|---------|-----------------------------------------------------------------------|
| `motion`   | boolean | access unit's coded (VCL) size exceeds `max(k * baseline, baseline + margin)` |
| `keyframe` | boolean | access unit is an IDR                                                  |

During the first 60 non-IDR access units, the EMA baseline has not been established yet, so every access unit (including IDRs) is fail-safe tagged `motion=true`. In steady state, IDRs are always tagged `motion=false` (keyframes are large regardless of content) and the baseline is only updated from quiet, non-IDR access units, so motion never "trains" the baseline.

Because the meta is attached as a `GstCustomMeta`, it survives `GstVideoDecoder`-based decoders (`avdec_h264`, `openh264dec`, `vah264dec`, `vah265dec`, `decodebin3`, …), so it can be read after decoding, for example to skip inference on frames that were not tagged as motion.

## Example

Tag a stream and read `GvaTriggerMeta` after decoding, for example to skip inference on quiet frames:

```bash
gst-launch-1.0 filesrc location=input.264 ! h264parse ! gvatagger k=1.5 ! decodebin3 ! autovideosink
```

See the
[`gvatagger_motion`](https://github.com/open-edge-platform/dlstreamer/tree/main/samples/gstreamer/python/gvatagger_motion)
sample for a full walkthrough that prints a motion summary.
