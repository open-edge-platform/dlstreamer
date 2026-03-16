# Supported Models

This page lists models supported by Intel® DL Streamer.

## Supported Architectures

DL Streamer supports the following model architectures from [GETI](https://docs.geti.intel.com/docs/user-guide/getting-started/use-geti/supported-models) and major model hubs such as [Ultralytics](https://www.ultralytics.com/) and [Hugging Face](https://huggingface.co/).

The table provides links to model preparation instructions describing download and conversion steps that can be performed either manually or by using dedicated scripts.

<table style="width: 100%; table-layout: fixed; margin-bottom: 1.5rem;">
  <colgroup>
    <col style="width: 16%;">
    <col style="width: 23%;">
    <col style="width: 19%;">
    <col style="width: 23%;">
    <col style="width: 19%;">
  </colgroup>
  <tr>
    <th>Category</th>
    <th>Architecture</th>
    <th>Model Preparation</th>
    <th>Example Model</th>
    <th>Demo App</th>
  </tr>
  <tr>
    <td rowspan="3" style="vertical-align:middle;">Anomaly Detection</td>
    <td>Padim</td>
    <td rowspan="3" style="vertical-align:middle;"><a href="https://docs.geti.intel.com/docs/user-guide/getting-started/use-geti/supported-models">GETI</a></td>
    <td rowspan="3" style="vertical-align:middle;">&nbsp;</td>
    <td rowspan="3" style="vertical-align:middle;"><a href="https://github.com/open-edge-platform/dlstreamer/tree/main/samples/gstreamer/gst_launch/geti_deployment"> GETI Deployment </a></td>
  </tr>
  <tr>
    <td>STFPM</td>
  </tr>
  <tr>
    <td>UFlow</td>
  </tr>
  <tr>
    <td rowspan="16" style="vertical-align:middle;">Detection</td>
    <td>YOLOv5u</td>
    <td rowspan="6" style="vertical-align:middle;"><a href="https://docs.ultralytics.com/integrations/openvino/">Ultralytics Exporter</a></td>
    <td><a href="https://github.com/ultralytics/assets/releases/download/v8.4.0/yolov5nu.pt">yolov5nu.pt</a></td>
    <td rowspan="6" style="vertical-align:middle;"><a href="https://github.com/open-edge-platform/dlstreamer/tree/main/samples/gstreamer/gst_launch/detection_with_yolo">Object Detection and Classification with YOLO</a></td>
  </tr>
  <tr>
    <td>YOLOv8</td>
    <td><a href="https://github.com/ultralytics/assets/releases/download/v8.4.0/yolov8n.pt">yolov8n.pt</a></td>
  </tr>
  <tr>
    <td>YOLOv9</td>
    <td><a href="https://github.com/ultralytics/assets/releases/download/v8.4.0/yolov9s.pt">yolov9s.pt</a></td>
  </tr>
  <tr>
    <td>YOLOv10</td>
    <td><a href="https://github.com/ultralytics/assets/releases/download/v8.4.0/yolov10n.pt">yolov10n.pt</a></td>
  </tr>
  <tr>
    <td>YOLO11</td>
    <td><a href="https://github.com/ultralytics/assets/releases/download/v8.4.0/yolo11n.pt">yolo11n.pt</a></td>
  </tr>
  <tr>
    <td>YOLO26</td>
    <td><a href="https://github.com/ultralytics/assets/releases/download/v8.4.0/yolo26n.pt">yolo26n.pt</a></td>
  </tr>
  <tr>
    <td>YOLOE-26</td>
    <td><a href="https://docs.ultralytics.com/integrations/openvino/">Ultralytics Exporter</a></td>
    <td><a href="https://github.com/ultralytics/assets/releases/download/v8.4.0/yoloe-26n-seg.pt">yoloe-26n-seg.pt</a></td>
    <td><a href="https://github.com/open-edge-platform/dlstreamer/tree/main/samples/gstreamer/python/prompted_detection">Prompt-based Object Detection</a></td>
  </tr>
  <tr>
    <td>RTDetrForObjectDetection</td>
    <td rowspan="2" style="vertical-align:middle;"><a href="https://huggingface.co/docs/optimum-onnx/onnx/usage_guides/export_a_model">Optimum-onnx</a><br>
    +<br>
    <a href="https://docs.openvino.ai/2026/openvino-workflow/model-preparation.html#convert-a-model-in-cli-ovc"> OpenVINO ovc </a>
    </td>
    <td><a href="https://huggingface.co/PekingU/rtdetr_r50vd">PekingU/rrtdetr_r50vd</a></td>
    <td rowspan="2" style="vertical-align:middle;"><a href="https://github.com/open-edge-platform/dlstreamer/tree/main/samples/gstreamer/python/smart_nvr">Smart Network Video Recorder for Lane Hogging Detection</a></td>
  </tr>
  <tr>
    <td>RtDetrV2ForObjectDetection</td>
    <td><a href="https://huggingface.co/PekingU/rtdetr_v2_r18vd">PekingU/rtdetr_v2_r18vd</a></td>
  </tr>
  <tr>
    <td>ATSS with ResNet or MobilenetV2</td>
    <td rowspan="5" style="vertical-align:middle;"><a href="https://docs.geti.intel.com/docs/user-guide/getting-started/use-geti/supported-models">GETI</a></td>
    <td rowspan="5" style="vertical-align:middle;">&nbsp;</td>
    <td rowspan="5" style="vertical-align:middle;">&nbsp;</td>
  </tr>
  <tr>
    <td>SSD with MobilenetV2</td>
  </tr>
  <tr>
    <td>RT-DETR</td>
  </tr>
  <tr>
    <td>YOLOX</td>
  </tr>
  <tr>
    <td>D-Fine</td>
  </tr>
  <tr>
    <td><a href="https://github.com/Star-Clouds/CenterFace/tree/master">CenterFace</a></td>
    <td><a href="https://github.com/open-edge-platform/dlstreamer/blob/main/samples/download_public_models.sh">download_public_models.sh</a></td>
    <td><a href="https://github.com/Star-Clouds/CenterFace/blob/master/models/onnx/centerface.onnx">centerface.onnx</a></td>
    <td><a href="https://github.com/open-edge-platform/dlstreamer/tree/main/samples/gstreamer/gst_launch/custom_postproc/classify">Custom Post-Processing Library Sample - Classification</a></td>
  </tr>
  <tr>
    <td><a href="https://github.com/WongKinYiu/yolov7">YOLOv7</a></td>
    <td><a href="https://github.com/open-edge-platform/dlstreamer/blob/main/samples/download_public_models.sh">download_public_models.sh</a><br>
        labels-file=<a href="https://github.com/open-edge-platform/dlstreamer/blob/main/samples/labels/coco_80cl.txt">coco_80cl.txt</a><br>
        model-proc=<a href="https://github.com/open-edge-platform/dlstreamer/blob/main/samples/gstreamer/model_proc/public/yolo-v7.json">yolo-v7.json</a>
    </td>
    <td><a href="https://github.com/WongKinYiu/yolov7/releases/download/v0.1/yolov7.pt">yolov7.pt</a></td>
    <td><a href="https://github.com/open-edge-platform/dlstreamer/tree/main/samples/gstreamer/gst_launch/detection_with_yolo">Object Detection and Classification with YOLO</a></td>
  </tr>
  <tr>
    <td>Emotion Recognition</td>
    <td><a href="https://github.com/av-savchenko/face-emotion-recognition/tree/main">HSEmotion</a></td>
    <td><a href="https://github.com/open-edge-platform/dlstreamer/blob/main/samples/download_public_models.sh">download_public_models.sh</a></td>
    <td><a href="https://github.com/sb-ai-lab/EmotiEffLib/blob/main/models/affectnet_emotions/onnx/enet_b0_8_va_mtl.onnx">enet_b0_8_va_mtl.onnx</a></td>
    <td><a href="https://github.com/open-edge-platform/dlstreamer/tree/main/samples/gstreamer/gst_launch/custom_postproc/classify">Custom Post-Processing Library Sample - Classification</a></td>
  </tr>
  <tr>
    <td>Feature Extraction</td>
    <td><a href="https://github.com/ZQPei/deep_sort_pytorch">Mars-small128</a></td>
    <td><a href="https://github.com/open-edge-platform/dlstreamer/blob/main/samples/download_public_models.sh">download_public_models.sh</a></td>
    <td>&nbsp;</td>
    <td><a href="https://github.com/open-edge-platform/dlstreamer/blob/main/docs/source/dev_guide/object_tracking.md#deep-sort-tracking">Deep SORT Tracking</a></td>
  </tr>
  <tr>
    <td rowspan="4" style="vertical-align:middle;">Image Classification</td>
    <td>ViTForImageClassification</td>
    <td><a href="https://huggingface.co/docs/optimum-intel/en/openvino/export">Optimum-Intel</a></td>
    <td><a href="https://huggingface.co/dima806/fairface_age_image_detection">dima806/fairface_age_image_detection</a></td>
    <td><a href="https://github.com/open-edge-platform/dlstreamer/tree/main/samples/gstreamer/python/face_detection_and_classification">Face Detection and Classification</a></td>
  </tr>
  <tr>
    <td>Mobilenet-V3</td>
    <td rowspan="3" style="vertical-align:middle;"><a href="https://docs.geti.intel.com/docs/user-guide/getting-started/use-geti/supported-models">GETI</a></td>
    <td rowspan="3" style="vertical-align:middle;">&nbsp;</td>
    <td rowspan="3" style="vertical-align:middle;">&nbsp;</td>
  </tr>
  <tr>
    <td>EfficientNet-B0</td>
  </tr>
  <tr>
    <td>DeitTiny</td>
  </tr>
  <tr>
    <td>Image Embeddings</td>
    <td>CLIPModel</td>
    <td><a href="https://docs.openedgeplatform.intel.com/dev/edge-ai-libraries/dlstreamer/dev_guide/lvms.html">CLIP ViT Conversion</a></td>
    <td><a href="https://huggingface.co/openai/clip-vit-large-patch14">openai/clip-vit-large-patch14</a></td>
    <td><a href="https://github.com/open-edge-platform/dlstreamer/blob/main/samples/gstreamer/gst_launch/lvm/README.md">Frame Embeddings</a></td>
  </tr>
  <tr>
    <td rowspan="5" style="vertical-align:middle;">Instance Segmentation</td>
    <td>YOLOv8-seg</td>
    <td rowspan="3" style="vertical-align:middle;"><a href="https://docs.ultralytics.com/integrations/openvino/">Ultralytics Exporter</a></td>
    <td><a href="https://github.com/ultralytics/assets/releases/download/v8.4.0/yolov8n-seg.pt">yolov8n-seg.pt</a></td>
    <td rowspan="3" style="vertical-align:middle;"><a href="https://github.com/open-edge-platform/dlstreamer/tree/main/samples/gstreamer/gst_launch/detection_with_yolo">Object Detection and Classification with YOLO</a></td>
  </tr>
  <tr>
    <td>YOLO11-seg</td>
    <td><a href="https://github.com/ultralytics/assets/releases/download/v8.4.0/yolo11n-seg.pt">yolo11n-seg.pt</a></td>
  </tr>
  <tr>
    <td>YOLO26-seg</td>
    <td><a href="https://github.com/ultralytics/assets/releases/download/v8.4.0/yolo26n-seg.pt">yolo26n-seg.pt</a></td>
  </tr>
  <tr>
    <td>MaskRCNN with EfficientNet, ResNet50, or Swin Transformer</td>
    <td rowspan="2" style="vertical-align:middle;"><a href="https://docs.geti.intel.com/docs/user-guide/getting-started/use-geti/supported-models">GETI</a></td>
    <td rowspan="2" style="vertical-align:middle;">&nbsp;</td>
    <td rowspan="2" style="vertical-align:middle;">&nbsp;</td>
  </tr>
  <tr>
    <td>RTMDet</td>
  </tr>
  <tr>
    <td>Optical Character Recognition</td>
    <td><a href="https://github.com/PaddlePaddle/PaddleOCR">Paddle OCRv4</a></td>
    <td><a href="https://github.com/open-edge-platform/dlstreamer/blob/main/samples/download_public_models.sh">download_public_models.sh</a></td>
    <td>ch_PP-OCRv4_rec_infer</td>
    <td><a href="https://github.com/open-edge-platform/dlstreamer/tree/main/samples/gstreamer/gst_launch/license_plate_recognition">License Plate Recognition Sample</a></td>
  </tr>
  <tr>
    <td rowspan="3" style="vertical-align:middle;">Oriented Detection</td>
    <td>YOLOv8-obb</td>
    <td rowspan="3" style="vertical-align:middle;"><a href="https://docs.ultralytics.com/integrations/openvino/">Ultralytics Exporter</a></td>
    <td><a href="https://github.com/ultralytics/assets/releases/download/v8.4.0/yolov8n-obb.pt">yolov8n-obb.pt</a></td>
    <td rowspan="3" style="vertical-align:middle;"><a href="https://github.com/open-edge-platform/dlstreamer/tree/main/samples/gstreamer/gst_launch/detection_with_yolo">Object Detection and Classification with YOLO</a></td>
  </tr>
  <tr>
    <td>YOLO11-obb</td>
    <td><a href="https://github.com/ultralytics/assets/releases/download/v8.4.0/yolo11n-obb.pt">yolo11n-obb.pt</a></td>
  </tr>
  <tr>
    <td>YOLO26-obb</td>
    <td><a href="https://github.com/ultralytics/assets/releases/download/v8.4.0/yolo26n-obb.pt">yolo26n-obb.pt</a></td>
  </tr>
  <tr>
    <td rowspan="3" style="vertical-align:middle;">Pose Estimation</td>
    <td>YOLOv8-pose</td>
    <td rowspan="3" style="vertical-align:middle;"><a href="https://docs.ultralytics.com/integrations/openvino/">Ultralytics Exporter</a></td>
    <td><a href="https://github.com/ultralytics/assets/releases/download/v8.4.0/yolov8n-pose.pt">yolov8n-pose.pt</a></td>
    <td rowspan="3" style="vertical-align:middle;"><a href="https://github.com/open-edge-platform/dlstreamer/tree/main/samples/gstreamer/gst_launch/detection_with_yolo">Object Detection and Classification with YOLO</a></td>
  </tr>
  <tr>
    <td>YOLO11-pose</td>
    <td><a href="https://github.com/ultralytics/assets/releases/download/v8.4.0/yolo11n-pose.pt">yolo11n-pose.pt</a></td>
  </tr>
  <tr>
    <td>YOLO26-pose</td>
    <td><a href="https://github.com/ultralytics/assets/releases/download/v8.4.0/yolo26n-pose.pt">yolo26n-pose.pt</a></td>
  </tr>
  <tr>
    <td rowspan="3" style="vertical-align:middle;">Semantic Segmentation</td>
    <td>Lite-HRNet</td>
    <td rowspan="3" style="vertical-align:middle;"><a href="https://docs.geti.intel.com/docs/user-guide/getting-started/use-geti/supported-models">GETI</a></td>
    <td rowspan="3" style="vertical-align:middle;">&nbsp;</td>
    <td rowspan="3" style="vertical-align:middle;">&nbsp;</td>
  </tr>
  <tr>
    <td>SegNext</td>
  </tr>
  <tr>
    <td>DinoV2</td>
  </tr>
  <tr>
    <td>Speech Recognition</td>
    <td>WhisperForConditionalGeneration</td>
    <td><a href="https://huggingface.co/docs/optimum-intel/en/openvino/export">Optimum-Intel</a></td>
    <td><a href="https://huggingface.co/openai/whisper-tiny">openai/whisper-tiny</a></td>
    <td><a href="https://github.com/open-edge-platform/dlstreamer/tree/main/samples/gstreamer/gst_launch/audio_transcribe">Audio Transcription</a></td>
  </tr>
  <tr>
    <td rowspan="13" style="vertical-align:middle;">VLM</td>
    <td>InternVLChatModel</td>
    <td rowspan="13" style="vertical-align:middle;"><a href="https://huggingface.co/docs/optimum-intel/en/openvino/export">Optimum-Intel</a></td>
    <td><a href="https://huggingface.co/OpenGVLab/InternVL2-1B">OpenGVLab/InternVL2-1B</a></td>
    <td rowspan="13" style="vertical-align:middle;"><a href="https://github.com/open-edge-platform/dlstreamer/tree/main/samples/gstreamer/gst_launch/gvagenai">Gvagenai Demo</a></td>
  </tr>
  <tr>
    <td>LlavaForConditionalGeneration</td>
    <td><a href="https://huggingface.co/llava-hf/llava-1.5-7b-hf">llava-hf/llava-1.5-7b-hf</a></td>
  </tr>
  <tr>
    <td>LlavaQwen2ForCausalLM</td>
    <td><a href="https://huggingface.co/qnguyen3/nanoLLaVA">qnguyen3/nanoLLaVA</a></td>
  </tr>
  <tr>
    <td>BunnyQwenForCausalLM</td>
    <td><a href="https://huggingface.co/qnguyen3/nanoLLaVA-1.5">qnguyen3/nanoLLaVA-1.5</a></td>
  </tr>
  <tr>
    <td>LlavaNextForConditionalGeneration</td>
    <td><a href="https://huggingface.co/llava-hf/llava-v1.6-mistral-7b-hf">llava-hf/llava-v1.6-mistral-7b-hf</a></td>
  </tr>
  <tr>
    <td>LlavaNextVideoForConditionalGeneration</td>
    <td><a href="https://huggingface.co/llava-hf/LLaVA-NeXT-Video-7B-hf">llava-hf/LLaVA-NeXT-Video-7B-hf</a></td>
  </tr>
  <tr>
    <td>MiniCPMO</td>
    <td><a href="https://huggingface.co/openbmb/MiniCPM-o-2_6">openbmb/MiniCPM-o-2_6</a></td>
  </tr>
  <tr>
    <td>MiniCPMV</td>
    <td><a href="https://huggingface.co/openbmb/MiniCPM-V-2_6">openbmb/MiniCPM-V-2_6</a></td>
  </tr>
  <tr>
    <td>Phi3VForCausalLM</td>
    <td><a href="https://huggingface.co/microsoft/Phi-3-vision-128k-instruct">microsoft/Phi-3-vision-128k-instruct</a></td>
  </tr>
  <tr>
    <td>Phi4MMForCausalLM</td>
    <td><a href="https://huggingface.co/microsoft/Phi-4-multimodal-instruct">microsoft/Phi-4-multimodal-instruct</a></td>
  </tr>
  <tr>
    <td>Qwen2VLForConditionalGeneration</td>
    <td><a href="https://huggingface.co/Qwen/Qwen2-VL-2B-Instruct">Qwen/Qwen2-VL-2B-Instruct</a></td>
  </tr>
  <tr>
    <td>Qwen2_5_VLForConditionalGeneration</td>
    <td><a href="https://huggingface.co/Qwen/Qwen2.5-VL-3B-Instruct">Qwen/Qwen2.5-VL-3B-Instruct</a></td>
  </tr>
  <tr>
    <td>Gemma3ForConditionalGeneration</td>
    <td><a href="https://huggingface.co/google/gemma-3-4b-it">google/gemma-3-4b-it</a></td>
  </tr>
</table>

## OMZ Models

The table below lists supported models from [OpenVINO™ Open Model Zoo](https://github.com/openvinotoolkit/open_model_zoo/). These models can be downloaded using the [`download_omz_models.sh`](https://github.com/open-edge-platform/dlstreamer/blob/main/samples/download_omz_models.sh) script:

<table style="width: 100%; table-layout: fixed; margin-top: 1rem;">
  <colgroup>
    <col style="width: 16%;">
    <col style="width: 34%;">
    <col style="width: 14%;">
    <col style="width: 16%;">
    <col style="width: 20%;">
  </colgroup>
  <tr>
    <th>Category</th>
    <th>Model Name</th>
    <th>labels-file</th>
    <th>model-proc</th>
    <th>Demo App</th>
  </tr>
  <tr>
    <td style="vertical-align:top;" rowspan="3">Action Recognition</td>
    <td><a href="https://github.com/openvinotoolkit/open_model_zoo/tree/master/models/intel/action-recognition-0001">action-recognition-0001</a></td>
    <td><a href="https://github.com/open-edge-platform/dlstreamer/blob/main/samples/labels/kinetics_400.txt">kinetics_400.txt</a></td>
    <td>&nbsp;</td>
    <td rowspan="3"><a href="https://github.com/openvinotoolkit/open_model_zoo/tree/master/demos/action_recognition_demo/python">Action Recognition Demo</a></td>
  </tr>
  <tr>
    <td><a href="https://github.com/openvinotoolkit/open_model_zoo/tree/master/models/intel/driver-action-recognition-adas-0002">driver-action-recognition-adas-0002</a></td>
    <td><a href="https://github.com/open-edge-platform/dlstreamer/blob/main/samples/labels/driver_actions.txt">driver_actions.txt</a></td>
    <td>&nbsp;</td>
  </tr>
  <tr>
    <td><a href="https://github.com/openvinotoolkit/open_model_zoo/tree/master/models/intel/weld-porosity-detection-0001">weld-porosity-detection-0001</a></td>
    <td>&nbsp;</td>
    <td><a href="https://github.com/open-edge-platform/dlstreamer/blob/main/samples/gstreamer/model_proc/intel/weld-porosity-detection-0001.json">weld-porosity-detection-0001.json</a></td>
  </tr>
  <tr>
    <td style="vertical-align:top;" rowspan="41">Classification</td>
    <td><a href="https://github.com/openvinotoolkit/open_model_zoo/tree/master/models/public/anti-spoof-mn3">anti-spoof-mn3</a></td>
    <td>&nbsp;</td>
    <td><a href="https://github.com/open-edge-platform/dlstreamer/blob/main/samples/gstreamer/model_proc/public/anti-spoof-mn3.json">anti-spoof-mn3.json</a></td>
    <td><a href="https://github.com/openvinotoolkit/open_model_zoo/tree/master/demos/interactive_face_detection_demo/cpp_gapi">Interactive Face Detection Demo</a></td>
  </tr>
  <tr>
    <td><a href="https://github.com/openvinotoolkit/open_model_zoo/tree/master/models/public/densenet-121-tf">densenet-121-tf</a></td>
    <td rowspan="6"><a href="https://github.com/open-edge-platform/dlstreamer/blob/main/samples/labels/imagenet_2012.txt">imagenet_2012.txt</a></td>
    <td rowspan="6"><a href="https://github.com/open-edge-platform/dlstreamer/blob/main/samples/gstreamer/model_proc/public/preproc-aspect-ratio.json">preproc-aspect-ratio.json</a></td>
    <td rowspan="6"><a href="https://github.com/openvinotoolkit/open_model_zoo/tree/master/demos/classification_demo/python">Classification Demo</a></td>
  </tr>
  <tr>
    <td><a href="https://github.com/openvinotoolkit/open_model_zoo/tree/master/models/public/dla-34">dla-34</a></td>
  </tr>
  <tr>
    <td><a href="https://github.com/openvinotoolkit/open_model_zoo/tree/master/models/public/efficientnet-b0">efficientnet-b0</a></td>
  </tr>
  <tr>
    <td><a href="https://github.com/openvinotoolkit/open_model_zoo/tree/master/models/public/efficientnet-b0-pytorch">efficientnet-b0-pytorch</a></td>
  </tr>
  <tr>
    <td><a href="https://github.com/openvinotoolkit/open_model_zoo/tree/master/models/public/efficientnet-v2-b0">efficientnet-v2-b0</a></td>
  </tr>
  <tr>
    <td><a href="https://github.com/openvinotoolkit/open_model_zoo/tree/master/models/public/efficientnet-v2-s">efficientnet-v2-s</a></td>
  </tr>
  <tr>
    <td><a href="https://github.com/onnx/models/tree/main/validated/vision/body_analysis/emotion_ferplus">emotion-ferplus-8</a></td>
    <td>&nbsp;</td>
    <td><a href="https://github.com/open-edge-platform/dlstreamer/blob/main/samples/gstreamer/model_proc/onnx/emotion-ferplus-8.json">emotion-ferplus-8.json</a></td>
    <td>&nbsp;</td>
  </tr>
  <tr>
    <td><a href="https://github.com/openvinotoolkit/open_model_zoo/tree/master/models/public/googlenet-v1-tf">googlenet-v1-tf</a></td>
    <td><a href="https://github.com/open-edge-platform/dlstreamer/blob/main/samples/labels/imagenet_2012.txt">imagenet_2012.txt</a></td>
    <td rowspan="16"><a href="https://github.com/open-edge-platform/dlstreamer/blob/main/samples/gstreamer/model_proc/public/preproc-aspect-ratio.json">preproc-aspect-ratio.json</a></td>
    <td rowspan="16"><a href="https://github.com/openvinotoolkit/open_model_zoo/tree/master/demos/classification_demo/python">Classification Demo</a></td>
  </tr>
  <tr>
    <td><a href="https://github.com/openvinotoolkit/open_model_zoo/tree/master/models/public/googlenet-v2-tf">googlenet-v2-tf</a></td>
    <td rowspan="2"><a href="https://github.com/open-edge-platform/dlstreamer/blob/main/samples/labels/imagenet_2012_bkgr.txt">imagenet_2012_bkgr.txt</a></td>
  </tr>
  <tr>
    <td><a href="https://github.com/openvinotoolkit/open_model_zoo/tree/master/models/public/googlenet-v3">googlenet-v3</a></td>
  </tr>
  <tr>
    <td><a href="https://github.com/openvinotoolkit/open_model_zoo/tree/master/models/public/googlenet-v3-pytorch">googlenet-v3-pytorch</a></td>
    <td><a href="https://github.com/open-edge-platform/dlstreamer/blob/main/samples/labels/imagenet_2012.txt">imagenet_2012.txt</a></td>
  </tr>
  <tr>
    <td><a href="https://github.com/openvinotoolkit/open_model_zoo/tree/master/models/public/googlenet-v4-tf">googlenet-v4-tf</a></td>
    <td><a href="https://github.com/open-edge-platform/dlstreamer/blob/main/samples/labels/imagenet_2012_bkgr.txt">imagenet_2012_bkgr.txt</a></td>
  </tr>
  <tr>
    <td><a href="https://github.com/openvinotoolkit/open_model_zoo/tree/master/models/public/hbonet-0.25">hbonet-0.25</a></td>
    <td rowspan="2"><a href="https://github.com/open-edge-platform/dlstreamer/blob/main/samples/labels/imagenet_2012.txt">imagenet_2012.txt</a></td>
  </tr>
  <tr>
    <td><a href="https://github.com/openvinotoolkit/open_model_zoo/tree/master/models/public/hbonet-1.0">hbonet-1.0</a></td>
  </tr>
  <tr>
    <td><a href="https://github.com/openvinotoolkit/open_model_zoo/tree/master/models/public/inception-resnet-v2-tf">inception-resnet-v2-tf</a></td>
    <td><a href="https://github.com/open-edge-platform/dlstreamer/blob/main/samples/labels/imagenet_2012_bkgr.txt">imagenet_2012_bkgr.txt</a></td>
  </tr>
  <tr>
    <td><a href="https://github.com/openvinotoolkit/open_model_zoo/tree/master/models/public/mixnet-l">mixnet-l</a></td>
    <td><a href="https://github.com/open-edge-platform/dlstreamer/blob/main/samples/labels/imagenet_2012.txt">imagenet_2012.txt</a></td>
  </tr>
  <tr>
    <td><a href="https://github.com/openvinotoolkit/open_model_zoo/tree/master/models/public/mobilenet-v1-0.25-128">mobilenet-v1-0.25-128</a></td>
    <td rowspan="4"><a href="https://github.com/open-edge-platform/dlstreamer/blob/main/samples/labels/imagenet_2012_bkgr.txt">imagenet_2012_bkgr.txt</a></td>
  </tr>
  <tr>
    <td><a href="https://github.com/openvinotoolkit/open_model_zoo/tree/master/models/public/mobilenet-v1-1.0-224-tf">mobilenet-v1-1.0-224-tf</a></td>
  </tr>
  <tr>
    <td><a href="https://github.com/openvinotoolkit/open_model_zoo/tree/master/models/public/mobilenet-v2-1.0-224">mobilenet-v2-1.0-224</a></td>
  </tr>
  <tr>
    <td><a href="https://github.com/openvinotoolkit/open_model_zoo/tree/master/models/public/mobilenet-v2-1.4-224">mobilenet-v2-1.4-224</a></td>
  </tr>
  <tr>
    <td><a href="https://github.com/openvinotoolkit/open_model_zoo/tree/master/models/public/mobilenet-v2-pytorch">mobilenet-v2-pytorch</a></td>
    <td rowspan="3"><a href="https://github.com/open-edge-platform/dlstreamer/blob/main/samples/labels/imagenet_2012.txt">imagenet_2012.txt</a></td>
  </tr>
  <tr>
    <td><a href="https://github.com/openvinotoolkit/open_model_zoo/tree/master/models/public/mobilenet-v3-large-1.0-224-tf">mobilenet-v3-large-1.0-224-tf</a></td>
  </tr>
  <tr>
    <td><a href="https://github.com/openvinotoolkit/open_model_zoo/tree/master/models/public/mobilenet-v3-small-1.0-224-tf">mobilenet-v3-small-1.0-224-tf</a></td>
  </tr>
  <tr>
    <td><a href="https://github.com/onnx/models/tree/main/validated/vision/classification/mobilenet">mobilenetv2-7</a></td>
    <td>&nbsp;</td>
    <td><a href="https://github.com/open-edge-platform/dlstreamer/blob/main/samples/gstreamer/model_proc/onnx/mobilenetv2-7.json">mobilenetv2-7.json</a></td>
    <td>&nbsp;</td>
  </tr>
  <tr>
    <td><a href="https://github.com/openvinotoolkit/open_model_zoo/tree/master/models/public/nfnet-f0">nfnet-f0</a></td>
    <td><a href="https://github.com/open-edge-platform/dlstreamer/blob/main/samples/labels/imagenet_2012.txt">imagenet_2012.txt</a></td>
    <td><a href="https://github.com/open-edge-platform/dlstreamer/blob/main/samples/gstreamer/model_proc/public/preproc-aspect-ratio.json">preproc-aspect-ratio.json</a></td>
    <td><a href="https://github.com/openvinotoolkit/open_model_zoo/tree/master/demos/classification_demo/python">Classification Demo</a></td>
  </tr>
  <tr>
    <td><a href="https://github.com/openvinotoolkit/open_model_zoo/tree/master/models/public/open-closed-eye-0001">open-closed-eye-0001</a></td>
    <td>&nbsp;</td>
    <td><a href="https://github.com/open-edge-platform/dlstreamer/blob/main/samples/gstreamer/model_proc/public/open-closed-eye-0001.json">open-closed-eye-0001.json</a></td>
    <td><a href="https://github.com/openvinotoolkit/open_model_zoo/tree/master/demos/gaze_estimation_demo/cpp_gapi">Gaze Estimation Demo</a></td>
  </tr>
  <tr>
    <td><a href="https://github.com/openvinotoolkit/open_model_zoo/tree/master/models/public/regnetx-3.2gf">regnetx-3.2gf</a></td>
    <td rowspan="8"><a href="https://github.com/open-edge-platform/dlstreamer/blob/main/samples/labels/imagenet_2012.txt">imagenet_2012.txt</a></td>
    <td rowspan="9"><a href="https://github.com/open-edge-platform/dlstreamer/blob/main/samples/gstreamer/model_proc/public/preproc-aspect-ratio.json">preproc-aspect-ratio.json</a></td>
    <td rowspan="14"><a href="https://github.com/openvinotoolkit/open_model_zoo/tree/master/demos/classification_demo/python">Classification Demo</a></td>
  </tr>
  <tr>
    <td><a href="https://github.com/openvinotoolkit/open_model_zoo/tree/master/models/public/repvgg-a0">repvgg-a0</a></td>
  </tr>
  <tr>
    <td><a href="https://github.com/openvinotoolkit/open_model_zoo/tree/master/models/public/repvgg-b1">repvgg-b1</a></td>
  </tr>
  <tr>
    <td><a href="https://github.com/openvinotoolkit/open_model_zoo/tree/master/models/public/repvgg-b3">repvgg-b3</a></td>
  </tr>
  <tr>
    <td><a href="https://github.com/openvinotoolkit/open_model_zoo/tree/master/models/public/resnest-50-pytorch">resnest-50-pytorch</a></td>
  </tr>
  <tr>
    <td><a href="https://github.com/openvinotoolkit/open_model_zoo/tree/master/models/public/resnet-18-pytorch">resnet-18-pytorch</a></td>
  </tr>
  <tr>
    <td><a href="https://github.com/openvinotoolkit/open_model_zoo/tree/master/models/public/resnet-34-pytorch">resnet-34-pytorch</a></td>
  </tr>
  <tr>
    <td><a href="https://github.com/openvinotoolkit/open_model_zoo/tree/master/models/public/resnet-50-pytorch">resnet-50-pytorch</a></td>
  </tr>
  <tr>
    <td><a href="https://github.com/openvinotoolkit/open_model_zoo/tree/master/models/public/resnet-50-tf">resnet-50-tf</a></td>
    <td><a href="https://github.com/open-edge-platform/dlstreamer/blob/main/samples/labels/imagenet_2012_bkgr.txt">imagenet_2012_bkgr.txt</a></td>
  </tr>
  <tr>
    <td><a href="https://github.com/openvinotoolkit/open_model_zoo/tree/master/models/intel/resnet18-xnor-binary-onnx-0001">resnet18-xnor-binary-onnx-0001</a></td>
    <td>&nbsp;</td>
    <td><a href="https://github.com/open-edge-platform/dlstreamer/blob/main/samples/gstreamer/model_proc/intel/resnet18-xnor-binary-onnx-0001.json">resnet18-xnor-binary-onnx-0001.json</a></td>
  </tr>
  <tr>
    <td><a href="https://github.com/openvinotoolkit/open_model_zoo/tree/master/models/intel/resnet50-binary-0001">resnet50-binary-0001</a></td>
    <td>&nbsp;</td>
    <td><a href="https://github.com/open-edge-platform/dlstreamer/blob/main/samples/gstreamer/model_proc/intel/resnet50-binary-0001.json">resnet50-binary-0001.json</a></td>
  </tr>
  <tr>
    <td><a href="https://github.com/openvinotoolkit/open_model_zoo/tree/master/models/public/rexnet-v1-x1.0">rexnet-v1-x1.0</a></td>
    <td rowspan="3"><a href="https://github.com/open-edge-platform/dlstreamer/blob/main/samples/labels/imagenet_2012.txt">imagenet_2012.txt</a></td>
    <td rowspan="3"><a href="https://github.com/open-edge-platform/dlstreamer/blob/main/samples/gstreamer/model_proc/public/preproc-aspect-ratio.json">preproc-aspect-ratio.json</a></td>
  </tr>
  <tr>
    <td><a href="https://github.com/openvinotoolkit/open_model_zoo/tree/master/models/public/shufflenet-v2-x1.0">shufflenet-v2-x1.0</a></td>
  </tr>
  <tr>
    <td><a href="https://github.com/openvinotoolkit/open_model_zoo/tree/master/models/public/swin-tiny-patch4-window7-224">swin-tiny-patch4-window7-224</a></td>
  </tr>
  <tr>
    <td style="vertical-align:top;" rowspan="47">Detection</td>
    <td><a href="https://github.com/openvinotoolkit/open_model_zoo/tree/master/models/public/efficientdet-d0-tf">efficientdet-d0-tf</a></td>
    <td rowspan="2"><a href="https://github.com/open-edge-platform/dlstreamer/blob/main/samples/labels/coco_91cl.txt">coco_91cl.txt</a></td>
    <td>&nbsp;</td>
    <td rowspan="12"><a href="https://github.com/openvinotoolkit/open_model_zoo/tree/master/demos/object_detection_demo/cpp">Object Detection Demo</a></td>
  </tr>
  <tr>
    <td><a href="https://github.com/openvinotoolkit/open_model_zoo/tree/master/models/public/efficientdet-d1-tf">efficientdet-d1-tf</a></td>
    <td>&nbsp;</td>
  </tr>
  <tr>
    <td><a href="https://github.com/openvinotoolkit/open_model_zoo/tree/master/models/intel/face-detection-0200">face-detection-0200</a></td>
    <td>&nbsp;</td>
    <td><a href="https://github.com/open-edge-platform/dlstreamer/blob/main/samples/gstreamer/model_proc/intel/face-detection-0200.json">face-detection-0200.json</a></td>
  </tr>
  <tr>
    <td><a href="https://github.com/openvinotoolkit/open_model_zoo/tree/master/models/intel/face-detection-0202">face-detection-0202</a></td>
    <td>&nbsp;</td>
    <td><a href="https://github.com/open-edge-platform/dlstreamer/blob/main/samples/gstreamer/model_proc/intel/face-detection-0202.json">face-detection-0202.json</a></td>
  </tr>
  <tr>
    <td><a href="https://github.com/openvinotoolkit/open_model_zoo/tree/master/models/intel/face-detection-0204">face-detection-0204</a></td>
    <td>&nbsp;</td>
    <td><a href="https://github.com/open-edge-platform/dlstreamer/blob/main/samples/gstreamer/model_proc/intel/face-detection-0204.json">face-detection-0204.json</a></td>
  </tr>
  <tr>
    <td><a href="https://github.com/openvinotoolkit/open_model_zoo/tree/master/models/intel/face-detection-0205">face-detection-0205</a></td>
    <td>&nbsp;</td>
    <td><a href="https://github.com/open-edge-platform/dlstreamer/blob/main/samples/gstreamer/model_proc/intel/face-detection-0205.json">face-detection-0205.json</a></td>
  </tr>
  <tr>
    <td><a href="https://github.com/openvinotoolkit/open_model_zoo/tree/master/models/intel/face-detection-0206">face-detection-0206</a></td>
    <td>&nbsp;</td>
    <td><a href="https://github.com/open-edge-platform/dlstreamer/blob/main/samples/gstreamer/model_proc/intel/face-detection-0206.json">face-detection-0206.json</a></td>
  </tr>
  <tr>
    <td><a href="https://github.com/openvinotoolkit/open_model_zoo/tree/master/models/intel/face-detection-adas-0001">face-detection-adas-0001</a></td>
    <td>&nbsp;</td>
    <td><a href="https://github.com/open-edge-platform/dlstreamer/blob/main/samples/gstreamer/model_proc/intel/face-detection-adas-0001.json">face-detection-adas-0001.json</a></td>
  </tr>
  <tr>
    <td><a href="https://github.com/openvinotoolkit/open_model_zoo/tree/master/models/intel/face-detection-retail-0004">face-detection-retail-0004</a></td>
    <td>&nbsp;</td>
    <td><a href="https://github.com/open-edge-platform/dlstreamer/blob/main/samples/gstreamer/model_proc/intel/face-detection-retail-0004.json">face-detection-retail-0004.json</a></td>
  </tr>
  <tr>
    <td><a href="https://github.com/openvinotoolkit/open_model_zoo/tree/master/models/intel/face-detection-retail-0005">face-detection-retail-0005</a></td>
    <td>&nbsp;</td>
    <td><a href="https://github.com/open-edge-platform/dlstreamer/blob/main/samples/gstreamer/model_proc/intel/face-detection-retail-0005.json">face-detection-retail-0005.json</a></td>
  </tr>
  <tr>
    <td><a href="https://github.com/openvinotoolkit/open_model_zoo/tree/master/models/public/faster_rcnn_inception_resnet_v2_atrous_coco">faster_rcnn_inception_resnet_v2_atrous_coco</a></td>
    <td rowspan="2"><a href="https://github.com/open-edge-platform/dlstreamer/blob/main/samples/labels/coco_91cl_bkgr.txt">coco_91cl_bkgr.txt</a></td>
    <td rowspan="2"><a href="https://github.com/open-edge-platform/dlstreamer/blob/main/samples/gstreamer/model_proc/public/preproc-image-info.json">preproc-image-info.json</a></td>
  </tr>
  <tr>
    <td><a href="https://github.com/openvinotoolkit/open_model_zoo/tree/master/models/public/faster_rcnn_resnet50_coco">faster_rcnn_resnet50_coco</a></td>
  </tr>
  <tr>
    <td><a href="https://github.com/openvinotoolkit/open_model_zoo/tree/master/models/intel/horizontal-text-detection-0001">horizontal-text-detection-0001</a></td>
    <td>&nbsp;</td>
    <td><a href="https://github.com/open-edge-platform/dlstreamer/blob/main/samples/gstreamer/model_proc/intel/horizontal-text-detection-0001.json">horizontal-text-detection-0001.json</a></td>
    <td><a href="https://github.com/openvinotoolkit/open_model_zoo/tree/master/demos/text_detection_demo/cpp">Text Detection Demo</a></td>
  </tr>
  <tr>
    <td><a href="https://github.com/openvinotoolkit/open_model_zoo/tree/master/models/public/mobilenet-yolo-v4-syg">mobilenet-yolo-v4-syg</a></td>
    <td>&nbsp;</td>
    <td><a href="https://github.com/open-edge-platform/dlstreamer/blob/main/samples/gstreamer/model_proc/public/mobilenet-yolo-v4-syg.json">mobilenet-yolo-v4-syg.json</a></td>
    <td rowspan="23"><a href="https://github.com/openvinotoolkit/open_model_zoo/tree/master/demos/object_detection_demo/cpp">Object Detection Demo</a></td>
  </tr>
  <tr>
    <td><a href="https://github.com/openvinotoolkit/open_model_zoo/tree/master/models/intel/pedestrian-and-vehicle-detector-adas-0001">pedestrian-and-vehicle-detector-adas-0001</a></td>
    <td>&nbsp;</td>
    <td><a href="https://github.com/open-edge-platform/dlstreamer/blob/main/samples/gstreamer/model_proc/intel/pedestrian-and-vehicle-detector-adas-0001.json">pedestrian-and-vehicle-detector-adas-0001.json</a></td>
  </tr>
  <tr>
    <td><a href="https://github.com/openvinotoolkit/open_model_zoo/tree/master/models/intel/pedestrian-detection-adas-0002">pedestrian-detection-adas-0002</a></td>
    <td>&nbsp;</td>
    <td><a href="https://github.com/open-edge-platform/dlstreamer/blob/main/samples/gstreamer/model_proc/intel/pedestrian-detection-adas-0002.json">pedestrian-detection-adas-0002.json</a></td>
  </tr>
  <tr>
    <td><a href="https://github.com/openvinotoolkit/open_model_zoo/tree/master/models/intel/person-detection-0200">person-detection-0200</a></td>
    <td>&nbsp;</td>
    <td><a href="https://github.com/open-edge-platform/dlstreamer/blob/main/samples/gstreamer/model_proc/intel/person-detection-0200.json">person-detection-0200.json</a></td>
  </tr>
  <tr>
    <td><a href="https://github.com/openvinotoolkit/open_model_zoo/tree/master/models/intel/person-detection-0201">person-detection-0201</a></td>
    <td>&nbsp;</td>
    <td><a href="https://github.com/open-edge-platform/dlstreamer/blob/main/samples/gstreamer/model_proc/intel/person-detection-0201.json">person-detection-0201.json</a></td>
  </tr>
  <tr>
    <td><a href="https://github.com/openvinotoolkit/open_model_zoo/tree/master/models/intel/person-detection-0202">person-detection-0202</a></td>
    <td>&nbsp;</td>
    <td><a href="https://github.com/open-edge-platform/dlstreamer/blob/main/samples/gstreamer/model_proc/intel/person-detection-0202.json">person-detection-0202.json</a></td>
  </tr>
  <tr>
    <td><a href="https://github.com/openvinotoolkit/open_model_zoo/tree/master/models/intel/person-detection-0203">person-detection-0203</a></td>
    <td>&nbsp;</td>
    <td rowspan="2"><a href="https://github.com/open-edge-platform/dlstreamer/blob/main/samples/gstreamer/model_proc/intel/person-detection-0203.json">person-detection-0203.json</a></td>
  </tr>
  <tr>
    <td><a href="https://github.com/openvinotoolkit/open_model_zoo/tree/master/models/intel/person-detection-asl-0001">person-detection-asl-0001</a></td>
    <td>&nbsp;</td>
  </tr>
  <tr>
    <td><a href="https://github.com/openvinotoolkit/open_model_zoo/tree/master/models/intel/person-detection-retail-0013">person-detection-retail-0013</a></td>
    <td>&nbsp;</td>
    <td><a href="https://github.com/open-edge-platform/dlstreamer/blob/main/samples/gstreamer/model_proc/intel/person-detection-retail-0013.json">person-detection-retail-0013.json</a></td>
  </tr>
  <tr>
    <td><a href="https://github.com/openvinotoolkit/open_model_zoo/tree/master/models/intel/person-vehicle-bike-detection-2000">person-vehicle-bike-detection-2000</a></td>
    <td>&nbsp;</td>
    <td><a href="https://github.com/open-edge-platform/dlstreamer/blob/main/samples/gstreamer/model_proc/intel/person-vehicle-bike-detection-2000.json">person-vehicle-bike-detection-2000.json</a></td>
  </tr>
  <tr>
    <td><a href="https://github.com/openvinotoolkit/open_model_zoo/tree/master/models/intel/person-vehicle-bike-detection-2001">person-vehicle-bike-detection-2001</a></td>
    <td>&nbsp;</td>
    <td><a href="https://github.com/open-edge-platform/dlstreamer/blob/main/samples/gstreamer/model_proc/intel/person-vehicle-bike-detection-2001.json">person-vehicle-bike-detection-2001.json</a></td>
  </tr>
  <tr>
    <td><a href="https://github.com/openvinotoolkit/open_model_zoo/tree/master/models/intel/person-vehicle-bike-detection-2002">person-vehicle-bike-detection-2002</a></td>
    <td>&nbsp;</td>
    <td><a href="https://github.com/open-edge-platform/dlstreamer/blob/main/samples/gstreamer/model_proc/intel/person-vehicle-bike-detection-2002.json">person-vehicle-bike-detection-2002.json</a></td>
  </tr>
  <tr>
    <td><a href="https://github.com/openvinotoolkit/open_model_zoo/tree/master/models/intel/person-vehicle-bike-detection-2003">person-vehicle-bike-detection-2003</a></td>
    <td>&nbsp;</td>
    <td><a href="https://github.com/open-edge-platform/dlstreamer/blob/main/samples/gstreamer/model_proc/intel/person-vehicle-bike-detection-2003.json">person-vehicle-bike-detection-2003.json</a></td>
  </tr>
  <tr>
    <td><a href="https://github.com/openvinotoolkit/open_model_zoo/tree/master/models/intel/person-vehicle-bike-detection-2004">person-vehicle-bike-detection-2004</a></td>
    <td>&nbsp;</td>
    <td><a href="https://github.com/open-edge-platform/dlstreamer/blob/main/samples/gstreamer/model_proc/intel/person-vehicle-bike-detection-2004.json">person-vehicle-bike-detection-2004.json</a></td>
  </tr>
  <tr>
    <td><a href="https://github.com/openvinotoolkit/open_model_zoo/tree/master/models/intel/person-vehicle-bike-detection-crossroad-0078">person-vehicle-bike-detection-crossroad-0078</a></td>
    <td>&nbsp;</td>
    <td><a href="https://github.com/open-edge-platform/dlstreamer/blob/main/samples/gstreamer/model_proc/intel/person-vehicle-bike-detection-crossroad-0078.json">person-vehicle-bike-detection-crossroad-0078.json</a></td>
  </tr>
  <tr>
    <td><a href="https://github.com/openvinotoolkit/open_model_zoo/tree/master/models/intel/person-vehicle-bike-detection-crossroad-1016">person-vehicle-bike-detection-crossroad-1016</a></td>
    <td>&nbsp;</td>
    <td><a href="https://github.com/open-edge-platform/dlstreamer/blob/main/samples/gstreamer/model_proc/intel/person-vehicle-bike-detection-crossroad-1016.json">person-vehicle-bike-detection-crossroad-1016.json</a></td>
  </tr>
  <tr>
    <td><a href="https://github.com/openvinotoolkit/open_model_zoo/tree/master/models/intel/person-vehicle-bike-detection-crossroad-yolov3-1020">person-vehicle-bike-detection-crossroad-yolov3-1020</a></td>
    <td>&nbsp;</td>
    <td><a href="https://github.com/open-edge-platform/dlstreamer/blob/main/samples/gstreamer/model_proc/intel/person-vehicle-bike-detection-crossroad-yolov3-1020.json">person-vehicle-bike-detection-crossroad-yolov3-1020.json</a></td>
  </tr>
  <tr>
    <td><a href="https://github.com/openvinotoolkit/open_model_zoo/tree/master/models/intel/product-detection-0001">product-detection-0001</a></td>
    <td>&nbsp;</td>
    <td><a href="https://github.com/open-edge-platform/dlstreamer/blob/main/samples/gstreamer/model_proc/intel/product-detection-0001.json">product-detection-0001.json</a></td>
  </tr>
  <tr>
    <td><a href="https://github.com/openvinotoolkit/open_model_zoo/tree/master/models/public/retinanet-tf">retinanet-tf</a></td>
    <td><a href="https://github.com/open-edge-platform/dlstreamer/blob/main/samples/labels/coco_80cl.txt">coco_80cl.txt</a></td>
    <td>&nbsp;</td>
  </tr>
  <tr>
    <td><a href="https://github.com/openvinotoolkit/open_model_zoo/tree/master/models/public/rfcn-resnet101-coco-tf">rfcn-resnet101-coco-tf</a></td>
    <td rowspan="4"><a href="https://github.com/open-edge-platform/dlstreamer/blob/main/samples/labels/coco_91cl_bkgr.txt">coco_91cl_bkgr.txt</a></td>
    <td><a href="https://github.com/open-edge-platform/dlstreamer/blob/main/samples/gstreamer/model_proc/public/preproc-image-info.json">preproc-image-info.json</a></td>
  </tr>
  <tr>
    <td><a href="https://github.com/openvinotoolkit/open_model_zoo/tree/master/models/public/ssd_mobilenet_v1_coco">ssd_mobilenet_v1_coco</a></td>
    <td>&nbsp;</td>
  </tr>
  <tr>
    <td><a href="https://github.com/openvinotoolkit/open_model_zoo/tree/master/models/public/ssd_mobilenet_v1_fpn_coco">ssd_mobilenet_v1_fpn_coco</a></td>
    <td>&nbsp;</td>
  </tr>
  <tr>
    <td><a href="https://github.com/openvinotoolkit/open_model_zoo/tree/master/models/public/ssdlite_mobilenet_v2">ssdlite_mobilenet_v2</a></td>
    <td>&nbsp;</td>
  </tr>
  <tr>
    <td><a href="https://pytorch.org/vision/main/models/generated/torchvision.models.detection.ssdlite320_mobilenet_v3_large.html">torchvision.models.detection.ssdlite320_mobilenet_v3_large</a></td>
    <td><a href="https://github.com/open-edge-platform/dlstreamer/blob/main/samples/labels/coco_80cl.txt">coco_80cl.txt</a></td>
    <td>&nbsp;</td>
    <td>&nbsp;</td>
  </tr>
  <tr>
    <td><a href="https://github.com/openvinotoolkit/open_model_zoo/tree/master/models/intel/vehicle-detection-0200">vehicle-detection-0200</a></td>
    <td>&nbsp;</td>
    <td><a href="https://github.com/open-edge-platform/dlstreamer/blob/main/samples/gstreamer/model_proc/intel/vehicle-detection-0200.json">vehicle-detection-0200.json</a></td>
    <td rowspan="4"><a href="https://github.com/openvinotoolkit/open_model_zoo/tree/master/demos/object_detection_demo/cpp">Object Detection Demo</a></td>
  </tr>
  <tr>
    <td><a href="https://github.com/openvinotoolkit/open_model_zoo/tree/master/models/intel/vehicle-detection-0201">vehicle-detection-0201</a></td>
    <td>&nbsp;</td>
    <td><a href="https://github.com/open-edge-platform/dlstreamer/blob/main/samples/gstreamer/model_proc/intel/vehicle-detection-0201.json">vehicle-detection-0201.json</a></td>
  </tr>
  <tr>
    <td><a href="https://github.com/openvinotoolkit/open_model_zoo/tree/master/models/intel/vehicle-detection-0202">vehicle-detection-0202</a></td>
    <td>&nbsp;</td>
    <td><a href="https://github.com/open-edge-platform/dlstreamer/blob/main/samples/gstreamer/model_proc/intel/vehicle-detection-0202.json">vehicle-detection-0202.json</a></td>
  </tr>
  <tr>
    <td><a href="https://github.com/openvinotoolkit/open_model_zoo/tree/master/models/intel/vehicle-detection-adas-0002">vehicle-detection-adas-0002</a></td>
    <td>&nbsp;</td>
    <td><a href="https://github.com/open-edge-platform/dlstreamer/blob/main/samples/gstreamer/model_proc/intel/vehicle-detection-adas-0002.json">vehicle-detection-adas-0002.json</a></td>
  </tr>
  <tr>
    <td><a href="https://github.com/openvinotoolkit/open_model_zoo/tree/master/models/intel/vehicle-license-plate-detection-barrier-0106">vehicle-license-plate-detection-barrier-0106</a></td>
    <td>&nbsp;</td>
    <td><a href="https://github.com/open-edge-platform/dlstreamer/blob/main/samples/gstreamer/model_proc/intel/vehicle-license-plate-detection-barrier-0106.json">vehicle-license-plate-detection-barrier-0106.json</a></td>
    <td rowspan="2"><a href="https://github.com/openvinotoolkit/open_model_zoo/tree/master/demos/security_barrier_camera_demo/cpp">Security Barrier Camera Demo</a></td>
  </tr>
  <tr>
    <td><a href="https://github.com/openvinotoolkit/open_model_zoo/tree/master/models/public/vehicle-license-plate-detection-barrier-0123">vehicle-license-plate-detection-barrier-0123</a></td>
    <td>&nbsp;</td>
    <td><a href="https://github.com/open-edge-platform/dlstreamer/blob/main/samples/gstreamer/model_proc/public/vehicle-license-plate-detection-barrier-0123.json">vehicle-license-plate-detection-barrier-0123.json</a></td>
  </tr>
  <tr>
    <td><a href="https://github.com/openvinotoolkit/open_model_zoo/tree/master/models/public/yolo-v3-tf">yolo-v3-tf</a></td>
    <td rowspan="4"><a href="https://github.com/open-edge-platform/dlstreamer/blob/main/samples/labels/coco_80cl.txt">coco_80cl.txt</a></td>
    <td><a href="https://github.com/open-edge-platform/dlstreamer/blob/main/samples/gstreamer/model_proc/public/yolo-v3-tf.json">yolo-v3-tf.json</a></td>
    <td rowspan="4"><a href="https://github.com/openvinotoolkit/open_model_zoo/tree/master/demos/object_detection_demo/cpp">Object Detection Demo</a></td>
  </tr>
  <tr>
    <td><a href="https://github.com/openvinotoolkit/open_model_zoo/tree/master/models/public/yolo-v3-tiny-tf">yolo-v3-tiny-tf</a></td>
    <td><a href="https://github.com/open-edge-platform/dlstreamer/blob/main/samples/gstreamer/model_proc/public/yolo-v3-tiny-tf.json">yolo-v3-tiny-tf.json</a></td>
  </tr>
  <tr>
    <td><a href="https://github.com/openvinotoolkit/open_model_zoo/tree/master/models/public/yolo-v4-tf">yolo-v4-tf</a></td>
    <td><a href="https://github.com/open-edge-platform/dlstreamer/blob/main/samples/gstreamer/model_proc/public/yolo-v4-tf.json">yolo-v4-tf.json</a></td>
  </tr>
  <tr>
    <td><a href="https://github.com/openvinotoolkit/open_model_zoo/tree/master/models/public/yolo-v4-tiny-tf">yolo-v4-tiny-tf</a></td>
    <td><a href="https://github.com/open-edge-platform/dlstreamer/blob/main/samples/gstreamer/model_proc/public/yolo-v4-tiny-tf.json">yolo-v4-tiny-tf.json</a></td>
  </tr>
  <tr>
    <td style="vertical-align:top;">Head Pose Estimation</td>
    <td><a href="https://github.com/openvinotoolkit/open_model_zoo/tree/master/models/intel/head-pose-estimation-adas-0001">head-pose-estimation-adas-0001</a></td>
    <td>&nbsp;</td>
    <td>&nbsp;</td>
    <td><a href="https://github.com/openvinotoolkit/open_model_zoo/tree/master/demos/gaze_estimation_demo/cpp_gapi">Gaze Estimation Demo</a></td>
  </tr>
  <tr>
    <td style="vertical-align:top;" rowspan="2">Human Pose Estimation</td>
    <td><a href="https://github.com/openvinotoolkit/open_model_zoo/tree/master/models/intel/human-pose-estimation-0001">human-pose-estimation-0001</a></td>
    <td>&nbsp;</td>
    <td><a href="https://github.com/open-edge-platform/dlstreamer/blob/main/samples/gstreamer/model_proc/intel/human-pose-estimation-0001.json">human-pose-estimation-0001.json</a></td>
    <td><a href="https://github.com/openvinotoolkit/open_model_zoo/tree/master/demos/multi_channel_human_pose_estimation_demo/cpp">Multi Channel Human Pose Estimation Demo</a></td>
  </tr>
  <tr>
    <td><a href="https://github.com/openvinotoolkit/open_model_zoo/tree/master/models/public/single-human-pose-estimation-0001">single-human-pose-estimation-0001</a></td>
    <td>&nbsp;</td>
    <td><a href="https://github.com/open-edge-platform/dlstreamer/blob/main/samples/gstreamer/model_proc/public/single-human-pose-estimation-0001.json">single-human-pose-estimation-0001.json</a></td>
    <td><a href="https://github.com/openvinotoolkit/open_model_zoo/tree/master/demos/single_human_pose_estimation_demo/python">Single Human Pose Estimation Demo</a></td>
  </tr>
  <tr>
    <td style="vertical-align:top;" rowspan="8">Instance Segmentation</td>
    <td><a href="https://github.com/openvinotoolkit/open_model_zoo/tree/master/models/intel/instance-segmentation-person-0007">instance-segmentation-person-0007</a></td>
    <td>&nbsp;</td>
    <td>&nbsp;</td>
    <td rowspan="6"><a href="https://github.com/openvinotoolkit/open_model_zoo/tree/master/demos/background_subtraction_demo/cpp_gapi">Background Subtraction Demo</a></td>
  </tr>
  <tr>
    <td><a href="https://github.com/openvinotoolkit/open_model_zoo/tree/master/models/intel/instance-segmentation-security-0002">instance-segmentation-security-0002</a></td>
    <td rowspan="5"><a href="https://github.com/open-edge-platform/dlstreamer/blob/main/samples/labels/coco_80cl.txt">coco_80cl.txt</a></td>
    <td>&nbsp;</td>
  </tr>
  <tr>
    <td><a href="https://github.com/openvinotoolkit/open_model_zoo/tree/master/models/intel/instance-segmentation-security-0091">instance-segmentation-security-0091</a></td>
    <td>&nbsp;</td>
  </tr>
  <tr>
    <td><a href="https://github.com/openvinotoolkit/open_model_zoo/tree/master/models/intel/instance-segmentation-security-0228">instance-segmentation-security-0228</a></td>
    <td>&nbsp;</td>
  </tr>
  <tr>
    <td><a href="https://github.com/openvinotoolkit/open_model_zoo/tree/master/models/intel/instance-segmentation-security-1039">instance-segmentation-security-1039</a></td>
    <td>&nbsp;</td>
  </tr>
  <tr>
    <td><a href="https://github.com/openvinotoolkit/open_model_zoo/tree/master/models/intel/instance-segmentation-security-1040">instance-segmentation-security-1040</a></td>
    <td>&nbsp;</td>
  </tr>
  <tr>
    <td><a href="https://github.com/openvinotoolkit/open_model_zoo/tree/master/models/public/mask_rcnn_inception_resnet_v2_atrous_coco">mask_rcnn_inception_resnet_v2_atrous_coco</a></td>
    <td>&nbsp;</td>
    <td rowspan="2"><a href="https://github.com/open-edge-platform/dlstreamer/blob/main/samples/gstreamer/model_proc/public/mask-rcnn.json">mask-rcnn.json</a></td>
    <td rowspan="2"><a href="https://github.com/openvinotoolkit/open_model_zoo/tree/master/demos/mask_rcnn_demo/cpp">Mask RCNN Demo</a></td>
  </tr>
  <tr>
    <td><a href="https://github.com/openvinotoolkit/open_model_zoo/tree/master/models/public/mask_rcnn_resnet50_atrous_coco">mask_rcnn_resnet50_atrous_coco</a></td>
    <td>&nbsp;</td>
  </tr>
  <tr>
    <td style="vertical-align:top;" rowspan="10">Object Attributes</td>
    <td><a href="https://github.com/openvinotoolkit/open_model_zoo/tree/master/models/intel/age-gender-recognition-retail-0013">age-gender-recognition-retail-0013</a></td>
    <td>&nbsp;</td>
    <td><a href="https://github.com/open-edge-platform/dlstreamer/blob/main/samples/gstreamer/model_proc/intel/age-gender-recognition-retail-0013.json">age-gender-recognition-retail-0013.json</a></td>
    <td rowspan="2"><a href="https://github.com/openvinotoolkit/open_model_zoo/tree/master/demos/interactive_face_detection_demo/cpp_gapi">Interactive Face Detection Demo</a></td>
  </tr>
  <tr>
    <td><a href="https://github.com/openvinotoolkit/open_model_zoo/tree/master/models/intel/emotions-recognition-retail-0003">emotions-recognition-retail-0003</a></td>
    <td>&nbsp;</td>
    <td><a href="https://github.com/open-edge-platform/dlstreamer/blob/main/samples/gstreamer/model_proc/intel/emotions-recognition-retail-0003.json">emotions-recognition-retail-0003.json</a></td>
  </tr>
  <tr>
    <td><a href="https://github.com/openvinotoolkit/open_model_zoo/tree/master/models/intel/facial-landmarks-35-adas-0002">facial-landmarks-35-adas-0002</a></td>
    <td>&nbsp;</td>
    <td><a href="https://github.com/open-edge-platform/dlstreamer/blob/main/samples/gstreamer/model_proc/intel/facial-landmarks-35-adas-0002.json">facial-landmarks-35-adas-0002.json</a></td>
    <td><a href="https://github.com/openvinotoolkit/open_model_zoo/tree/master/demos/gaze_estimation_demo/cpp_gapi">Gaze Estimation Demo</a></td>
  </tr>
  <tr>
    <td><a href="https://github.com/openvinotoolkit/open_model_zoo/tree/master/models/intel/facial-landmarks-98-detection-0001">facial-landmarks-98-detection-0001</a></td>
    <td>&nbsp;</td>
    <td><a href="https://github.com/open-edge-platform/dlstreamer/blob/main/samples/gstreamer/model_proc/intel/facial-landmarks-98-detection-0001.json">facial-landmarks-98-detection-0001.json</a></td>
    <td><a href="https://github.com/openvinotoolkit/open_model_zoo/tree/master/demos/gaze_estimation_demo/cpp">Gaze Estimation Demo</a></td>
  </tr>
  <tr>
    <td><a href="https://github.com/openvinotoolkit/open_model_zoo/tree/master/models/intel/landmarks-regression-retail-0009">landmarks-regression-retail-0009</a></td>
    <td>&nbsp;</td>
    <td><a href="https://github.com/open-edge-platform/dlstreamer/blob/main/samples/gstreamer/model_proc/intel/landmarks-regression-retail-0009.json">landmarks-regression-retail-0009.json</a></td>
    <td><a href="https://github.com/openvinotoolkit/open_model_zoo/tree/master/demos/face_recognition_demo/python">Face Recognition Demo</a></td>
  </tr>
  <tr>
    <td><a href="https://github.com/openvinotoolkit/open_model_zoo/tree/master/models/intel/person-attributes-recognition-crossroad-0230">person-attributes-recognition-crossroad-0230</a></td>
    <td>&nbsp;</td>
    <td><a href="https://github.com/open-edge-platform/dlstreamer/blob/main/samples/gstreamer/model_proc/intel/person-attributes-recognition-crossroad-0230.json">person-attributes-recognition-crossroad-0230.json</a></td>
    <td rowspan="3"><a href="https://github.com/openvinotoolkit/open_model_zoo/tree/master/demos/crossroad_camera_demo/cpp">Crossroad Camera Demo</a></td>
  </tr>
  <tr>
    <td><a href="https://github.com/openvinotoolkit/open_model_zoo/tree/master/models/intel/person-attributes-recognition-crossroad-0234">person-attributes-recognition-crossroad-0234</a></td>
    <td>&nbsp;</td>
    <td><a href="https://github.com/open-edge-platform/dlstreamer/blob/main/samples/gstreamer/model_proc/intel/person-attributes-recognition-crossroad-0234.json">person-attributes-recognition-crossroad-0234.json</a></td>
  </tr>
  <tr>
    <td><a href="https://github.com/openvinotoolkit/open_model_zoo/tree/master/models/intel/person-attributes-recognition-crossroad-0238">person-attributes-recognition-crossroad-0238</a></td>
    <td>&nbsp;</td>
    <td><a href="https://github.com/open-edge-platform/dlstreamer/blob/main/samples/gstreamer/model_proc/intel/person-attributes-recognition-crossroad-0238.json">person-attributes-recognition-crossroad-0238.json</a></td>
  </tr>
  <tr>
    <td><a href="https://github.com/openvinotoolkit/open_model_zoo/tree/master/models/intel/vehicle-attributes-recognition-barrier-0039">vehicle-attributes-recognition-barrier-0039</a></td>
    <td>&nbsp;</td>
    <td><a href="https://github.com/open-edge-platform/dlstreamer/blob/main/samples/gstreamer/model_proc/intel/vehicle-attributes-recognition-barrier-0039.json">vehicle-attributes-recognition-barrier-0039.json</a></td>
    <td rowspan="2"><a href="https://github.com/openvinotoolkit/open_model_zoo/tree/master/demos/security_barrier_camera_demo/cpp">Security Barrier Camera Demo</a></td>
  </tr>
  <tr>
    <td><a href="https://github.com/openvinotoolkit/open_model_zoo/tree/master/models/intel/vehicle-attributes-recognition-barrier-0042">vehicle-attributes-recognition-barrier-0042</a></td>
    <td>&nbsp;</td>
    <td><a href="https://github.com/open-edge-platform/dlstreamer/blob/main/samples/gstreamer/model_proc/intel/vehicle-attributes-recognition-barrier-0042.json">vehicle-attributes-recognition-barrier-0042.json</a></td>
  </tr>
  <tr>
    <td style="vertical-align:top;">Optical Character Recognition</td>
    <td><a href="https://github.com/openvinotoolkit/open_model_zoo/tree/master/models/public/license-plate-recognition-barrier-0007">license-plate-recognition-barrier-0007</a></td>
    <td>&nbsp;</td>
    <td><a href="https://github.com/open-edge-platform/dlstreamer/blob/main/samples/gstreamer/model_proc/intel/license-plate-recognition-barrier-0007.json">license-plate-recognition-barrier-0007.json</a></td>
    <td><a href="https://github.com/openvinotoolkit/open_model_zoo/tree/master/demos/security_barrier_camera_demo/cpp">Security Barrier Camera Demo</a></td>
  </tr>
  <tr>
    <td style="vertical-align:top;">Sound Classification</td>
    <td><a href="https://github.com/openvinotoolkit/open_model_zoo/tree/master/models/public/aclnet">aclnet</a></td>
    <td>&nbsp;</td>
    <td><a href="https://github.com/open-edge-platform/dlstreamer/blob/main/samples/gstreamer/model_proc/public/aclnet.json">aclnet.json</a></td>
    <td><a href="https://github.com/openvinotoolkit/open_model_zoo/tree/master/demos/sound_classification_demo/python">Sound Classification Demo</a></td>
  </tr>
</table>

## Legal Information

Ultralytics, Hugging Face, PyTorch, TensorFlow, PaddlePaddle, Caffe, Keras, and MXNet are trademarks or brand names of their respective owners.
All company, product, and service names used on this website are for identification purposes only.
Use of these names, trademarks, and brands does not imply endorsement.
