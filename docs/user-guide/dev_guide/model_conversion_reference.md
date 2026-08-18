# Model Conversion Reference

This reference describes isolated Python environments and reproducible commands
for converting models from their original framework format to OpenVINO™
Intermediate Representation (IR). Each conversion produces an `.xml` model
topology file and a matching `.bin` weights file.

Package commands in this document assume an isolated environment. The table
starts with conversion methods shared by broad model families and then narrows
to models requiring version overrides, extra modules, or custom conversion
stages.

## Conversion Methods

| Models | Modules | Conversion command |
|---|---|---|
| **Ultralytics YOLO:** YOLOv5u, YOLOv8, YOLOv9, YOLOv10, YOLO11, YOLO26, and YOLOE-26 | `python -m pip install openvino==2026.2.0 nncf==3.0.0 ultralytics==8.4.57` | `yolo export model=yolo11n.pt format=openvino dynamic=True` |
| **Other Hugging Face models supported by Optimum Intel** | `python -m pip install "optimum-intel[openvino]==2.0.0"` | `optimum-cli export openvino --model MODEL_ID --task TASK --weight-format fp16 OUTPUT_DIR` |
| **ONNX models** (for example, CenterFace and HSEmotion) | `python -m pip install openvino==2026.2.0` | `ovc MODEL.onnx --input INPUT_SHAPE --output_model MODEL.xml` |
| **OpenVINO GenAI VLMs using the standard environment:** LLaVA 1.5, LLaVA-NeXT, LLaVA-NeXT-Video, Qwen2-VL, Qwen2.5-VL, Fara-7B, Gemma 3, and Gemma 3n | `python -m pip install "optimum-intel[openvino]==2.0.0" "transformers==4.57.6"` | `optimum-cli export openvino --model MODEL_ID --task image-text-to-text --weight-format fp16 OUTPUT_DIR` |
| **TIMM image classification** | `python -m pip install "optimum-intel[openvino]==2.0.0" timm==1.0.26` | `optimum-cli export openvino --library timm --task image-classification --model MODEL_ID --weight-format fp16 OUTPUT_DIR` |
| **VLMs requiring remote model code:** nanoLLaVA 1.5, MiniCPM-V 2.6, Phi-3 Vision, and Qwen3-VL | `python -m pip install "optimum-intel[openvino]==2.0.0" "transformers==4.57.6"` | `optimum-cli export openvino --model MODEL_ID --task image-text-to-text --trust-remote-code --weight-format fp16 OUTPUT_DIR` |
| **InternVL2, InternVL2.5, and InternVL3** | `python -m pip install "optimum-intel[openvino]==2.0.0" "transformers==4.57.6" "timm==1.0.28" "einops==0.8.2"` | `optimum-cli export openvino --model MODEL_ID --task image-text-to-text --trust-remote-code --weight-format fp16 OUTPUT_DIR` |
| **nanoLLaVA** | `python -m pip install "optimum-intel[openvino]==2.0.0" "transformers==4.53.3"` | `optimum-cli export openvino --model qnguyen3/nanoLLaVA --task image-text-to-text --trust-remote-code --weight-format fp16 nanollava-openvino` |
| **MiniCPM-o 2.6** | `python -m pip install "optimum-intel[openvino]==2.0.0" "transformers==4.51.3"` | `optimum-cli export openvino --model openbmb/MiniCPM-o-2_6 --task image-text-to-text --trust-remote-code --weight-format fp16 minicpm-o-2_6-openvino` |
| **VideoChat-Flash** | `python -m pip install "optimum-intel[openvino]==2.0.0" "transformers==4.57.6" modelscope` | `modelscope download --model OpenGVLab/VideoChat-Flash-Qwen2_5-7B_InternVideo2-1B --local_dir VideoChat-Flash && optimum-cli export openvino --model VideoChat-Flash --task image-text-to-text --trust-remote-code --weight-format fp16 videochat-flash-openvino` |
| **Phi-4 Multimodal** | `python -m pip install "optimum-intel[openvino]==2.0.0" "transformers==4.53.3"` | `hf download microsoft/Phi-4-multimodal-instruct --revision refs/pr/78 --local-dir Phi-4-multimodal-patched && optimum-cli export openvino --model Phi-4-multimodal-patched --task image-text-to-text --trust-remote-code --weight-format fp16 phi-4-multimodal-openvino` |
| **Qwen3.5 and Qwen3.6** | `python -m pip install "optimum-intel[openvino]==2.0.0" && python -m pip install --no-deps "https://github.com/huggingface/optimum-intel/archive/a8c4734741e766ef95d7f1a7d1e29a1d4ba2ab8f.tar.gz" && python -m pip install "transformers==5.2.0"` | `optimum-cli export openvino --model MODEL_ID --task image-text-to-text --weight-format fp16 OUTPUT_DIR` |
| **Gemma 4** | `python -m pip install "optimum-intel[openvino]==2.0.0" && python -m pip install --no-deps "https://github.com/huggingface/optimum-intel/archive/a8c4734741e766ef95d7f1a7d1e29a1d4ba2ab8f.tar.gz" && python -m pip install "transformers==5.5.0"` | `optimum-cli export openvino --model MODEL_ID --task image-text-to-text --weight-format fp16 OUTPUT_DIR` |
| **Gemma 4 Unified** | `python -m pip install "optimum-intel[openvino]==2.0.0" && python -m pip install --no-deps "git+https://github.com/huggingface/optimum-intel.git@3e159833682a192132094443b7ff8da3d7d46c42" && python -m pip install "transformers==5.10.0"` | `optimum-cli export openvino --model MODEL_ID --task image-text-to-text --weight-format fp16 OUTPUT_DIR` |
| **RT-DETR and RT-DETRv2** | `python -m pip install "optimum-intel[openvino]==2.0.0" optimum-onnx==0.1.0` | `optimum-cli export onnx --model MODEL_ID --task object-detection --opset 18 --width 640 --height 640 rtdetr-onnx && ovc rtdetr-onnx/model.onnx --output_model rtdetr.xml` |
| **YOLOv7** | From the cloned repository: `python -m pip install -r requirements.txt openvino==2026.2.0` | `python export.py --weights yolov7.pt --grid --dynamic-batch && ovc yolov7.onnx --output_model yolov7.xml` |
| **PaddleOCR** | `python -m pip install openvino==2026.2.0 paddlepaddle paddle2onnx` | `paddle2onnx --model_dir paddle_model --model_filename inference.pdmodel --params_filename inference.pdiparams --save_file model.onnx --opset_version 14 && ovc model.onnx --output_model paddleocr.xml` |

Replace `MODEL_ID`, `TASK`, `OUTPUT_DIR`, and other uppercase placeholders
with values for the selected model. The notes below provide representative
model IDs, input shapes, precision options, and required post-conversion
metadata without repeating the table commands.

## Conversion Notes

### Ultralytics YOLO

The model argument may be a supported Ultralytics model name, which is
downloaded automatically, or a path to a local `.pt` checkpoint. Replace
`yolo11n.pt` in the export command with another supported detection,
segmentation, pose, oriented bounding box (OBB), or classification checkpoint.
For example:

| Task | Example model |
|---|---|
| Object detection | `yolo11n.pt` |
| Instance segmentation | `yolo11n-seg.pt` |
| Pose estimation | `yolo11n-pose.pt` |
| Oriented object detection | `yolo11n-obb.pt` |
| Image classification | `yolo11n-cls.pt` |

#### Precision Options

The default export uses floating-point precision.

| Precision | Export argument | Notes |
|---|---|---|
| FP32 | None | The Ultralytics exporter default; omit `half` and `int8`. |
| FP16 | `half=True` | Compresses model weights to FP16. |
| INT8 | `int8=True` | Performs INT8 quantization. NNCF is required, and Ultralytics may download a calibration dataset. Validate accuracy against representative data before deployment. |

For example, export an INT8 model with the Ultralytics CLI:

```bash
yolo export model=yolo11n.pt format=openvino dynamic=True int8=True
```

Or export a local checkpoint with the Python API:

```python
from ultralytics import YOLO

model = YOLO("/path/to/custom.pt")
model.export(format="openvino", dynamic=True, half=True)
```

Do not enable `half` and `int8` together.

> **NOTE:** Legacy YOLOv5 checkpoints from the
> [`ultralytics/yolov5`](https://github.com/ultralytics/yolov5) repository are
> not the same family as Ultralytics YOLOv5u checkpoints. Follow the
> [Older YOLOv5 Versions](./yolo_models.md#older-yolov5-versions) instructions
> for those models.

For exporter behavior and additional model-specific details, see
[YOLO Models](./yolo_models.md) and the
[Ultralytics OpenVINO integration](https://docs.ultralytics.com/integrations/openvino/).

### Hugging Face Models Supported by Optimum Intel

Optimum Intel supports OpenVINO export for many architectures from libraries
such as Transformers, Diffusers, and Sentence Transformers. Check the
[supported model architectures](https://huggingface.co/docs/optimum-intel/en/openvino/models)
before conversion. The `--model` argument accepts either a Hugging Face Hub
model ID or a local model directory.

For Hub models, Optimum usually infers the task. Set `--task` explicitly when
inference is ambiguous and always set it for a local model.

Choose the exported weight precision with `--weight-format`:

| Precision | Optimum CLI argument | Notes |
|---|---|---|
| FP32 | `--weight-format fp32` | Retains full-precision weights. |
| FP16 | `--weight-format fp16` | Compresses model weights to FP16. |
| INT8 | `--weight-format int8` | Applies weight-only INT8 quantization. |
| INT4 | `--weight-format int4` | Applies weight-only INT4 quantization, primarily for large generative models. |

For full INT8 quantization of weights and activations, use `--quant-mode int8`
and provide a representative dataset when required by the model. For gated or
private models, authenticate with Hugging Face before running the export.

#### OpenVINO GenAI Vision-Language Models

The model groups and overrides in the conversion table follow the
[OpenVINO GenAI supported-models table](https://openvinotoolkit.github.io/openvino.genai/docs/supported-models/#vision-language-models-vlms).
Replace `MODEL_ID` and `OUTPUT_DIR` with a model variant and destination from
the applicable group. Fara-7B, Gemma 3, Gemma 3n, and Gemma 4 repositories are
gated, so authenticate with Hugging Face before conversion.

The Qwen3.5, Qwen3.6, Gemma 4, and Gemma 4 Unified rows reproduce OpenVINO
GenAI development configurations rather than released dependency sets. Their
source exporter is installed with `--no-deps`, so `pip check` reports the
intentional conflict between Optimum Intel 2.0.0 metadata and Transformers 5.x.
Prefer a compatible released Optimum Intel package when one becomes available.

`--trust-remote-code` executes code supplied by the model repository. Use it
only after reviewing and pinning a trusted model revision. Some repositories
are gated; authenticate with `hf auth login` before conversion when required.

VideoChat-Flash is downloaded from ModelScope before local export and supports
video, but not image, input at runtime. Phi-4 Multimodal uses the patched files
from Hugging Face discussion 78. For Phi-3 Vision runtime, take the generation
EOS token from the tokenizer because model configurations are inconsistent.

Optimum support means that the model can be converted; it does not guarantee
that a DL Streamer element implements the required inputs and post-processing.
Check [Supported Models](../supported_models.md) before deployment. For more
details, see [Transformer Models](./transformers.md) and the
[Optimum Intel export guide](https://huggingface.co/docs/optimum-intel/en/openvino/export).

### TIMM Image Classification

Optimum exports pretrained TIMM models hosted on Hugging Face. Pass the model's
Hugging Face repository ID to `--model`; this is not always identical to the
short name accepted by the TIMM Python API. For example,
`mobilenetv3_small_100` is hosted as
`timm/mobilenetv3_small_100.lamb_in1k`.

The output directory contains the OpenVINO IR and configuration files required
for inference. 

## Model-Specific Details

These details supplement the model-specific rows in the conversion table.


### YOLOv7

Prepare the original YOLOv7 repository and checkpoint before running the
installation and conversion commands from the table:

```bash
git clone https://github.com/WongKinYiu/yolov7.git
cd yolov7
wget https://github.com/WongKinYiu/yolov7/releases/download/v0.1/yolov7.pt
sed -i 's/torch\.load(w, map_location=map_location)/torch.load(w, map_location=map_location, weights_only=False)/g' models/experimental.py
```

The loader adjustment is required by PyTorch 2.6 and newer. Only load a
checkpoint from a source you trust when `weights_only=False` is used.

When using the IR with `gvadetect`, also specify
[`coco_80cl.txt`](https://github.com/open-edge-platform/dlstreamer/blob/main/samples/labels/coco_80cl.txt)
as the labels file and
[`yolo-v7.json`](https://github.com/open-edge-platform/dlstreamer/blob/main/samples/gstreamer/model_proc/public/yolo-v7.json)
as the model-proc file.

### PaddleOCR

PaddleOCR models require a PaddlePaddle-to-ONNX conversion before `ovc`. For a
PP-OCRv4 inference model containing `inference.pdmodel` and
`inference.pdiparams`, use the command from the table. Newer PaddleOCR exports
may contain `inference.json` instead of
`inference.pdmodel`; pass the filename present in the downloaded model. Keep
the corresponding character dictionary and model configuration with the IR,
because OCR post-processing needs them to decode output tokens.
