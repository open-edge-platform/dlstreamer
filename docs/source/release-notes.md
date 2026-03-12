# Release Notes: Deep Learning Streamer Pipeline Framework

## Version 2026.0

**April 01, 2026**

**New**

- A sample presenting
  [DL Streamer and DeepStream coexistence](./dev_guide/concurrent.md).
- Basic support for [ONVIF camera discovery](https://github.com/open-edge-platform/dlstreamer/blob/main/samples/gstreamer/python/onvif_cameras_discovery/README.md).
- Support for [radar data processing](https://github.com/open-edge-platform/dlstreamer/blob/main/samples/gstreamer/gst_launch/g3dradarprocess/README.md).
- Rate/flow control on video stream processing.

**Improved**

- Support for cross-stream batching in the pipeline optimizer.
- Customization options in Gvawatermark.
- Fine control in batching parameters (including cross-stream batching) such as auto-batch timeout.

**Fixed**

- Stability and performance fixes.

## Legal Information

- FFmpeg is an open source project licensed under LGPL and GPL. \
  See <https://www.ffmpeg.org/legal.html>. \
  You are solely responsible for determining if your use of FFmpeg requires any additional licenses. \
  Intel is not responsible for obtaining any such licenses, nor liable for any licensing fees due, in connection with your use of FFmpeg.

- GStreamer is an open source framework licensed under LGPL. \
  See <https://gstreamer.freedesktop.org/documentation/frequently-asked-questions/licensing.html>. \
  You are solely responsible for determining if your use of GStreamer requires any additional licenses. \
  Intel is not responsible for obtaining any such licenses, nor liable for any licensing fees due, in connection with your use of GStreamer.

<!--hide_directive
```{toctree}
:hidden:

release-notes/release-notes-2025.md
release-notes/release-notes-2024.md
```
hide_directive-->
