# GPU device selection

This article describes how to select a GPU device on a multi-GPU system.

## 1. Inference (OpenVINO™ based) elements

### Explicit selection

In case of video decoding running on CPU and inference running on GPU, the
`device` property in inference elements enables you to select the GPU device
according to the
[OpenVINO™ GPU device naming convention](https://docs.openvino.ai/2024/openvino-workflow/running-inference/inference-devices-and-modes/gpu-device.html#device-naming-convention)
, with devices enumerated as **GPU.0**, **GPU.1**, etc., for example:

```bash
gst-launch-1.0 "... ! decodebin3 ! gvadetect device=GPU.1 ! ..."
```

## 2. Media and Inference elements

GStreamer allows selecting the GPU render device for VA codec plugins when
more than one GPU is available in the system.

On *single-GPU* systems, VA codec plugin elements such as `vah264dec` and
`vapostproc` map to the `/dev/dri/renderD128` GPU device (GPU.0).

On *multi-GPU* systems, each additional GPU corresponds to a separate DRI
device, for example:

- `/dev/dri/renderD129` for GPU.1
- `/dev/dri/renderD130` for GPU.2

Sample list of available VA codec plugins on a *multi-GPU* system:

```bash
gst-inspect-1.0 | grep va
. . .
va:  vah264dec: VA-API H.264 Decoder in Intel(R) Gen Graphics
va:  vapostproc: VA-API Video Postprocessor in Intel(R) Gen Graphics
. . .
va:  varenderD129h264dec: VA-API H.264 Decoder in Intel(R) Gen Graphics in renderD129
va:  varenderD129postproc: VA-API Video Postprocessor in Intel(R) Gen Graphics in renderD129
. . .
va:  varenderD130h265dec: VA-API H.265 Decoder in Intel(R) Gen Graphics in renderD130
va:  varenderD130postproc: VA-API Video Postprocessor in Intel(R) Gen Graphics in renderD130
```

Example of selecting the corresponding VA codec elements (`vah264dec`,
`vapostproc`) on a *single-GPU* system:

```bash
gst-launch-1.0 filesrc location=${VIDEO_FILE} ! parsebin ! vah264dec ! vapostproc ! "video/x-raw(memory:VAMemory)" ! \
gvadetect model=${MODEL_FILE} device=GPU.0 pre-process-backend=va-surface-sharing batch_size=8 ! queue ! gvafpscounter ! fakesink
```

For GPU devices other than the default one (GPU or GPU.0), it is highly
recommended to select the matching VA codec elements.

- `varenderD129h264dec`, `varenderD129postproc` for GPU.1
- `varenderD130h264dec`, `varenderD130postproc` for GPU.2, and so on

Example of selecting VA codec elements (`varenderD129h264dec`,
`varenderD129postproc`) on a *multi-GPU* system for GPU.1:

```bash
gst-launch-1.0 filesrc location=${VIDEO_FILE} ! parsebin ! varenderD129h264dec ! varenderD129postproc ! "video/x-raw(memory:VAMemory)" ! \
gvadetect model=${MODEL_FILE} device=GPU.1 pre-process-backend=va-surface-sharing batch_size=8 ! queue ! gvafpscounter ! fakesink
```

> **NOTE:** Starting with [GStreamer 1.24.12](https://gstreamer.freedesktop.org/releases/1.24/) and [DLS 2025.0.1.2](https://github.com/open-edge-platform/dlstreamer/releases/tag/v2025.0.1.2), using `decodebin3` instead of `decodebin` is strongly recommended for the primary GPU (GPU.0).
> `decodebin3` simplifies the pipeline layout.
> The sequence `parsebin ! vah264dec ! vapostproc ! "video/x-raw(memory:VAMemory)"` can be replaced with a single `decodebin3` element.

```bash
gst-launch-1.0 filesrc location=${VIDEO_FILE} ! decodebin3 ! \
gvadetect model=${MODEL_FILE} device=GPU pre-process-backend=va-surface-sharing ! queue ! gvafpscounter ! fakesink
```

> **NOTE:** From [GStreamer 1.24](https://gstreamer.freedesktop.org/releases/1.24/) and [DLS 2024.1.0](https://github.com/open-edge-platform/dlstreamer/releases/tag/v2024.1.0), the VAAPI plugin is deprecated. The `GST_VAAPI_ALL_DRIVERS` environment variable is deprecated in favor of `GST_VA_ALL_DRIVERS`.
> If you need to use the legacy VAAPI plugin (which will be removed in [GStreamer 1.28](https://gstreamer.freedesktop.org/releases/1.28/)), VAAPI elements should be used directly (e.g., `vaapih264dec` instead of `decodebin`).
> Use the [GST_VAAPI_DRM_DEVICE environment variable](https://people.freedesktop.org/~tsaunier/documentation/vaapi/index.html?gi-language=c#environment-variables) to set the decoding device.
