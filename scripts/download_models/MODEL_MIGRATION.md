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

## Impact on functional tests (configs_ov2/common/samples.json)

This section maps every test set in `samples.json` to the model(s) it requires
and states whether those models are available through the new download helpers.

Legend:
- ✅ **OK** — all required models can be downloaded with `scripts/download_models/`.
- ⚠️ **PARTIAL** — some models OK, others not yet covered.
- ❌ **BLOCKED** — at least one required model has no replacement in the new scripts; removing the legacy script breaks this test.

### Tests that are OK after migration

| Test set | Model(s) required | New script |
| --- | --- | --- |
| `yolov5s_CPU`, `yolov5s_fp32_GPU`, `yolov5s_NPU` | `yolov5s` | `download_ultralytics_models.py --model yolov5s.pt` |
| `yolov5su_CPU`, `yolov5su_GPU`, `yolov5su_fp32_NPU` | `yolov5su` | `download_ultralytics_models.py --model yolov5su.pt` |
| `yolov5su_int8_CPU`, `yolov5su_int8_GPU`, `yolov5su_int8_NPU` | `yolov5su` (INT8) | `download_ultralytics_models.py --model yolov5su.pt --int8` |
| `yolov8s_CPU`, `yolov8s_GPU`, `yolov8s_NPU` | `yolov8s` | `download_ultralytics_models.py --model yolov8s.pt` |
| `yolov8s_int8_CPU`, `yolov8s_int8_GPU`, `yolov8s_int8_NPU` | `yolov8s` (INT8) | `download_ultralytics_models.py --model yolov8s.pt --int8` |
| `yolov8n-obb_CPU`, `yolov8n-obb_GPU`, `yolov8n-obb_NPU` | `yolov8n-obb` | `download_ultralytics_models.py --model yolov8n-obb.pt` |
| `yolov8n-seg_CPU`, `yolov8n-seg_GPU`, `yolov8n-seg_NPU` | `yolov8n-seg` | `download_ultralytics_models.py --model yolov8n-seg.pt` |
| `yolov9c_CPU`, `yolov9c_GPU`, `yolov9c_NPU` | `yolov9c` | `download_ultralytics_models.py --model yolov9c.pt` |
| `yolov10s_CPU`, `yolov10s_GPU` | `yolov10s` | `download_ultralytics_models.py --model yolov10s.pt` |
| `yolo11s_CPU`, `yolo11s_GPU`, `yolo11s_NPU` | `yolo11s` | `download_ultralytics_models.py --model yolo11s.pt` |
| `yolo11s_int8_CPU`, `yolo11s_int8_GPU`, `yolo11s_int8_NPU` | `yolo11s` (INT8) | `download_ultralytics_models.py --model yolo11s.pt --int8` |
| `yolo11s-obb_CPU`, `yolo11s-obb_GPU`, `yolo11s-obb_NPU` | `yolo11s-obb` | `download_ultralytics_models.py --model yolo11s-obb.pt` |
| `yolo11s-seg_CPU`, `yolo11s-seg_GPU`, `yolo11s-seg_NPU` | `yolo11s-seg` | `download_ultralytics_models.py --model yolo11s-seg.pt` |
| `yolo11s-pose_CPU`, `yolo11s-pose_GPU`, `yolo11s-pose_NPU` | `yolo11s-pose` | `download_ultralytics_models.py --model yolo11s-pose.pt` |
| `yolo26n_int8_CPU/GPU/NPU` | `yolo26n` (INT8) | `download_ultralytics_models.py --model yolo26n.pt --int8` |
| `yolo26s_int8_CPU/GPU/NPU` | `yolo26s` (INT8) | `download_ultralytics_models.py --model yolo26s.pt --int8` |
| `yolo26m_int8_CPU/GPU/NPU` | `yolo26m` (INT8) | `download_ultralytics_models.py --model yolo26m.pt --int8` |
| `yolo26l_int8_CPU/GPU/NPU` | `yolo26l` (INT8) | `download_ultralytics_models.py --model yolo26l.pt --int8` |
| `yolo26x_int8_CPU/GPU/NPU` | `yolo26x` (INT8) | `download_ultralytics_models.py --model yolo26x.pt --int8` |
| `yolo26n_fp16_CPU/GPU` | `yolo26n` (FP16) | `download_ultralytics_models.py --model yolo26n.pt --half` |
| `yolo26s_fp16_CPU/GPU` | `yolo26s` (FP16) | `download_ultralytics_models.py --model yolo26s.pt --half` |
| `yolo26s-obb_int8_CPU/GPU/NPU` | `yolo26s-obb` (INT8) | `download_ultralytics_models.py --model yolo26s-obb.pt --int8` |
| `yolo26s-seg_fp16_CPU/GPU` | `yolo26s-seg` (FP16) | `download_ultralytics_models.py --model yolo26s-seg.pt --half` |
| `yolo26s-pose_fp16_CPU/GPU` | `yolo26s-pose` (FP16) | `download_ultralytics_models.py --model yolo26s-pose.pt --half` |
| `lvm_clip_vit_large_patch14_CPU`, `lvm_clip_vit_large_patch14_GPU_*` | `clip-vit-large-patch14` | `download_hf_models.py --model openai/clip-vit-large-patch14` |
| `lvm_clip_vit_base_patch16_CPU`, `lvm_clip_vit_base_patch16_GPU_*` | `clip-vit-base-patch16` | `download_hf_models.py --model openai/clip-vit-base-patch16` |
| `lvm_clip_vit_base_patch32_CPU`, `lvm_clip_vit_base_patch32_GPU_*` | `clip-vit-base-patch32` | `download_hf_models.py --model openai/clip-vit-base-patch32` |
| `motion_detect_CPU`, `motion_detect_GPU` | `yolov8n` | `download_ultralytics_models.py --model yolov8n.pt` |
| `multistream_onemodel_cpu_gpu` | `yolov8s` | `download_ultralytics_models.py --model yolov8s.pt` |
| `multistream_twomodels_cpu_gpu` | `yolov8s`, `yolov9c` | `download_ultralytics_models.py --model yolov8s.pt` + `yolov9c.pt` |
| `multistream_twomodels_cpu_yolo26s_gpu_yolo11s` | `yolo26s`, `yolo11s` | `download_ultralytics_models.py --model yolo26s.pt` + `yolo11s.pt` |
| `multistream_onemodel_npu_npu` | `yolov8s` | `download_ultralytics_models.py --model yolov8s.pt` |
| `multistream_twomodels_npu_npu` | `yolov8s`, `yolov9c` | `download_ultralytics_models.py --model yolov8s.pt` + `yolov9c.pt` |
| `gvaanalytics_CPU`, `gvaanalytics_GPU`, `gvaanalytics_NPU` | `yolov8n` | `download_ultralytics_models.py --model yolov8n.pt` |
| `custom_postproc_detect_CPU`, `custom_postproc_detect_GPU` | `yolov8n` (domyślnie) | `download_ultralytics_models.py --model yolov8n.pt` |

### Tests that are BLOCKED after migration

Removing `download_omz_models.sh` or `download_public_models.sh` without replacement **breaks** the following tests:

| Test set | Model(s) required | Reason blocked |
| --- | --- | --- |
| `face_detection_and_classification_cpu`, `face_detection_and_classification_gpu` | `face-detection-adas-0001`, `age-gender-recognition-retail-0013`, `emotions-recognition-retail-0003`, `landmarks-regression-retail-0009` | OMZ — no replacement |
| `audio_event_detection` | `aclnet` | OMZ — no replacement |
| `vehicle_pedestrian_tracking_10_gpu`, `vehicle_pedestrian_tracking_20_gpu` | `person-vehicle-bike-detection-2004`, `person-attributes-recognition-crossroad-0230`, `vehicle-attributes-recognition-barrier-0039` | OMZ — no replacement |
| `human_pose_estimation_cpu`, `human_pose_estimation_gpu`, `human_pose_estimation_npu` | `human-pose-estimation-0001` | OMZ — no replacement |
| `metapublish` | `face-detection-adas-0001` | OMZ — no replacement |
| `gvapython_cpu`, `gvapython_gpu` | `face-detection-adas-0001`, `age-gender-recognition-retail-0013`, `emotions-recognition-retail-0003`, `landmarks-regression-retail-0009` | OMZ — no replacement |
| `gvaatachroi_CPU`, `gvaatachroi_GPU`, `gvaatachroi_NPU` | `face-detection-adas-0001`, `age-gender-recognition-retail-0013` | OMZ — no replacement |
| `python_draw_face_attributes_CPU` | `face-detection-adas-0001`, `age-gender-recognition-retail-0013`, `emotions-recognition-retail-0003`, `facial-landmarks-35-adas-0002` | OMZ — no replacement |
| `benchmark_one_*`, `benchmark_two_*` | `face-detection-adas-0001` | OMZ — no replacement |
| `cpp_draw_attributes_CPU`, `cpp_draw_attributes_GPU` | `face-detection-adas-0001`, `age-gender-recognition-retail-0013`, `emotions-recognition-retail-0003`, `facial-landmarks-35-adas-0002` | OMZ — no replacement |
| `instance_segmentation_mask_rcnn_inception_resnet_v2_atrous_coco_CPU/GPU` | `mask_rcnn_inception_resnet_v2_atrous_coco` | OMZ — no replacement |
| `instance_segmentation_mask_rcnn_resnet50_atrous_coco_CPU/GPU` | `mask_rcnn_resnet50_atrous_coco` | OMZ — no replacement |
| `action_recognition_CPU` | `action-recognition-0001-encoder`, `action-recognition-0001-decoder` | OMZ — no replacement |
| `license_plate_recognition_CPU_opencv`, `license_plate_recognition_GPU_va_surface_sharing` | `yolov8_license_plate_detector`, `ch_PP-OCRv4_rec_infer` | custom source (edge-ai-resources) — no replacement |
| `custom_postproc_classify_CPU`, `custom_postproc_classify_GPU` | `face-detection-adas-0001` (via face detection script) | OMZ — no replacement |
| `yolox_tiny_CPU`, `yolox-tiny_GPU` | `yolox-tiny` | Megvii source — no replacement |
| `yolox_s_CPU`, `yolox_s_GPU`, `yolox_s_NPU` | `yolox_s` | Megvii source — no replacement |
| `yolov7_CPU`, `yolov7_GPU`, `yolov7_NPU` | `yolov7` | WongKinYiu source — no replacement |

### Summary

| Category | Test sets count | Status |
| --- | --- | --- |
| Ultralytics YOLO (v5 and newer except v7) | ~50 | ✅ OK |
| CLIP (lvm tests) | 9 | ✅ OK |
| Motion detect, multistream, analytics | ~10 | ✅ OK |
| OMZ-based (face, pose, vehicle, audio, action) | ~25 | ❌ BLOCKED |
| Custom source (YOLOX, YOLOv7, license plate) | ~8 | ❌ BLOCKED |

> **Recommendation**: keep `samples/download_omz_models.sh` and the OMZ-specific
> paths in `samples/download_public_models.sh` until OMZ models are replaced or
> tests are updated. Migrate only the Ultralytics and CLIP tests to the new helpers.

## Example commands

```bash
# Ultralytics
python scripts/download_models/download_ultralytics_models.py --model yolov8n.pt --outdir "$MODELS_PATH"

# Hugging Face CLIP
python scripts/download_models/download_hf_models.py --model openai/clip-vit-base-patch32 --outdir "$MODELS_PATH"

# TIMM
python scripts/download_models/download_timm_models.py import --model efficientnet_b0 --precision both --output-dir "$MODELS_PATH"
```