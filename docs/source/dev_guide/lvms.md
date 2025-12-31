# Large Vision Models

This article explains how to prepare the Vision Transformer from the
CLIP models for integration with the Deep Learning Streamer pipeline.

> **NOTE:** The instructions provided below are comprehensive, but for convenience,
> it is recommended to use the
> [download_public_models.sh](https://github.com/open-edge-platform/dlstreamer/blob/master/samples/download_public_models.sh)
> script. This script will download all supported models and perform the
> necessary conversions automatically. See [download_public_models](./download_public_models.md) for more information.

## 1. Setup

The instructions assume Deep Learning Streamer framework is installed on the
local system along with Intel® OpenVINO™ model downloader and converter
tools, as described in the [Tutorial](../get_started/tutorial.md#setup).

It is also necessary to install the Transformers and Pillow packages:

```bash
pip install transformers
pip install pillow
```

## 2. Model preparation

Below is a Python script for converting the Vision Transformer from the
**clip-vit-large-patch14**/**clip-vit-base-patch16**/**clip-vit-base-patch32**
model to the Intel® OpenVINO™ format. Since using a sample input is
recommended during the conversion, prepare a sample image in one of the
common formats and replace `IMG_PATH` with the relevant value:

### clip-vit-large-patch14

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

### clip-vit-base-patch16

```python
from transformers import CLIPProcessor, CLIPVisionModel
import PIL
import openvino as ov
from openvino.runtime import PartialShape, Type
import sys
import os

MODEL='clip-vit-base-patch16'
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

### clip-vit-base-patch32

```python
from transformers import CLIPProcessor, CLIPVisionModel
import PIL
import openvino as ov
from openvino.runtime import PartialShape, Type
import sys
import os

MODEL='clip-vit-base-patch32'
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

## 3. Model usage

See the [generate_frame_embeddings.sh](https://github.com/open-edge-platform/dlstreamer/blob/master/samples/gstreamer/gst_launch/lvm/generate_frame_embeddings.sh) sample for detailed
examples of Deep Learning Streamer pipelines using the model.
