# Performance Guide

## 1. Media and AI processing (single stream)

The Deep Learning Streamer Pipeline Framework combines media processing and AI inference
capabilities. The simplest pipeline detects objects in a video stream stored as a file.

For Intel platforms with integrated GPU and/or NPU devices, use the recommended command line:

```bash
gst-launch-1.0 filesrc location=${VIDEO_FILE} ! parsebin ! vah264dec ! "video/x-raw(memory:VAMemory)" ! gvadetect model=${MODEL_FILE} device=GPU pre-process-backend=va ! queue ! gvafpscounter ! fakesink
gst-launch-1.0 filesrc location=${VIDEO_FILE} ! parsebin ! vah264dec ! "video/x-raw(memory:VAMemory)" ! gvadetect model=${MODEL_FILE} device=NPU pre-process-backend=va ! queue ! gvafpscounter ! fakesink
```
- `vah264dec` uses the hardware video decoder to generate output images (VAMemory).
- `gvadetect` consumes VAMemory images (zero-copy operation) and generates
  inference results.
- `pre-process-backend=va` uses the hardware image scaler to resize the VAMemory image into
  input model tensor dimensions.

When using discrete GPUs, it is recommended to set `pre-process-backend=va-surface-sharing`
to enforce zero-copy operation between video decoder and AI inference engine. Note that 
`va-surface-sharing` may be slightly slower than the `va` backend when integrated GPU is used.

The `va-surface-sharing` option compiles the image scaling layer into the AI model, consuming
GPU compute resources:

```bash
gst-launch-1.0 filesrc location=${VIDEO_FILE} ! parsebin ! vah264dec ! "video/x-raw(memory:VAMemory)" ! gvadetect model=${MODEL_FILE} device=GPU pre-process-backend=va-surface-sharing ! queue ! gvafpscounter ! fakesink
```

While GPU is preferred for hardware-accelerated media decoding, CPU may also be used to decode
video streams. The following table lists command lines with recommended pipelines for various
combinations of media decode and AI inference devices.

| Media Decode device | Inference device             | Sample command line                  |
|---------------------|------------------------------|--------------------------------------|
| GPU                 | <br>GPU<br>or<br>NPU<br><br> | gst-launch-1.0 filesrc location=${VIDEO_EXAMPLE} ! parsebin ! vah264dec ! “video/x-raw(memory:VAMemory)” ! gvadetect model=${MODEL_FILE} device=GPU pre-process-backend=va ! queue ! gvafpscounter ! fakesink |
| GPU                 | CPU                          | gst-launch-1.0 filesrc location=${VIDEO_EXAMPLE} ! parsebin ! vah264dec ! “video/x-raw” ! gvadetect model=${MODEL_FILE} device=CPU pre-process-backend=opencv ! queue ! gvafpscounter ! fakesink              |
| CPU                 | <br>GPU<br>or<br>NPU<br><br> | gst-launch-1.0 filesrc location=${VIDEO_EXAMPLE} ! parsebin ! avdec_h264 ! “video/x-raw” ! gvadetect model=${MODEL_FILE} device=GPU pre-process-backend=opencv ! queue ! gvafpscounter ! fakesink             |
| CPU                 | CPU                          | gst-launch-1.0 filesrc location=${VIDEO_EXAMPLE} ! parsebin ! avdec_h264 ! “video/x-raw” ! gvadetect model=${MODEL_FILE} device=CPU pre-process-backend=opencv ! queue ! gvafpscounter ! fakesink             |


GStreamer supports several memory types, but the most common formats found in DL Streamer
pipelines are:
* *video/x-raw*, which typically resolves to *video/x-raw(memory:SystemMemory)* —
suitable for CPU processing.
* *video/x-raw(memory:VAMemory)*, which is optimized for GPU acceleration.

DL Streamer inference elements, such as `gvadetect`, `gvaclassify`, and `gvainference`,
can apply different preprocessing backends, including `ie` (Inference Engine), `opencv`, and
`va-surface-sharing`. You can set these explicitly, using the pre-process-backend option,
or allow DL Streamer to make the decision internally. If the pipeline is defined correctly,
GStreamer can negotiate the optimal memory type for a given device, allowing DL Streamer to
automatically set the optimal preprocessing backend.

For example:  
The `decodebin3` element recognizes the presence of a GPU in the system and attempts to
introduce the optimal VAMemory setting. This automatically results in using the efficient
`va-surface-sharing` backend in DL Streamer if the inference element device is set to GPU or
NPU.

However, if the pipeline is suboptimal (e.g., using `decodebin` instead of `decodebin3`),
DL Streamer will switch to a less efficient preprocessing backend (e.g., `opencv` for the GPU)
to ensure the pipeline functions. In such cases, you will get a warning and a suggestion for 
correcting the pipeline.

| Inference Device | Memory Type                   | Preprocessing Backend |
|------------------|-------------------------------|-----------------------|
| CPU              | only `video/x-raw` available  | `ie` or `opencv`      |
| GPU / NPU        | use `video/x-raw(memory:VAMemory)` for optimal performance | use `va-surface-sharing` to avoid memory copying |

### Windows (D3D11 and D3D12)

On Windows, hardware decoders output Direct3D memory instead of VAMemory, and
inference elements use the D3D pre-processing backends described in
[Model Preparation](./model_preparation.md#windows-pre-processing-backends).

On Windows 10 and later, `decodebin3` uses the D3D12 decoders (`d3d12h264dec`,
`d3d12h265dec`, ...). They output `video/x-raw(memory:D3D12Memory)` when every
element downstream accepts it, and otherwise copy the frame into
`video/x-raw(memory:D3D11Memory)`. If `pre-process-backend` is not set, inference
elements accept both memory types and select the matching backend. On GPU, they
select `d3d12-surface-sharing`, which keeps the frame on the GPU and avoids
reading the converted image back to system memory. On NPU they select `d3d12`:

```bash
gst-launch-1.0 filesrc location=${VIDEO_FILE} ! decodebin3 ! gvadetect model=${MODEL_FILE} device=GPU ! queue ! gvafpscounter ! fakesink
gst-launch-1.0 filesrc location=${VIDEO_FILE} ! decodebin3 ! gvadetect model=${MODEL_FILE} device=NPU ! queue ! gvafpscounter ! fakesink
```

A `d3d11` backend accepts only D3D11Memory, so the D3D12 decoder copies each frame
into a D3D11 texture. Use it when an element downstream supports only D3D11Memory,
or with the `d3d11h264dec`/`d3d11h265dec` decoders:

```bash
gst-launch-1.0 filesrc location=${VIDEO_FILE} ! parsebin ! d3d11h264dec ! gvadetect model=${MODEL_FILE} device=GPU pre-process-backend=d3d11-surface-sharing ! queue ! gvafpscounter ! fakesink
```

| Inference Device | Memory Type                              | Preprocessing Backend                     |
|------------------|------------------------------------------|-------------------------------------------|
| CPU              | `video/x-raw`                            | `ie` or `opencv`                          |
| GPU              | `video/x-raw(memory:D3D12Memory)`        | `d3d12-surface-sharing` (or `d3d12`)      |
| GPU              | `video/x-raw(memory:D3D11Memory)`        | `d3d11-surface-sharing` (or `d3d11`)      |
| NPU              | `video/x-raw(memory:D3D12Memory)` or `video/x-raw(memory:D3D11Memory)` | `d3d12` or `d3d11` |

For single-stage pipelines on GPU, the surface sharing backends are the fastest
option, since no converted image is read back to system memory. In multi-stage
pipelines such as `gvadetect ! gvaclassify`, the GPU is typically fully occupied by
inference, so all D3D backends reach similar throughput. In that case, batch the
per-object stage with `batch-size` to reduce the per-inference overhead. Batching
benefits the surface sharing backends the most:

```bash
gst-launch-1.0 filesrc location=${VIDEO_FILE} ! decodebin3 ! \
gvadetect model=${MODEL_FILE_1} device=GPU ! queue ! \
gvaclassify model=${MODEL_FILE_2} device=GPU batch-size=8 ! queue ! gvafpscounter ! fakesink
```

By default, the inference element reuses the upstream D3D11/D3D12 device when
it is on the same GPU as the inference `device`. Set `share-d3d-ctx=false` to
create a separate device for inference.



## 2. Multi-stage pipeline with gvadetect and gvaclassify

The rules outlined above can be combined to create multi-stage
pipelines. For example, the first two inference stages can use GPU and
NPU devices with the VA backend. The third element may use CPU device, after
the video stream is copied from the device memory (VAMemory) to the system
memory.

```bash
gst-launch-1.0 filesrc location=${VIDEO_FILE} ! parsebin ! vah264dec ! "video/x-raw(memory:VAMemory)" ! \
gvadetect model=${MODEL_FILE_1} device=GPU pre-process-backend=va ! queue ! \
gvaclassify model=${MODEL_FILE_2} device=NPU pre-process-backend=va ! queue ! \
vapostproc ! video/x-raw ! \
gvaclassify model=${MODEL_FILE_3} device=CPU pre-process-backend=opencv ! queue ! \
gvafpscounter ! fakesink
```

Static allocation of AI stages to inference devices may be suboptimal if
one model is much bigger than others. In such cases, it is recommended to
use `virtual` aggregated devices and let OpenVINO™ inference
engine to select devices dynamically. The pre-processing backend should
be selected to handle all possible combinations.

```bash
gst-launch-1.0 filesrc location=${VIDEO_FILE} ! parsebin ! vah264dec ! "video/x-raw(memory:VAMemory)" ! \
gvadetect model=${MODEL_FILE_1} device=MULTI:GPU,NPU,CPU pre-process-backend=va ! queue ! \
gvaclassify model=${MODEL_FILE_2} device=MULTI:GPU,NPU,CPU pre-process-backend=va ! queue ! \
gvaclassify model=${MODEL_FILE_3} device=MULTI:GPU,NPU,CPU pre-process-backend=va ! queue ! \
gvafpscounter ! fakesink
```

## 3. Multi-stream pipelines with single AI stage

The GStreamer framework can execute multiple input streams in parallel.
If streams use the same pipeline configuration, it is recommended to
create a shared inference element. The `model-instance-id=inf0`
parameter constructs such element. In addition, the `batch-size`
element should be set to the integer multiply of the stream count. This
approach batches images from different streams to maximize throughput
and at the same time to reduce latency penalty due to batching.

```bash
gst-launch-1.0 filesrc location=${VIDEO_FILE_1} ! parsebin ! vah264dec ! "video/x-raw(memory:VAMemory)" ! \
gvadetect model=${MODEL_FILE} device=GPU pre-process-backend=va model-instance-id=inf0 batch-size=4 ! queue ! gvafpscounter ! fakesink \
filesrc location=${VIDEO_FILE_2} ! parsebin ! vah264dec ! "video/x-raw(memory:VAMemory)" ! \
gvadetect model=${MODEL_FILE} device=GPU pre-process-backend=va model-instance-id=inf0 batch-size=4 ! queue ! gvafpscounter ! fakesink \
filesrc location=${VIDEO_FILE_3} ! parsebin ! vah264dec ! "video/x-raw(memory:VAMemory)" ! \
gvadetect model=${MODEL_FILE} device=GPU pre-process-backend=va model-instance-id=inf0 batch-size=4 ! queue ! gvafpscounter ! fakesink \
filesrc location=${VIDEO_FILE_4} ! parsebin ! vah264dec ! "video/x-raw(memory:VAMemory)" ! \
gvadetect model=${MODEL_FILE} device=GPU pre-process-backend=va model-instance-id=inf0 batch-size=4 ! queue ! gvafpscounter ! fakesink
```

Similarly to multi-stage scenarios, an aggregated inference device
can be used with `device=MULTI:GPU,NPU,CPU`.

Note that a single Deep Learning Streamer command line with multiple input
streams yields higher performance than running multiple DL Streamer
command lines per each processing of a single single stream. The reason is
multiple command lines cannot benefit from sharing one AI model instance
and cross-stream batching.

## 4. Multi-stream pipelines with multiple AI stages

The multi-stage and multi-stream scenarios can be combined to form
complex execution graphs. In the following example, four input streams
are processed by `gvadetect` and `gvaclassify`. Note that the pipeline creates
only two instances of inference models:

- `inf1` with a detection model running on GPU
- `inf2` with a classification model running on NPU

```bash
gst-launch-1.0 filesrc location=${VIDEO_FILE_1} ! parsebin ! vah264dec ! "video/x-raw(memory:VAMemory)" ! \
gvadetect model=${MODEL_FILE_1} device=GPU pre-process-backend=va model-instance-id=inf1 batch-size=4 ! queue ! \
gvaclassify model=${MODEL_FILE_2} device=NPU pre-process-backend=va model-instance-id=inf2 batch-size=4 ! queue ! gvafpscounter ! fakesink \
filesrc location=${VIDEO_FILE_2} ! parsebin ! vah264dec ! "video/x-raw(memory:VAMemory)" ! \
gvadetect model=${MODEL_FILE_1} device=GPU pre-process-backend=va model-instance-id=inf1 batch-size=4 ! queue ! \
gvaclassify model=${MODEL_FILE_2} device=NPU pre-process-backend=va model-instance-id=inf2 batch-size=4 ! queue ! gvafpscounter ! fakesink \
filesrc location=${VIDEO_FILE_3} ! parsebin ! vah264dec ! "video/x-raw(memory:VAMemory)" ! \
gvadetect model=${MODEL_FILE_1} device=GPU pre-process-backend=va model-instance-id=inf1 batch-size=4 ! queue ! \
gvaclassify model=${MODEL_FILE_2} device=NPU pre-process-backend=va model-instance-id=inf2 batch-size=4 ! queue ! gvafpscounter ! fakesink \
filesrc location=${VIDEO_FILE_4} ! parsebin ! vah264dec ! "video/x-raw(memory:VAMemory)" ! \
gvadetect model=${MODEL_FILE_1} device=GPU pre-process-backend=va model-instance-id=inf1 batch-size=4 ! queue ! \
gvaclassify model=${MODEL_FILE_2} device=NPU pre-process-backend=va model-instance-id=inf2 batch-size=4 ! queue ! gvafpscounter ! fakesink
```

## 5. Multi-stream pipelines with meta-aggregation element

The multi-stage and multi-stream scenarios can use the 
[gvametaaggregate](../elements/gvametaaggregate.md) element to aggregate the results from
multiple branches of the pipeline. The aggregated results are published as a single JSON 
metadata output.

The following example shows how to use the `gvametaaggregate` element to
aggregate the results from two stream pipelines:

```bash
gst-launch-1.0 filesrc location=${VIDEO_FILE_1} ! decodebin3 ! videoconvert ! \
  tee name=t t. ! queue ! gvametaaggregate name=a !
  gvaclassify model=${MODEL_FILE_2} device=CPU ! queue ! \
  gvametaconvert format=json add-tensor-data=true ! gvametapublish file-path=./result.json method=file file-format=json-lines ! \
  fakesink sync=false t. ! queue ! \
  gvadetect model=${MODEL_FILE_1} device=GPU ! a. \
  filesrc location=${VIDEO_FILE_1} ! decodebin3 ! videoconvert ! \
  gvadetect model=${MODEL_FILE_1} device=GPU ! a.
```

## 6. Heterogeneous systems

For more information on configuring and optimizing pipelines for heterogeneous systems with multiple devices, see the [Heterogeneous Systems Guide](heterogeneous_systems_guide.md).

## 7. The Deep Learning Streamer Pipeline Framework performance benchmark results

The Deep Learning Streamer Pipeline Framework example performance benchmark
results can be found as a part of the
[Smart Cities Accelerated by Intel® Graphics Solutions paper](https://www.intel.com/content/www/us/en/secure/content-details/826398/smart-cities-accelerated-by-intel-gpus-arc-gpu-addendum.html?wapkw=smart%20cities&DocID=826398).

## 8. Displaying analysis results with gvawatermark

When video analysis results are supposed to be visually inspected, the `gvawatermark` element must be added to the pipeline. It should be positioned after inference elements (such as `gvadetect`, `gvaclassify`, or `gvainference`) and before the pipeline's sink that displays the results live or saves them to a video file. 

The `device` property of `gvawatermark` allows for executing the gvawatermark code that renders ROIs, labels, keypoints, etc. either on the CPU or on the GPU. Intuitively, the GPU might look like a better option regardless of the configuration of the other parts of the pipeline. However, the following important factors must be taken into account:
1. When using an integrated GPU whose resources are already 100% consumed by the inference process, adding watermark generation to the GPU's workload will lower the pipeline performance. In this scenario, we recommend using the default `CPU` setting, since the cost of data transmission will usually be negligible, and no GPU resources will have to be withdrawn from the inference process.
2. On the other hand, when a powerful PCI-based discrete GPU is used, computing resources on the GPU are often available, and video frame transfers from the GPU to the system can become the main bottleneck, eliminating the benefits of the additional hardware. In these situations, setting `device=GPU` is highly recommended in order to keep the video frames on the GPU end to end.
3. Remember to match memory types across the pipeline: for `device=GPU`, keep `"video/x-raw(memory:VAMemory)"` and use `pre-process-backend=va-surface-sharing` to avoid hidden copies.

Examples:

- CPU watermark when the integrated GPU is fully utilized by inference:

  ```bash
  gst-launch-1.0 filesrc location=${VIDEO_FILE} ! decodebin3 ! "video/x-raw(memory:VAMemory)" ! \
    gvadetect model=${MODEL_FILE} device=GPU pre-process-backend=va-surface-sharing ! queue ! \
    gvawatermark device=CPU ! vapostproc ! autovideosink
  ```

- End-to-end GPU watermark on a discrete PCIe GPU (avoid frame round-trips to system memory):

  ```bash
  gst-launch-1.0 filesrc location=${VIDEO_FILE} ! qtdemux ! varenderD129h264dec ! varenderD129postproc ! \
    "video/x-raw(memory:VAMemory)" ! gvadetect model=${MODEL_FILE} device=GPU.1 pre-process-backend=va-surface-sharing ! \
    queue ! gvawatermark device=GPU ! videoconvert ! x264enc ! mp4mux ! filesink location=${OUTPUT_FILE}
  ```



On Windows, `gvawatermark` defaults to the GPU on D3D11Memory and D3D12Memory. The
GPU renderer draws into the frame's texture without copying it to system memory,
whereas the CPU renderer has to download every frame it draws on and upload it
again, so keep the default there even next to GPU inference. Keep the pipeline in
D3D12Memory where possible (the default with `decodebin3` when every element
accepts it): the watermark and the `d3d12-surface-sharing` pre-processing are both
faster than their D3D11 counterparts.

```bash
gst-launch-1.0 filesrc location=${VIDEO_FILE} ! decodebin3 ! gvadetect model=${MODEL_FILE} device=GPU ! queue ! \
  gvawatermark ! d3d12videosink
```
