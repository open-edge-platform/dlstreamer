# FPS Throttle Samples

This directory contains sample pipelines demonstrating the use of the `gvafpsthrottle` element.

The `gvafpsthrottle` element throttles (limits) framerate by capping the maximum rate at which buffers pass through. Note: This element does not duplicate or drop frames to match the framerate. It cannot increase FPS, any slowdown in upstream processing cannot be recovered.

For detailed documentation, see [docs/source/elements/gvafpsthrottle.md](../../../../docs/source/elements/gvafpsthrottle.md).

## Features

- **Independent of Sink Sync**: Unlike using `sync=true` on a sink element, `gvafpsthrottle` controls framerate at any point in the pipeline, regardless of downstream sink settings.
- **One-directional**: Only throttles FPS. Cannot speed up slow upstream processing. Does not duplicate or drop frames to match the framerate.
- **Precise Timing**: Uses monotonic time and high precision timer to ensure accurate framerate limiting.
- **In-place Processing**: Operates in-place without modifying buffers for optimal performance.

## Use Cases

- **Rate Limiting for Slow Inference**: When inference takes longer than realtime, limit the input framerate to prevent buffer buildup.
- **Testing at Different Framerates**: Test pipeline behavior at various framerates without changing the input source.
- **Independent Pipeline Control**: Control framerate in the middle of a pipeline, regardless of source or sink settings.

## Usage

### Basic Pipeline

```bash
gst-launch-1.0 videotestsrc ! gvafpsthrottle target-fps=10 ! fakesink
```

### In Inference Pipeline

```bash
gst-launch-1.0 filesrc location=video.mp4 ! decodebin3 ! gvafpsthrottle target-fps=10 ! \
    gvadetect model=model.xml ! \
    gvawatermark ! videoconvert ! autovideosink sync=false
```

## Running the Samples

The `test_gvafpsthrottle.sh` script provides usage examples and instructions for testing the element.

```bash
./test_gvafpsthrottle.sh
```
