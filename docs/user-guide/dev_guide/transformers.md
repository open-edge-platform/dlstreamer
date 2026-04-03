# Transformer Models

This article explains how to prepare models based on the [Hugging Face](https://huggingface.co/welcome) [`transformers`](https://github.com/huggingface/transformers) library for integration with the Deep Learning Streamer pipeline.

Many transformer-based models can be converted to OpenVINO™ IR format using [optimum-cli](https://huggingface.co/docs/optimum-intel/en/openvino/export). DL Streamer supports selected Hugging Face architectures for tasks such as image classification, object detection, audio transcription, and more. See the [Supported Models](https://docs.openedgeplatform.intel.com/dev/edge-ai-libraries/dlstreamer/supported_models.html) table for details.

> **NOTE:** The instructions below are comprehensive, but for convenience, we recommend using the
> [download_hf_models.py](https://github.com/open-edge-platform/dlstreamer/blob/main/scripts/download_models/download_hf_models.py)
> script. It can download a model from the Hugging Face Hub and perform the required conversions automatically.
> See [Model Conversion Scripts](https://github.com/open-edge-platform/dlstreamer/blob/main/scripts/download_models/README.md) for more information.

## Optimum-Intel Supported Models

The list available [here](https://huggingface.co/docs/optimum-intel/en/openvino/models) includes models that can be converted to IR format with a single `optimum-cli` command. If a model architecture is [supported by DL Streamer](https://docs.openedgeplatform.intel.com/dev/edge-ai-libraries/dlstreamer/supported_models.html#supported-architectures), it can typically be prepared as follows:

```bash
optimum-cli export openvino --model provider_id/model_id --weight-format=int8 output_path
```

The directory specified by `output_path` contains all files required to use the model with DL Streamer elements such as `gvaclassify` or `gvagenai`. No further modifications are required. Some Visual Language Models (VLMs) may require additional `optimum-cli` options; see the [OpenVINO™ documentation](https://openvinotoolkit.github.io/openvino.genai/docs/supported-models/#visual-language-models-vlms) for details.

## RT-DETR and RT-DETRv2 Models

Hugging Face models based on the `RTDetrForObjectDetection` and `RtDetrV2ForObjectDetection` architectures must first be exported to ONNX format using [Optimum-ONNX](https://huggingface.co/docs/optimum-onnx/onnx/usage_guides/export_a_model). For example:

```bash
optimum-cli export onnx --model PekingU/rtdetr_v2_r18vd --task object-detection --opset 18 --width 640 --height 640 ./out/rtdetr_v2_r18vd_onnx
```

This command creates the `./out/rtdetr_v2_r18vd_onnx/model.onnx` file, which can then be converted to IR format using the OpenVINO [ovc tool](https://docs.openvino.ai/2026/openvino-workflow/model-preparation/convert-model-onnx.html):

```bash
ovc ./out/rtdetr_v2_r18vd_onnx/model.onnx
```

The `./out/rtdetr_v2_r18vd_onnx/` directory now contains all files required to use the model with the DL Streamer `gvadetect` element.

## CLIP Models

DL Streamer supports using the Vision Transformer (ViT) component of CLIP models to generate image embeddings. However, this component cannot be extracted from the `CLIPModel` architecture by using `optimum-cli`. Instead, use the following Python script to convert the Vision Transformer from **clip-vit-large-patch14**, **clip-vit-base-patch16**, or **clip-vit-base-patch32** to Intel® OpenVINO™ format. Because conversion is best performed with a sample input, prepare an image in a common format and replace `IMG_PATH` with the appropriate value.

```python
from transformers import CLIPProcessor, CLIPVisionModel
import PIL
import openvino as ov
from openvino.runtime import PartialShape, Type
import sys
import os

MODEL='clip-vit-large-patch14'
IMG_PATH = "sample_image.jpg"

img = PIL.Image.open(IMG_PATH)
vision_model = CLIPVisionModel.from_pretrained('openai/'+MODEL)
processor = CLIPProcessor.from_pretrained('openai/'+MODEL)
batch = processor.image_processor(images=img, return_tensors='pt')["pixel_values"]

print("Conversion starting...")
ov_model = ov.convert_model(vision_model, example_input=batch)
print("Conversion finished.")

# Define the input shape explicitly
input_shape = PartialShape([-1, batch.shape[1], batch.shape[2], batch.shape[3]])

# Set the input shape and type explicitly
for input in ov_model.inputs:
   input.get_node().set_partial_shape(PartialShape(input_shape))
   input.get_node().set_element_type(Type.f32)

ov_model.set_rt_info("clip_token", ['model_info', 'model_type'])
ov_model.set_rt_info("68.500,66.632,70.323", ['model_info', 'scale_values'])
ov_model.set_rt_info("122.771,116.746,104.094", ['model_info', 'mean_values'])
ov_model.set_rt_info("True", ['model_info', 'reverse_input_channels'])
ov_model.set_rt_info("crop", ['model_info', 'resize_type'])

ov.save_model(ov_model, MODEL + ".xml")
```

Alternatively, you can use the [download_hf_models.py](https://github.com/open-edge-platform/dlstreamer/blob/main/scripts/download_models/download_hf_models.py) script, to perform the above steps automatically.

## Model Usage

The choice of the DL Streamer element that should be used to perform the inference with a given model depends on the task. The table below maps the tasks and sample model architectures to the appropriate inference elements.

| Task | Example Architecture | Inference element |
| --- | --- | --- |
| Image Classification | ViTForImageClassification | `gvaclassify` |
| Object Detection | RTDetrForObjectDetection | `gvadetect` |
| Speech Recognition | WhisperForConditionalGeneration  | `gvaaudiotranscribe` |
| Image To Text (VLMs) | Phi4MMForCausalLM | `gvagenai` |
| Image Embeddings | CLIPModel | `gvainference` |

See the following samples for detailed examples of DL Streamer pipelines that use transformer-based models:

1. [Using VLM Models With gvagenai Element](https://github.com/open-edge-platform/dlstreamer/tree/main/samples/gstreamer/gst_launch/gvagenai)
2. [Image Embeddings Generation with ViT](https://github.com/open-edge-platform/dlstreamer/blob/main/samples/gstreamer/gst_launch/lvm/)
3. [Face Detection and Classification](https://github.com/open-edge-platform/dlstreamer/tree/main/samples/gstreamer/python/face_detection_and_classification)
4. [Smart Network Video Recorder for Lane Hogging Detection](https://github.com/open-edge-platform/dlstreamer/tree/main/samples/gstreamer/python/smart_nvr)
5. [VLM Alerts](https://github.com/open-edge-platform/dlstreamer/tree/main/samples/gstreamer/python/vlm_alerts)
