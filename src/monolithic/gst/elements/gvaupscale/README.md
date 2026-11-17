# gvaupscale — Neural image upscaling element

`gvaupscale` is a DL Streamer GStreamer element that performs neural
super-resolution on video frames using an OpenVINO™ toolkit model. Unlike the
`gva*` analytics elements, it does **not** attach metadata — it runs inference
and emits **new, larger video frames** (e.g. 2×), so it is a size-changing
`GstVideoFilter` (its src caps differ from its sink caps).

Preprocessing (resize, color order, normalization) and inference are delegated
to the same `InferenceBackend::ImageInference` engine used by `gvainference`, so
the element reuses the existing, optimized/parametrized pipeline instead of
reimplementing it. Only the final step — writing the network output tensor into
a new, larger frame — is specific to this element.

## Properties

| Property | Type   | Default | Description |
|----------|--------|---------|-------------|
| `model`  | string | —       | Path to the OpenVINO™ toolkit super-resolution model (`.xml`). Required. |
| `device` | string | `CPU`   | Inference device (`CPU`, `GPU`, `NPU`, `AUTO`, …). |
| `scale`  | double | `2.0`   | Output/input resolution ratio produced by the model (drives caps negotiation). |

## Model contract

- Input: image tensor `[1, 3, H, W]`, float32. Color order and input
  normalization are read from the model's `model_info` `rt_info`
  (`reverse_input_channels`, `scale_values`/`mean_values`), following the
  OpenVINO Model API convention. Fully convolutional (dynamic `H`/`W`) models are
  supported — the input is statically reshaped to the frame size.
- Output: `[1, 3, scale·H, scale·W]`, float32. This experimental version assumes
  Real-ESRGAN-style output (values in `[0, 1]`); the frame is produced by
  `clip(x, 0, 1) · 255` → 8-bit.

## Pads

Both pads are `video/x-raw, format=BGR` (system memory), consistent with the
other DL Streamer inference elements. Insert `videoconvert` as needed.

## Example

```bash
gst-launch-1.0 filesrc location=input.mp4 ! decodebin ! videoconvert ! \
    video/x-raw,format=BGR ! \
    gvaupscale model=/path/to/real_esrgan_x2.xml device=CPU scale=2 ! \
    videoconvert ! x264enc ! mp4mux ! filesink location=upscaled.mp4
```

## Notes / current limitations (experimental)

- One synchronous inference per frame (no async `nireq` pipelining yet).
- System-memory preprocessing only (no VA-API surface sharing / zero-copy yet).
- Tiled inference for very large frames is not yet implemented.
