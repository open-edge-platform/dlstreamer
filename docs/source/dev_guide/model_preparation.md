# Model Preparation

When getting started with Deep Learning Streamer, the best way to obtain a
collection of models ready for use in video analytics pipelines is to
run
[download_omz_models.sh](https://github.com/open-edge-platform/dlstreamer/blob/master/samples/download_omz_models.sh)
and
[download_public_models.sh](https://github.com/open-edge-platform/dlstreamer/blob/master/samples/download_public_models.sh).
These scripts will download models from
[Open Model Zoo](https://github.com/openvinotoolkit/open_model_zoo) and other
sources, handle the necessary conversions and put model files in a
directory specified by the `MODELS_PATH` environment variable.

This way, you will be able to easily perform the most popular tasks,
such as object detection and classification, instance segmentation, face
localization and many others. For examples of how to set up Deep
Learning Streamer pipelines that carry out these functions, refer to the
[sample directory](https://github.com/open-edge-platform/dlstreamer/tree/master/samples/gstreamer/gst_launch).

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

1. Choose model(s) from the extensive set of pre-trained models available in
   [Open Model Zoo](https://github.com/openvinotoolkit/open_model_zoo)
   (already in the IR format).
2. Use
   [OpenVINO™ Toolkit Model Conversion](https://docs.openvino.ai/2024/openvino-workflow/model-preparation/convert-model-to-ir.html)
   method for converting your model from the training framework format
   (e.g., TensorFlow) to the IR format.

When using a pre-trained model from Open Model Zoo, consider using the
[Model Downloader](https://github.com/openvinotoolkit/open_model_zoo/blob/master/tools/model_tools/README.md)
tool to facilitate the model downloading process.

When converting a custom model, you can optionally utilize the
[Post-Training Model Optimization and Compression](https://docs.openvino.ai/2024/openvino-workflow/model-optimization.html)
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
memory (VAMemory and DMABuf).

| pre-process-backend | Description | Memory type in pipeline | Configurable with model-proc? |
|---|---|---|---|
| ie | Short for “Inference Engine”. It resizes an image with a bilinear algorithm and sets color format which is deduced from current media. All that’s done with capabilities provided by Inference Engine from OpenVINO™ Toolkit. | System | No |
| opencv | All power of OpenCV is leveraged for input image pre-processing. Provides a wide variety of operations that can be performed on image. | System | Yes |
| <br>va<br><br> | Should be used in pipelines with GPU memory. Performs mapping to the system memory and uses VA pre-processor. | <br>VAMemory<br>and<br>DMABuf<br><br> | Yes |
| <br>va-surface-sharing<br><br> | Should be used in pipelines with GPU memory and GPU inference device. Doesn’t perform mapping to the system memory. As a pre-processor, it performs image resize, crop, and sets color format to NV12. | <br>VAMemory<br><br> | Partially |

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

yolo_models
lvms
download_public_models
:::
hide_directive-->
