# YOLO Models

This article describes how to prepare models from the **YOLO** family for
integration with the Deep Learning Streamer pipeline.

## Ultralytics Model Preparation

All models supported by the [ultralytics/ultralytics](https://github.com/ultralytics/ultralytics) library can be converted to OpenVINO™ IR format by using the [Ultralytics exporter](https://docs.ultralytics.com/integrations/openvino/). DL Streamer supports many Ultralytics YOLO architectures for tasks such as zero-shot object detection, oriented object detection, segmentation, pose estimation, and more. See the [Supported Models](https://docs.openedgeplatform.intel.com/2026.0/edge-ai-libraries/dlstreamer/supported_models.html) table for details.

> **NOTE:** The instructions below are comprehensive, but for convenience, we recommend using the
> [download_ultralytics_models.py](https://github.com/open-edge-platform/dlstreamer/blob/main/scripts/download_models/download_ultralytics_models.py)
> script. It can download a YOLO model or read one from a PyTorch file and perform the required conversions automatically.
> See [Model Conversion Scripts](https://github.com/open-edge-platform/dlstreamer/blob/main/scripts/download_models/README.md) for more information.

If you prefer to prepare the model manually, the following minimal Python script converts an Ultralytics model stored in the `yolo.pt` file to IR format:

```python
from ultralytics import YOLO

# Load a YOLO PyTorch model
model = YOLO("yolo.pt")

# Export the model
model.export(format="openvino", int8=True)  # creates 'yolo_int8_openvino_model/'
```

The argument passed to `YOLO()` can be either a local PyTorch file or an identifier for a model available from Ultralytics, such as [yolo26n.pt](https://github.com/ultralytics/assets/releases/download/v8.4.0/yolo26n.pt).

The directory created by the exporter contains all files required to use the model with the `gvadetect` element. No further modifications are required.


## Other YOLO Models

> **NOTE:** To obtain ready-to-use versions of the models described below, we recommend using the [`download_public_models.sh`](https://github.com/open-edge-platform/dlstreamer/blob/main/samples/download_public_models.sh) script. See [Download Public Models](./download_public_models.md) for details.

### YOLOv7

Model preparation:

```bash
git clone https://github.com/WongKinYiu/yolov7.git
cd yolov7
python3 export.py --weights yolov7.pt --grid --dynamic-batch
# Convert the model to OpenVINO format FP32 precision
ovc yolov7.onnx --compress_to_fp16=False
# Convert the model to OpenVINO format FP16 precision
ovc yolov7.onnx
```
When used with `gvadetect`, this model requires `label-file=`[`coco_80cl.txt`](https://github.com/open-edge-platform/dlstreamer/blob/main/samples/labels/coco_80cl.txt) and `model-proc=`[`yolo-v7.json`](https://github.com/open-edge-platform/dlstreamer/blob/main/samples/gstreamer/model_proc/public/yolo-v7.json).

### Older YOLOv5 Versions

YOLOv5 models trained with `ultralytics/yolov5` are not compatible with the `ultralytics/ultralytics` library or the [download_ultralytics_models.py](https://github.com/open-edge-platform/dlstreamer/blob/main/scripts/download_models/download_ultralytics_models.py) script.

Preparing YOLOv5 7.0 from Ultralytics therefore involves two steps.

1. Convert the PyTorch model to Intel® OpenVINO™ format:

   ```bash
   git clone https://github.com/ultralytics/yolov5
   cd yolov5
   wget https://github.com/ultralytics/yolov5/releases/download/v7.0/yolov5s.pt
   python3 export.py --weights yolov5s.pt --include openvino --dynamic
   ```

2. Then reshape the model to enable a dynamic batch size while keeping the
   other dimensions fixed:

   ```python
   from openvino.runtime import Core
   from openvino.runtime import save_model
   core = Core()
   model = core.read_model("yolov5s_openvino_model/yolov5s.xml")
   model.reshape([-1, 3, 640, 640])
   save_model(model, "yolov5s.xml")
   ```

### YOLOX

An Intel® OpenVINO™ version of the model can be obtained from the ONNX file:

```bash
wget https://github.com/Megvii-BaseDetection/YOLOX/releases/download/0.1.1rc0/yolox_s.onnx
ovc yolox_s.onnx --compress_to_fp16=False
```

## Model Usage

See [Samples](https://github.com/open-edge-platform/dlstreamer/tree/main/samples/gstreamer/gst_launch/detection_with_yolo) for detailed examples of Deep Learning Streamer pipelines using different YOLO models.
