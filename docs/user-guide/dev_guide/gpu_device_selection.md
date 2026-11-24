# GPU device selection

This article describes how to select a GPU device on a multi-GPU system.

## 1. Inference (OpenVINO™ based) elements

### Explicit selection

In case of video decoding running on CPU and inference running on GPU, the
`device` property in inference elements enables you to select the GPU device
according to the
[OpenVINO™ GPU device naming convention](https://docs.openvino.ai/2026/openvino-workflow/running-inference/inference-devices-and-modes/gpu-device.html#device-naming-convention)
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

> **NOTE:** From [GStreamer 1.28](https://gstreamer.freedesktop.org/releases/1.28/) and [DL Streamer 2026.1.0] the VAAPI plugin support has being removed, the VA plugin must be used instead.

## 3. Windows (D3D11 and D3D12)

On Windows, D3D11 and D3D12 codec plugins register one decoder element per GPU
(Direct3D adapter), similar to the VA plugins on Linux:

- `d3d11h264dec`, `d3d12h264dec` for the default adapter
- `d3d11h264device1dec`, `d3d12h264device1dec` for the next adapter that supports
  the codec, and so on

Each decoder reports its adapter in the read-only `adapter-luid` property
(`gst-inspect-1.0 d3d12h264device1dec`). The element index does not always match
the OpenVINO™ GPU index, because OpenVINO™ lists integrated GPUs before discrete
GPUs. Compare `adapter-luid` with the OpenVINO™ `DEVICE_LUID` property of each GPU
to find the matching pair. `adapter-luid` is a decimal integer, while `DEVICE_LUID`
is 8 bytes (printed as hex) that equal `adapter-luid` when read as a little-endian
64-bit integer. The following script prints `DEVICE_LUID` in the `adapter-luid`
format:

```python
import openvino as ov

core = ov.Core()
for dev in core.available_devices:
    if dev.startswith("GPU"):
        luid = core.get_property(dev, "DEVICE_LUID")
        print(dev, int.from_bytes(bytes.fromhex(luid), "little", signed=True))
```

Inference elements match the upstream D3D11/D3D12 device to an OpenVINO™ GPU by
adapter LUID:

- With `device=GPU`, the upstream D3D11/D3D12 device is reused for inference.
- With `device=GPU.<N>`, the upstream device is reused only if its adapter LUID
  matches OpenVINO™ `GPU.<N>`. Otherwise the inference element creates a new
  D3D11/D3D12 device on the adapter of `GPU.<N>`.
- `share-d3d-ctx=false` always creates a new device for inference instead of
  reusing the upstream one.

For zero-copy operation on a *multi-GPU* system, decode on the same adapter as the
inference device. For example, if `d3d11h264device1dec`/`d3d12h264device1dec`
run on the adapter of OpenVINO™ `GPU.1`:

```bash
gst-launch-1.0 filesrc location=${VIDEO_FILE} ! parsebin ! d3d11h264device1dec ! \
gvadetect model=${MODEL_FILE} device=GPU.1 pre-process-backend=d3d11-surface-sharing ! queue ! gvafpscounter ! fakesink

gst-launch-1.0 filesrc location=${VIDEO_FILE} ! parsebin ! d3d12h264device1dec ! \
gvadetect model=${MODEL_FILE} device=GPU.1 pre-process-backend=d3d12-surface-sharing ! queue ! gvafpscounter ! fakesink
```
