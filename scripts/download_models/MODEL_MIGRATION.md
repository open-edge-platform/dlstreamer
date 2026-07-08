# Legacy Model Download Migration

This document maps legacy model download entry points:

- `samples/download_public_models.sh`
- `samples/download_omz_models.sh`

to the newer download helpers in `scripts/download_models/`:

- `download_ultralytics_models.py`
- `download_hf_models.py`
- `download_timm_models.py`

## How to read this table

- `Legacy model(s)`: model name accepted by the old scripts.
- `New script`: helper to use from `scripts/download_models/`.
- `New argument`: value to pass to the new helper.
- `Notes`: important differences in naming or coverage.

## Models covered by the new scripts

| Legacy model(s) | New script | New argument | Notes |
| --- | --- | --- | --- |
| `yolov5n`, `yolov5s`, `yolov5m`, `yolov5l`, `yolov5x`, `yolov5n6`, `yolov5s6`, `yolov5m6`, `yolov5l6`, `yolov5x6` | `download_ultralytics_models.py` | `<model>.pt` | Legacy YOLOv5 models were previously exported through a dedicated compatibility path. In the new flow they should be treated as Ultralytics checkpoints. |
| `yolov5nu`, `yolov5su`, `yolov5mu`, `yolov5lu`, `yolov5xu`, `yolov5n6u`, `yolov5s6u`, `yolov5m6u`, `yolov5l6u`, `yolov5x6u` | `download_ultralytics_models.py` | `<model>.pt` | Direct Ultralytics family mapping. |
| `yolov8n`, `yolov8s`, `yolov8m`, `yolov8l`, `yolov8x` | `download_ultralytics_models.py` | `<model>.pt` | Direct Ultralytics family mapping. |
| `yolov8n-obb`, `yolov8s-obb`, `yolov8m-obb`, `yolov8l-obb`, `yolov8x-obb` | `download_ultralytics_models.py` | `<model>.pt` | Direct Ultralytics family mapping. |
| `yolov8n-seg`, `yolov8s-seg`, `yolov8m-seg`, `yolov8l-seg`, `yolov8x-seg` | `download_ultralytics_models.py` | `<model>.pt` | Direct Ultralytics family mapping. |
| `yolov8n-pose`, `yolov8s-pose`, `yolov8m-pose`, `yolov8l-pose`, `yolov8x-pose` | `download_ultralytics_models.py` | `<model>.pt` | Direct Ultralytics family mapping. |
| `yolov9t`, `yolov9s`, `yolov9m`, `yolov9c`, `yolov9e` | `download_ultralytics_models.py` | `<model>.pt` | Direct Ultralytics family mapping. |
| `yolov10n`, `yolov10s`, `yolov10m`, `yolov10b`, `yolov10l`, `yolov10x` | `download_ultralytics_models.py` | `<model>.pt` | Direct Ultralytics family mapping. |
| `yolo11n`, `yolo11s`, `yolo11m`, `yolo11l`, `yolo11x` | `download_ultralytics_models.py` | `<model>.pt` | Direct Ultralytics family mapping. |
| `yolo11n-obb`, `yolo11s-obb`, `yolo11m-obb`, `yolo11l-obb`, `yolo11x-obb` | `download_ultralytics_models.py` | `<model>.pt` | Direct Ultralytics family mapping. |
| `yolo11n-seg`, `yolo11s-seg`, `yolo11m-seg`, `yolo11l-seg`, `yolo11x-seg` | `download_ultralytics_models.py` | `<model>.pt` | Direct Ultralytics family mapping. |
| `yolo11n-pose`, `yolo11s-pose`, `yolo11m-pose`, `yolo11l-pose`, `yolo11x-pose` | `download_ultralytics_models.py` | `<model>.pt` | Direct Ultralytics family mapping. |
| `yolo26n`, `yolo26s`, `yolo26m`, `yolo26l`, `yolo26x` | `download_ultralytics_models.py` | `<model>.pt` | Direct Ultralytics family mapping. |
| `yolo26n-obb`, `yolo26s-obb`, `yolo26m-obb`, `yolo26l-obb`, `yolo26x-obb` | `download_ultralytics_models.py` | `<model>.pt` | Direct Ultralytics family mapping. |
| `yolo26n-seg`, `yolo26s-seg`, `yolo26m-seg`, `yolo26l-seg`, `yolo26x-seg` | `download_ultralytics_models.py` | `<model>.pt` | Direct Ultralytics family mapping. |
| `yolo26n-pose`, `yolo26s-pose`, `yolo26m-pose`, `yolo26l-pose`, `yolo26x-pose` | `download_ultralytics_models.py` | `<model>.pt` | Direct Ultralytics family mapping. |
| `clip-vit-large-patch14` | `download_hf_models.py` | `openai/clip-vit-large-patch14` | Legacy short name becomes an explicit Hugging Face repo id. |
| `clip-vit-base-patch16` | `download_hf_models.py` | `openai/clip-vit-base-patch16` | Legacy short name becomes an explicit Hugging Face repo id. |
| `clip-vit-base-patch32` | `download_hf_models.py` | `openai/clip-vit-base-patch32` | Legacy short name becomes an explicit Hugging Face repo id. |
| `efficientnet-b0` | `download_timm_models.py` | `efficientnet_b0` | Covered through TIMM. The name changes from hyphen to underscore. |
| `resnest-50-pytorch` | `download_timm_models.py` | `resnest50d` | Closest supported TIMM replacement, not a byte-identical model name. |

## Models not covered by the new scripts

These legacy models do not have a direct replacement in `scripts/download_models/` today.

### From `download_public_models.sh`

- `yolox-tiny`
- `yolox_s`
- `yolov7`
- `yolov8_license_plate_detector`
- `centerface`
- `hsemotion`
- `deeplabv3`
- `ch_PP-OCRv4_rec_infer`
- `PP-OCRv5_server_rec`
- `PP-OCRv5_mobile_rec`
- any other `PP-OCRv5*` variant accepted by the old script, for example `PP-OCRv5_server_det`, `PP-OCRv5_mobile_det`, `en_PP-OCRv5_mobile_rec`
- `pallet_defect_detection`
- `colorcls2`
- `mars-small128`
- `pointpillars`

### From `download_omz_models.sh`

- `face-detection-adas-0001`
- `age-gender-recognition-retail-0013`
- `emotions-recognition-retail-0003`
- `landmarks-regression-retail-0009`
- `facial-landmarks-35-adas-0002`
- `person-vehicle-bike-detection-2004`
- `person-attributes-recognition-crossroad-0230`
- `vehicle-attributes-recognition-barrier-0039`
- `head-pose-estimation-adas-0001`
- `human-pose-estimation-0001`
- `aclnet`
- `action-recognition-0001-encoder`
- `action-recognition-0001-decoder`
- `instance-segmentation-security-1040`
- `mask_rcnn_inception_resnet_v2_atrous_coco`
- `mask_rcnn_resnet50_atrous_coco`
- `instance-segmentation-security-0002`
- `person-detection-retail-0013`
- `person-vehicle-bike-detection-crossroad-0078`
- `single-human-pose-estimation-0001`
- `ssdlite_mobilenet_v2`
- `vehicle-license-plate-detection-barrier-0106`
- `license-plate-recognition-barrier-0007`
- `vehicle-detection-adas-0002`

## Notes about partial replacements

- `efficientnet-b0` is covered via TIMM as `efficientnet_b0`.
- `resnest-50-pytorch` is covered via TIMM as `resnest50d`.
- `mask_rcnn_resnet50_atrous_coco` is not considered covered by TIMM. Although its name contains `resnet50`, it is a segmentation model rather than a plain TIMM image-classification export.
- `mask_rcnn_inception_resnet_v2_atrous_coco` is not considered covered by TIMM for the same reason.

## Example commands

```bash
# Ultralytics
python scripts/download_models/download_ultralytics_models.py --model yolov8n.pt --outdir "$MODELS_PATH"

# Hugging Face CLIP
python scripts/download_models/download_hf_models.py --model openai/clip-vit-base-patch32 --outdir "$MODELS_PATH"

# TIMM
python scripts/download_models/download_timm_models.py import --model efficientnet_b0 --precision both --output-dir "$MODELS_PATH"
```