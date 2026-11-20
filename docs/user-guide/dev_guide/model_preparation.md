# Model Preparation

When getting started with Deep Learning Streamer, the recommended way to obtain
models ready for use in video analytics pipelines is to use the per-source
conversion scripts under
[scripts/download_models](https://github.com/open-edge-platform/dlstreamer/tree/main/scripts/download_models)
(`download_hf_models.py` for Hugging Face, `download_ultralytics_models.py` for
Ultralytics YOLO, `download_timm_models.py` for TIMM, and
`download_other_models.py` for a handful of other helper models). See
[Download and Convert Models](./download_public_models.md) and the script's own
[README](https://github.com/open-edge-platform/dlstreamer/blob/main/scripts/download_models/README.md)
for details.

This way, you will be able to easily perform the most popular tasks,
such as object detection and classification, instance segmentation, face
localization and many others. For examples of how to set up Deep
Learning Streamer pipelines that carry out these functions, refer to the
[sample directory](https://github.com/open-edge-platform/dlstreamer/tree/main/samples/gstreamer/gst_launch).

If you are interested in designing custom pipelines, make sure to review the
[Supported Models](../supported_models.md) table for
guidance on whether Deep Learning Streamer elements require specific
configurations (defined by the model-proc or labels files) for your
selected model.

## 1. Model file format used by OpenVINO™ Toolkit

Video Analytics GStreamer plugins utilize
[Intel® Distribution of OpenVINO™ Toolkit](https://www.intel.com/content/www/us/en/developer/tools/openvino-toolkit/overview.html)
as a back-end for high efficiency-inference on Intel® CPU and
accelerators (GPU, NPU, FPGA) and require the model to be converted from
the training framework format (e.g., TensorFlow or Caffe) into a format
optimized for inference on the target device. The model format used by
OpenVINO™ Toolkit is called Intermediate Representation (IR) and
consists of two files:

- xml - a file with the description of network topology
- bin: - a binary file (potentially big) with model weights

You can either:

1. Choose a pre-trained model that is already available in the OpenVINO IR
  format.
2. Use
   [OpenVINO™ Toolkit Model Conversion](https://docs.openvino.ai/2026/openvino-workflow/model-preparation/convert-model-to-ir.html)
   method for converting your model from the training framework format
   (e.g., TensorFlow) to the IR format.

When converting a custom model, you can optionally utilize the
[Post-Training Model Optimization and Compression](https://docs.openvino.ai/2026/openvino-workflow/model-optimization.html)
for converting the model into a performance efficient, and more
hardware-friendly representation. For example you can quantize it from 32-bit
floating point-precision into 8-bit integer precision. This gives a
significant performance boost (up to 4x) on some hardware platforms
including the CPU, with only a minor accuracy drop.

## 2. Model pre- and post-processing

Deep Learning Streamer plugins are capable
of optionally performing certain pre- and post-processing operations
before/after inference.

Pre- and post-processing can be configured using:

- The `model-proc` file (mostly applicable to models from Open Model
  Zoo). Its format and all possible pre- and post-processing
  configuration parameters are described in the
  [model-proc description](./model_proc_file.md) page.
- The "model_info" section inside the .xml file of OpenVINO™ Intermediate Representation
  with network topology, is described on the
  [model_info description](./model_info_xml.md) page.

Both methods yield the same results, but the "model_info" is recommended as
it simplifies dynamic deployments (like re-training models with Intel®
Geti™) as certain model pre- and post-processing parameters are tightly
coupled with model training results.

**Pre-processing** is an input data transformation into an appropriate
form which a neural network expects it to be. Most pre-processing
actions are designed for images. Pipeline Framework provides several
pre-processing back-ends depending on your use case.

**To use one of them, set the pre-process-backend property of the
inference element to one from the table below.**

**Default behavior**: If the property is not set, Pipeline Framework
will pick `ie` if the system memory is used in the pipeline, `va` - for GPU
memory (VAMemory and DMABuf). On Windows, `d3d12-surface-sharing` is picked for
D3D12Memory and `d3d11-surface-sharing` for D3D11Memory when the inference device
is a GPU; `d3d12` and `d3d11` are picked for NPU, AUTO and MULTI devices, or when
surface sharing is not supported with the current settings (see
[Windows pre-processing backends](#windows-pre-processing-backends)).

| pre-process-backend | Description | Memory type in pipeline | Configurable with model-proc? |
|---|---|---|---|
| ie | Short for “Inference Engine”. It resizes an image with a bilinear algorithm and sets color format which is deduced from current media. All that’s done with capabilities provided by Inference Engine from OpenVINO™ Toolkit. | System | No |
| opencv | All power of OpenCV is leveraged for input image pre-processing. Provides a wide variety of operations that can be performed on image. | System | Yes |
| <br>va<br><br> | Should be used in pipelines with GPU memory. Performs mapping to the system memory and uses VA pre-processor. | <br>VAMemory<br>and<br>DMABuf<br><br> | Yes |
| <br>va-surface-sharing<br><br> | Should be used in pipelines with GPU memory and GPU inference device. Doesn’t perform mapping to the system memory. As a pre-processor, it performs image resize, crop, and sets color format to NV12. | <br>VAMemory<br><br> | Partially |
| <br>opencl<br><br> | One OpenCL kernel on the GPU converts the color, resizes, crops and pads the frame or ROI, and writes the model input directly. Works with any inference device. Used only when selected, see [OpenCL pre-processing backend](#opencl-pre-processing-backend). | <br>VAMemory,<br>DMABuf<br>and System<br><br> | Yes |

### Windows pre-processing backends

On Windows, the VA backends are replaced by Direct3D backends. `ie` and
`opencv` are available for system memory, as on Linux.

| pre-process-backend | Description | Memory type in pipeline |
|---|---|---|
| d3d11 | Resizes, crops and converts the color format on the GPU with the GStreamer D3D11 converter, then maps the result to system memory for inference. Works with GPU and NPU inference devices. | D3D11Memory |
| d3d11-surface-sharing | Converts the frame on the GPU into a BGRA/RGBA texture at model input size and passes it to OpenVINO™ through OpenCL/D3D11 interop. The frame is not copied to system memory. GPU inference device only. | D3D11Memory |
| d3d12 | Same as `d3d11`, using the GStreamer D3D12 converter. Works with GPU and NPU inference devices. | D3D12Memory |
| d3d12-surface-sharing | Converts the frame on the GPU into a BGRA/RGBA texture at model input size and copies it into a shared D3D12 buffer. OpenCL imports the buffer through `cl_khr_external_memory`, so the frame is not copied to system memory. GPU inference device only. | D3D12Memory |
| opencl | One OpenCL kernel on the GPU converts the color, resizes, crops and pads the frame or ROI, and writes the model input directly; D3D11 frames are copied into a shared texture first, once per frame. Works with any inference device. See [OpenCL pre-processing backend](#opencl-pre-processing-backend). | D3D11Memory and System |

Notes:

- None of the D3D backends support custom pre-processing libraries
  (`custom-preproc-lib`).
- If a surface sharing backend is not supported with the current settings
  (for example, `device=NPU`), the element falls back to `d3d11` or `d3d12`
  and logs a warning.
- Surface sharing backends hand OpenVINO™ a BGRA or RGBA tensor that
  matches the channel order in the model's `rt_info`. If the model specifies
  no channel order, BGR is assumed.
- If `pre-process-backend` is not set, inference elements accept both
  D3D11Memory and D3D12Memory. D3D12 decoders (used by `decodebin3` on
  Windows 10 and later) then output D3D12Memory. A `d3d11` backend accepts only
  D3D11Memory, and a `d3d12` backend accepts only D3D12Memory.

### OpenCL pre-processing backend

With `pre-process-backend=opencl`, `gvadetect`, `gvaclassify` and `gvainference`
pre-process frames with one OpenCL kernel on the GPU: color conversion, resize,
crop and padding in one pass, written straight into the model input (u8, planar,
in the model's channel order). OpenVINO™ pre-processing only converts the
precision and applies the mean and scale values.

- **Frames**: VAMemory and DMABuf (NV12) on Linux, D3D11Memory (NV12, BGRA,
  RGBA, BGRx) on Windows, and system memory (NV12, I420, BGR, BGRx, BGRA) on
  both. A frame is imported (or uploaded, for system memory) once and shared by
  all of its ROIs.
- **Inference device**: any. With a GPU device, the kernel runs in OpenVINO™'s
  OpenCL context and writes the model input to GPU memory, which the inference
  reads in place. With another device (CPU, NPU, AUTO, ...), the kernel runs on
  the GPU the frames are on and writes the model input to host memory. GPU
  frames are therefore accepted with `device=CPU` too.
- **Batches**: the infer request of a batch has one model input, written by
  the kernels. The ROIs of a frame are pre-processed by one kernel launch, so
  `batch-size` greater than 1 also reduces the number of launches in pipelines
  with many ROIs per frame.
- **model-proc**: all resize types except `aspect-ratio-multiple-of`, all crop
  types, padding, `color_space` BGR or RGB, and range/mean/std normalization
  (applied by OpenVINO™). The regions are computed as by the `va` backends,
  with the same rounding. Unlike them, `aspect-ratio-pad` keeps the aspect
  ratio and places the image at the top-left corner, as `opencv` does.
- **Sampling and color**: bilinear sampling at pixel centers, without
  anti-aliasing for large downscales (as `cv::resize` with `INTER_LINEAR`). YUV
  frames are converted to RGB with the color matrix and range of the
  `colorimetry` in their caps. If the caps don't specify it, GStreamer's default
  for the frame size applies: BT.601 up to 576 lines, BT.709 above, and BT.2020
  from 2160 lines, all limited range.

The backend is never selected automatically. If it is not supported with the
current settings (for example, with `custom-preproc-lib`, an
`aspect-ratio-multiple-of` resize, a grayscale model or an unsupported frame
format), the element falls back to the backend it would select automatically
and logs a warning. It also falls back when GPU inference runs on another GPU
than the one the frames are on: to `va-surface-sharing` on Linux and to `d3d11`
on Windows. `batch-timeout` is not supported, as with the VA and D3D backends.

```bash
gst-launch-1.0 filesrc location=${VIDEO_FILE} ! parsebin ! vah264dec ! \
  gvadetect model=${DETECTION_MODEL} device=GPU pre-process-backend=opencl ! queue ! \
  gvaclassify model=${CLASSIFICATION_MODEL} device=GPU pre-process-backend=opencl batch-size=8 ! queue ! \
  gvafpscounter ! fakesink
```

**Post-processing** uses so called converters to process raw inference results.
The converters transform the results from raw output
tensors to a GStreamer representation attached to a frame as metadata.

If there is no suitable pre- and/or post-processing implementation in the Deep Learning Streamer,
[Custom Processing](./custom_processing.md) can be used.

For models with custom operations not natively supported by OpenVINO™, see
[OpenVINO Custom Operations](./openvino_custom_operations.md).

## 3. Specify model files in GStreamer elements

The path to the .xml model file must be specified by the mandatory `model`
property of GStreamer inference elements -
`gvainference`/`gvadetect`/`gvaclassify`.
Below is an example of a pipeline with
object detection ([gvadetect](../elements/gvadetect.md)) and
classification ([gvaclassify](../elements/gvaclassify.md))

```bash
gvadetect model=MODEL1_FILE_PATH.xml ! gvaclassify model=MODEL1_FILE_PATH.xml
```

The .bin file is expected to be located in the same folder as the .xml file,
and to have the same filename (with a different extension).

Below is the example of a pipeline including `gvadetect` and `gvaclassify` with
pre-processing/post-processing rules:

```bash
gvadetect model=MODEL1_FILE_PATH.xml model-proc=MODEL1_FILE_PATH.json ! gvaclassify model=MODEL2_FILE_PATH.xml model-proc=MODEL2_FILE_PATH.json
```

<!--hide_directive
:::{toctree}
:maxdepth: 2

download_public_models
:::
hide_directive-->
