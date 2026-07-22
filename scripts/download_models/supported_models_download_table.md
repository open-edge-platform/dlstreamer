# Supported models download table

Poniższa tabela bazuje na `docs/user-guide/supported_models.md` i pokazuje, jakie modele sa pobierane dzisiaj przez skrypty z `scripts/download_models/`.

Rows with `---` in the model or script column mean that the architecture is listed in `supported_models.md`, but there is no active download entry in this folder for it.

| Architektura | Modele pobierane | Skrypt | Czy pobieranie dziala teraz? | Powod, jesli nie |
| --- | --- | --- | --- | --- |
| Anomaly Detection: Padim | --- | brak aktywnego skryptu w `scripts/download_models/` | Nie | Brak dedykowanego skryptu w `scripts/download_models/` |
| Anomaly Detection: STFPM | --- | brak aktywnego skryptu w `scripts/download_models/` | Nie | Brak dedykowanego skryptu w `scripts/download_models/` |
| Anomaly Detection: UFlow | --- | brak aktywnego skryptu w `scripts/download_models/` | Nie | Brak dedykowanego skryptu w `scripts/download_models/` |
| Detection: YOLOv5u | `yolov5su.pt` | `download_ultralytics_models.py` | Tak | --- |
| Detection: YOLOv8 | `yolov8n.pt`, `yolov8s.pt` | `download_ultralytics_models.py` | Tak | --- |
| Detection: YOLOv9 | `yolov9c.pt` | `download_ultralytics_models.py` | Tak | --- |
| Detection: YOLOv10 | `yolov10s.pt` | `download_ultralytics_models.py` | Tak | --- |
| Detection: YOLO11 | `yolo11n.pt`, `yolo11s.pt` | `download_ultralytics_models.py` | Tak | --- |
| Detection: YOLO26 | `yolo26n.pt`, `yolo26s.pt`, `yolo26m.pt`, `yolo26l.pt`, `yolo26x.pt` | `download_ultralytics_models.py` | Tak | --- |
| Detection: YOLOE-26 | `yoloe-26n-seg.pt` | `download_ultralytics_models.py` | Tak | --- |
| Detection: RTDetrForObjectDetection | `PekingU/rtdetr_r50vd` | `download_hf_models.py` | Tak | --- |
| Detection: RtDetrV2ForObjectDetection | `PekingU/rtdetr_v2_r18vd`, `PekingU/rtdetr_v2_r50vd` | `download_hf_models.py` | Tak | --- |
| Detection: ATSS with ResNet or MobilenetV2 | --- | brak aktywnego skryptu w `scripts/download_models/` | Nie | Brak dedykowanego skryptu w `scripts/download_models/` |
| Detection: SSD with MobilenetV2 | --- | brak aktywnego skryptu w `scripts/download_models/` | Nie | Brak dedykowanego skryptu w `scripts/download_models/` |
| Detection: RT-DETR | --- | brak aktywnego skryptu w `scripts/download_models/` | Nie | Brak dedykowanego skryptu w `scripts/download_models/` |
| Detection: YOLOX | --- | brak aktywnego skryptu w `scripts/download_models/` | Nie | Brak dedykowanego skryptu w `scripts/download_models/` |
| Detection: D-Fine | --- | brak aktywnego skryptu w `scripts/download_models/` | Nie | Brak dedykowanego skryptu w `scripts/download_models/` |
| Detection: CenterFace | `centerface.onnx` | `download_public_models.sh` | Tak | --- |
| Detection: YOLOv7 | `yolov7.pt` | `download_public_models.sh` | Tak | --- |
| Emotion Recognition: HSEmotion | `enet_b0_8_va_mtl.onnx` | `download_public_models.sh` | Tak | --- |
| Feature Extraction: Mars-small128 | `mars-small128` | `download_public_models.sh` | Tak | --- |
| Image Classification: ViTForImageClassification | `dima806/fairface_age_image_detection`, `dima806/fairface_gender_image_detection`, `dima806/facial_age_image_detection`, `dima806/vehicle_10_types_image_detection` | `download_hf_models.py` | Tak | --- |
| Image Classification: Mobilenet-V3 | --- | brak aktywnego skryptu w `scripts/download_models/` | Nie | Brak dedykowanego skryptu w `scripts/download_models/` |
| Image Classification: EfficientNet-B0 | --- | brak aktywnego skryptu w `scripts/download_models/` | Nie | Brak dedykowanego skryptu w `scripts/download_models/` |
| Image Classification: DeitTiny | --- | brak aktywnego skryptu w `scripts/download_models/` | Nie | Brak dedykowanego skryptu w `scripts/download_models/` |
| Image Embeddings: CLIPModel | `openai/clip-vit-base-patch32`, `openai/clip-vit-base-patch16`, `openai/clip-vit-large-patch14` | `download_hf_models.py` | Tak | --- |
| Instance Segmentation: YOLOv8-seg | `yolov8n-seg.pt` | `download_ultralytics_models.py` | Tak | --- |
| Instance Segmentation: YOLO11-seg | `yolo11s-seg.pt` | `download_ultralytics_models.py` | Tak | --- |
| Instance Segmentation: YOLO26-seg | `yolo26s-seg.pt` | `download_ultralytics_models.py` | Tak | --- |
| Instance Segmentation: MaskRCNN with EfficientNet, ResNet50, or Swin Transformer | --- | brak aktywnego skryptu w `scripts/download_models/` | Nie | Brak dedykowanego skryptu w `scripts/download_models/` |
| Instance Segmentation: RTMDet | --- | brak aktywnego skryptu w `scripts/download_models/` | Nie | Brak dedykowanego skryptu w `scripts/download_models/` |
| Optical Character Recognition: Paddle OCRv4 | `ch_PP-OCRv4_rec_infer` | `download_public_models.sh` | Tak | --- |
| Oriented Detection: YOLOv8-obb | `yolov8n-obb.pt` | `download_ultralytics_models.py` | Tak | --- |
| Oriented Detection: YOLO11-obb | `yolo11s-obb.pt` | `download_ultralytics_models.py` | Tak | --- |
| Oriented Detection: YOLO26-obb | `yolo26s-obb.pt` | `download_ultralytics_models.py` | Tak | --- |
| Pose Estimation: YOLOv8-pose | `yolov8n-pose.pt` | `download_ultralytics_models.py` | Tak | --- |
| Pose Estimation: YOLO11-pose | `yolo11s-pose.pt` | `download_ultralytics_models.py` | Tak | --- |
| Pose Estimation: YOLO26-pose | `yolo26s-pose.pt` | `download_ultralytics_models.py` | Tak | --- |
| Semantic Segmentation: Lite-HRNet | --- | brak aktywnego skryptu w `scripts/download_models/` | Nie | Brak dedykowanego skryptu w `scripts/download_models/` |
| Semantic Segmentation: SegNext | --- | brak aktywnego skryptu w `scripts/download_models/` | Nie | Brak dedykowanego skryptu w `scripts/download_models/` |
| Semantic Segmentation: DinoV2 | --- | brak aktywnego skryptu w `scripts/download_models/` | Nie | Brak dedykowanego skryptu w `scripts/download_models/` |
| Speech Recognition: WhisperForConditionalGeneration | `openai/whisper-tiny` | `download_hf_models.py` | Tak | --- |
| VLM: InternVLChatModel | `OpenGVLab/InternVL2-1B` | `download_hf_models.py` | Tak | --- |
| VLM: LlavaForConditionalGeneration | `llava-hf/llava-1.5-7b-hf` | `download_hf_models.py` | Tak | --- |
| VLM: LlavaQwen2ForCausalLM | `qnguyen3/nanoLLaVA` | `download_hf_models.py` | Tak | --- |
| VLM: BunnyQwenForCausalLM | `qnguyen3/nanoLLaVA-1.5` | `download_hf_models.py` | Tak | --- |
| VLM: LlavaNextForConditionalGeneration | `llava-hf/llava-v1.6-mistral-7b-hf` | `download_hf_models.py` | Nie | `SIGKILL during optimum-cli export (process ran out of memory in CI environment)` |
| VLM: LlavaNextVideoForConditionalGeneration | `llava-hf/LLaVA-NeXT-Video-7B-hf` | `download_hf_models.py` | Nie | `SIGKILL during optimum-cli export (process ran out of memory in CI environment)` |
| VLM: MiniCPMO | `openbmb/MiniCPM-o-2_6` | `download_hf_models.py` | Nie | `Missing dynamically-loaded module 'image_processing_minicpmv.py' during export` |
| VLM: MiniCPMV | `openbmb/MiniCPM-V-2_6` | `download_hf_models.py` | Nie | `Gated model - requires HF token with access permissions` |
| VLM: Phi3VForCausalLM | `microsoft/Phi-3-vision-128k-instruct` | `download_hf_models.py` | Nie | `incompatible dependency stack in CI (DynamicCache.get_usable_length missing during export)` |
| VLM: Phi4MMForCausalLM | `microsoft/Phi-4-multimodal-instruct` | `download_hf_models.py` | Nie | `CI export environment misses required dependency 'peft' for this remote-code model` |
| VLM: Qwen2VLForConditionalGeneration | `Qwen/Qwen2-VL-2B-Instruct` | `download_hf_models.py` | Tak | --- |
| VLM: Qwen2_5_VLForConditionalGeneration | `Qwen/Qwen2.5-VL-3B-Instruct` | `download_hf_models.py` | Tak | --- |
| VLM: Gemma3ForConditionalGeneration | `google/gemma-3-4b-it`, `google/gemma-3-12b-it`, `google/gemma-3-27b-it` | `download_hf_models.py` | Nie | `gated repos require HF access token/approval in CI` |

## Uwagi

- Dla architektur oznaczonych jako `---` nie ma jeszcze dedykowanego skryptu w `scripts/download_models/`.