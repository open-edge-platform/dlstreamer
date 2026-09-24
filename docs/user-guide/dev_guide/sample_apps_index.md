# Available Sample Apps

This page indexes all DL Streamer samples by use case. See
[Using Sample Apps](./using_sample_apps.md) for installation, model download, and run
instructions before trying any of the samples below.

Each entry below is a mini "card": name, a one-line preview thumbnail (when available), what it
demonstrates, the elements/models it uses, and its language — `CLI` (`gst-launch` command line),
`Python`, or `C++`.

---

## Browse by category

[Object detection, classification & segmentation (9)](#object-detection-classification-segmentation) ·
[Object tracking & analytics (3)](#object-tracking-analytics) ·
[Vision-Language Models (VLM) & GenAI (5)](#vision-language-models-vlm-genai) ·
[Audio analytics (2)](#audio-analytics) ·
[3D: LiDAR & radar (5)](#3d-lidar-radar) ·
[Cameras & input sources (4)](#cameras-input-sources) ·
[Metadata: publishing, access & visualization (8)](#metadata-publishing-access-visualization) ·
[Customization & extensibility (8)](#customization-extensibility) ·
[Performance & benchmarking (2)](#performance-benchmarking) ·
[Interoperability (2)](#interoperability) ·
[Auto-generated reference applications (8)](#auto-generated-reference-applications)

> **Tip:** Use your browser's find-in-page (Ctrl+F / Cmd+F) to search across all samples by name,
> element (e.g. `gvadetect`), or model (e.g. `yolo11n`).

<!-- Shared card styling (GitHub strips this <style> block harmlessly; Sphinx applies it) -->
<style>
.sample-card { display:flex; gap:1rem; align-items:center; margin:0.5rem 0; padding-bottom:0.5rem; border-bottom:1px solid #4443; }
.sample-thumb { flex:0 0 96px; max-width:96px; height:auto; }
.sample-body { flex:1; line-height:1.35; }
.sample-body p { margin:0.15rem 0; }
.sample-body p:first-child { margin-top:0; }
.sample-body p:last-child { margin-bottom:0; font-size:0.9rem; color:#888; }
</style>

---

### Object detection, classification & segmentation

| | Sample | Description | Elements | Models |
|---|---|---|---|---|
| <img src="../_images/sample-detection-with-yolo-thumb.jpg" width="72"> | **[Detection with YOLO](https://github.com/open-edge-platform/dlstreamer/tree/main/samples/gstreamer/gst_launch/detection_with_yolo)** `CLI` | Object detection and classification with publicly available YOLO models. | `gvadetect`, `gvaclassify` | `yolox_s` (default; many YOLO variants) |
| <img src="../_images/sample_app_template.jpg" width="72"> | **[Face Detection and Classification](https://github.com/open-edge-platform/dlstreamer/tree/main/samples/gstreamer/gst_launch/face_detection_and_classification)** `CLI` | Detect faces and estimate age, gender, emotions and facial landmarks. | `gvadetect`, `gvaclassify` | `centerface`, `dima806_facial_age_image_detection`, `dima806_fairface_gender_image_detection`, `dima806_face_emotions_image_detection` |
| <img src="../_images/sample-instance-segmentation-thumb.jpg" width="72"> | **[Instance Segmentation](https://github.com/open-edge-platform/dlstreamer/tree/main/samples/gstreamer/gst_launch/instance_segmentation)** `CLI` | Instance segmentation via the `object_detect` and `object_classify` bin elements. | `object_detect`, `object_classify` | `yolo26s-seg` (default; also `yolo11s-seg`) |
| <img src="../_images/sample_app_template.jpg" width="72"> | **[Human Pose Estimation](https://github.com/open-edge-platform/dlstreamer/tree/main/samples/gstreamer/gst_launch/human_pose_estimation)** `CLI` | Full-frame human pose estimation. | `gvaclassify` | `yolo26s-pose` |
| <img src="../_images/sample_app_template.jpg" width="72"> | **[Depth Estimation](https://github.com/open-edge-platform/dlstreamer/tree/main/samples/gstreamer/gst_launch/depth_estimation)** `CLI` | YOLO11n detection followed by Depth Anything V2 depth estimation on detected regions. | `gvadetect`, `gvainference` | `yolo11n`, `Depth-Anything-V2-Small-hf` |
| <img src="../_images/sample_app_template.jpg" width="72"> | **[License Plate Recognition](https://github.com/open-edge-platform/dlstreamer/tree/main/samples/gstreamer/gst_launch/license_plate_recognition)** `CLI` | YOLO detector combined with an optical character recognition model. | `gvadetect`, `gvainference` | `yolov8` license-plate detector, `PP-OCRv4` |
| <img src="../_images/sample-prompted-detection-thumb.jpg" width="72"> | **[Prompt-based Object Detection](https://github.com/open-edge-platform/dlstreamer/tree/main/samples/gstreamer/python/prompted_detection)** `Python` | Search a video for user-defined objects using an open-vocabulary model (YOLOE). | `gvadetect` | `yoloe-26s-seg` (text-prompt, class baked in at export) |
| <img src="../_images/sample-geti-deployment-thumb.jpg" width="72"> | **[Deployment of Geti™ models](https://github.com/open-edge-platform/dlstreamer/tree/main/samples/gstreamer/gst_launch/geti_deployment)** `CLI` | Deploy Geti™-trained models for detection, anomaly detection and classification. | `gvadetect`, `gvaclassify` | Geti™-trained (Padim / STFPM / UFlow) |
| <img src="../_images/sample_app_template.jpg" width="72"> | **[Motion Detect](https://github.com/open-edge-platform/dlstreamer/tree/main/samples/gstreamer/gst_launch/motion_detect)** `CLI` | Run detection only over motion ROIs (GPU and CPU paths). | `gvamotiondetect`, `gvadetect` | `yolov8n` |

### Object tracking & analytics

<div class="sample-card">
<img class="sample-thumb" src="../_images/sample_app_template.jpg" alt="Vehicle and Pedestrian Tracking" width="96">
<div class="sample-body">

**[Vehicle and Pedestrian Tracking](https://github.com/open-edge-platform/dlstreamer/tree/main/samples/gstreamer/gst_launch/vehicle_pedestrian_tracking)** `CLI`

Object tracking across frames.

**Elements:** `gvatrack`, `gvadetect`, `gvaclassify` &nbsp;|&nbsp; **Models:** `yolo26s`, `dima806_vehicle_10_types_image_detection`

</div>
</div>

<div class="sample-card">
<img class="sample-thumb" src="../_images/sample-gvaanalytics-tripwire-thumb.jpg" alt="Vehicle Counter with gvaanalytics Tripwires" width="96">
<div class="sample-body">

**[Vehicle Counter with gvaanalytics Tripwires](https://github.com/open-edge-platform/dlstreamer/tree/main/samples/gstreamer/python/gvaanalytics_tripwire)** `Python`

Count vehicles crossing a virtual line in both directions using tripwires.

**Elements:** `gvaanalytics`, `gvatrack` &nbsp;|&nbsp; **Models:** `yolo11n`

</div>
</div>

<div class="sample-card">
<img class="sample-thumb" src="../_images/sample-smart-nvr-thumb.jpg" alt="Smart NVR for Lane Hogging Detection" width="96">
<div class="sample-body">

**[Smart NVR for Lane Hogging Detection](https://github.com/open-edge-platform/dlstreamer/tree/main/samples/gstreamer/python/smart_nvr)** `Python`

Build an NVR with custom analytics and video storage to detect lane-hogging events.

**Elements:** `gvaanalytics_py`, `gvarecorder_py` &nbsp;|&nbsp; **Models:** `rtdetr_v2_r50vd` (RT-DETRv2)

</div>
</div>

### Vision-Language Models (VLM) & GenAI

<div class="sample-card">
<img class="sample-thumb" src="../_images/sample_app_template.jpg" alt="Using VLM Models with gvagenai" width="96">
<div class="sample-body">

**[Using VLM Models with gvagenai](https://github.com/open-edge-platform/dlstreamer/tree/main/samples/gstreamer/gst_launch/gvagenai)** `CLI`

Video summarization with MiniCPM-V.

**Elements:** `gvagenai` &nbsp;|&nbsp; **Models:** `MiniCPM-V`, `Phi-4-multimodal-instruct` or `Gemma-3`

</div>
</div>

<div class="sample-card">
<img class="sample-thumb" src="../_images/sample-vlm-alerts-thumb.jpg" alt="VLM Alerts" width="96">
<div class="sample-body">

**[VLM Alerts](https://github.com/open-edge-platform/dlstreamer/tree/main/samples/gstreamer/python/vlm_alerts)** `Python`

Edge alerting pipeline that generates structured JSON alerts per frame with annotated video.

**Elements:** `gvagenai` &nbsp;|&nbsp; **Models:** Configurable VLM (e.g. `Qwen2.5-VL`, `InternVL`)

</div>
</div>

<div class="sample-card">
<img class="sample-thumb" src="../_images/sample-vlm-self-checkout-thumb.jpg" alt="VLM-assisted Self Checkout" width="96">
<div class="sample-body">

**[VLM-assisted Self Checkout](https://github.com/open-edge-platform/dlstreamer/tree/main/samples/gstreamer/python/vlm_self_checkout)** `Python`

Combine CV object detection with a VLM for item classification, running both locally on edge.

**Elements:** `gvadetect`, `gvagenai` &nbsp;|&nbsp; **Models:** `yolo26s`, `MiniCPM-V-4_5`

</div>
</div>

<div class="sample-card">
<img class="sample-thumb" src="../_images/sample-onvif-camera-analytics-validation-thumb.jpg" alt="ONVIF Camera Analytics Validation" width="96">
<div class="sample-body">

**[ONVIF Camera Analytics Validation](https://github.com/open-edge-platform/dlstreamer/tree/main/samples/gstreamer/python/onvif_camera_analytics_validation)** `Python`

Use a VLM as an additional validation layer for ONVIF-enabled analytics cameras.

**Elements:** `gvagenai` &nbsp;|&nbsp; **Models:** Configurable VLM

</div>
</div>

<div class="sample-card">
<img class="sample-thumb" src="../_images/sample_app_template.jpg" alt="Image Embeddings Generation with ViT" width="96">
<div class="sample-body">

**[Image Embeddings Generation with ViT](https://github.com/open-edge-platform/dlstreamer/tree/main/samples/gstreamer/gst_launch/lvm)** `CLI`

Generate image embeddings using the Vision Transformer component of a CLIP model.

**Elements:** `gvainference` &nbsp;|&nbsp; **Models:** `clip-vit-large-patch14` (CLIP ViT)

</div>
</div>

### Audio analytics

<div class="sample-card">
<img class="sample-thumb" src="../_images/sample_app_template.jpg" alt="Audio Event Detection" width="96">
<div class="sample-body">

**[Audio Event Detection](https://github.com/open-edge-platform/dlstreamer/tree/main/samples/gstreamer/gst_launch/audio_detect)** `CLI`

Audio event detection, converting results to JSON.

**Elements:** `gvaaudiodetect`, `gvametaconvert`, `gvametapublish` &nbsp;|&nbsp; **Models:** `aclnet`

</div>
</div>

<div class="sample-card">
<img class="sample-thumb" src="../_images/sample_app_template.jpg" alt="Audio Transcription" width="96">
<div class="sample-body">

**[Audio Transcription](https://github.com/open-edge-platform/dlstreamer/tree/main/samples/gstreamer/gst_launch/audio_transcribe)** `CLI`

Speech transcription using an OpenVINO GenAI Whisper model.

**Elements:** `gvaaudiotranscribe` &nbsp;|&nbsp; **Models:** `whisper`

</div>
</div>

### 3D: LiDAR & radar

<div class="sample-card">
<img class="sample-thumb" src="../_images/sample_app_template.jpg" alt="PointPillars Inference with g3dinference" width="96">
<div class="sample-body">

**[PointPillars Inference with g3dinference](https://github.com/open-edge-platform/dlstreamer/tree/main/samples/gstreamer/gst_launch/g3dinference)** `CLI`

Complete LiDAR-only 3D detection pipeline.

**Elements:** `g3dlidarparse`, `g3dinference` &nbsp;|&nbsp; **Models:** `PointPillars`

</div>
</div>

<div class="sample-card">
<img class="sample-thumb" src="../_images/sample_app_template.jpg" alt="LiDAR Parse" width="96">
<div class="sample-body">

**[LiDAR Parse](https://github.com/open-edge-platform/dlstreamer/tree/main/samples/gstreamer/gst_launch/g3dlidarparse)** `CLI`

LiDAR parsing pipeline.

**Elements:** `g3dlidarparse` &nbsp;|&nbsp; **Models:** — (parsing only)

</div>
</div>

<div class="sample-card">
<img class="sample-thumb" src="../_images/sample_app_template.jpg" alt="Live LiDAR Capture" width="96">
<div class="sample-body">

**[Live LiDAR Capture](https://github.com/open-edge-platform/dlstreamer/tree/main/samples/gstreamer/gst_launch/g3dlidarsrc)** `CLI`

Real-time LiDAR capture from a physical device (RoboSense via rs_driver).

**Elements:** `g3dlidarsrc`, `g3dinference` &nbsp;|&nbsp; **Models:** `PointPillars`

</div>
</div>

<div class="sample-card">
<img class="sample-thumb" src="../_images/sample_app_template.jpg" alt="Camera + 3D Object Fusion" width="96">
<div class="sample-body">

**[Camera + 3D Object Fusion](https://github.com/open-edge-platform/dlstreamer/tree/main/samples/gstreamer/gst_launch/g3dobjectfuser)** `CLI`

Fuse 2D camera detections with 3D LiDAR detections.

**Elements:** `g3dobjectfuser`, `gvastreammux` &nbsp;|&nbsp; **Models:** `yolo11n`, `PointPillars`

</div>
</div>

<div class="sample-card">
<img class="sample-thumb" src="../_images/sample_app_template.jpg" alt="Radar Signal Process" width="96">
<div class="sample-body">

**[Radar Signal Process](https://github.com/open-edge-platform/dlstreamer/tree/main/samples/gstreamer/gst_launch/g3dradarprocess)** `CLI`

mmWave radar signal processing with point-cloud detection, clustering and tracking.

**Elements:** `g3dradarprocess` &nbsp;|&nbsp; **Models:** — (signal processing)

</div>
</div>

### Cameras & input sources

<div class="sample-card">
<img class="sample-thumb" src="../_images/sample_app_template.jpg" alt="RealSense™ Camera" width="96">
<div class="sample-body">

**[RealSense™ Camera](https://github.com/open-edge-platform/dlstreamer/tree/main/samples/gstreamer/gst_launch/gvarealsense)** `CLI`

Capture a video stream from a 3D Intel RealSense™ Depth Camera.

**Elements:** `gvarealsense` &nbsp;|&nbsp; **Models:** — (capture only)

</div>
</div>

<div class="sample-card">
<img class="sample-thumb" src="../_images/sample-onvif-cameras-discovery-thumb.jpg" alt="ONVIF Camera Discovery" width="96">
<div class="sample-body">

**[ONVIF Camera Discovery](https://github.com/open-edge-platform/dlstreamer/tree/main/samples/gstreamer/python/onvif_cameras_discovery)** `Python`

Automatically discover ONVIF cameras on the network and launch pipelines for each.

**Elements:** `gvadetect` &nbsp;|&nbsp; **Models:** Configurable detector

</div>
</div>

<div class="sample-card">
<img class="sample-thumb" src="../_images/sample_app_template.jpg" alt="Multi-camera deployments" width="96">
<div class="sample-body">

**[Multi-camera deployments](https://github.com/open-edge-platform/dlstreamer/tree/main/samples/gstreamer/gst_launch/multi_stream)** `CLI`

Handle video streams from multiple cameras in a single application.

**Elements:** `gvadetect`, `gvafpscounter` &nbsp;|&nbsp; **Models:** `yolo11s` (many YOLO variants)

</div>
</div>

<div class="sample-card">
<img class="sample-thumb" src="../_images/sample_app_template.jpg" alt="Multi-Stream Mux/Demux" width="96">
<div class="sample-body">

**[Multi-Stream Mux/Demux](https://github.com/open-edge-platform/dlstreamer/tree/main/samples/gstreamer/gst_launch/stream_mux_and_demux)** `CLI`

Share a single inference pipeline across streams with per-source routing.

**Elements:** `gvastreammux`, `gvastreamdemux` &nbsp;|&nbsp; **Models:** Configurable detector

</div>
</div>

### Metadata: publishing, access & visualization

<div class="sample-card">
<img class="sample-thumb" src="../_images/sample_app_template.jpg" alt="Metadata Publishing" width="96">
<div class="sample-body">

**[Metadata Publishing](https://github.com/open-edge-platform/dlstreamer/tree/main/samples/gstreamer/gst_launch/metapublish)** `CLI`

Convert inference metadata to JSON and publish to file or Kafka/MQTT.

**Elements:** `gvametaconvert`, `gvametapublish` &nbsp;|&nbsp; **Models:** `centerface`, `dima806_fairface_gender_image_detection`, `dima806_facial_age_image_detection`

</div>
</div>

<div class="sample-card">
<img class="sample-thumb" src="../_images/sample_app_template.jpg" alt="gvaattachroi" width="96">
<div class="sample-body">

**[gvaattachroi](https://github.com/open-edge-platform/dlstreamer/tree/main/samples/gstreamer/gst_launch/gvaattachroi)** `CLI`

Define the regions on which inference should be performed.

**Elements:** `gvaattachroi`, `gvadetect` &nbsp;|&nbsp; **Models:** `yolov8s`

</div>
</div>

<div class="sample-card">
<img class="sample-thumb" src="../_images/sample_app_template.jpg" alt="FPS Throttle" width="96">
<div class="sample-body">

**[FPS Throttle](https://github.com/open-edge-platform/dlstreamer/tree/main/samples/gstreamer/gst_launch/gvafpsthrottle)** `CLI`

Throttle framerate independently of sink sync, without frame duplication or dropping.

**Elements:** `gvafpsthrottle` &nbsp;|&nbsp; **Models:** —

</div>
</div>

<div class="sample-card">
<img class="sample-thumb" src="../_images/sample-watermark-meta-thumb.jpg" alt="Watermark Metadata" width="96">
<div class="sample-body">

**[Watermark Metadata](https://github.com/open-edge-platform/dlstreamer/tree/main/samples/gstreamer/python/watermark_meta)** `Python`

Attach custom drawing primitives (hexagons, lines, circles, text) and render them.

**Elements:** `gvawatermark` &nbsp;|&nbsp; **Models:** — (drawing only)

</div>
</div>

<div class="sample-card">
<img class="sample-thumb" src="../_images/sample_app_template.jpg" alt="Draw Face Attributes (C++)" width="96">
<div class="sample-body">

**[Draw Face Attributes (C++)](https://github.com/open-edge-platform/dlstreamer/tree/main/samples/gstreamer/cpp/draw_face_attributes)** `C++`

Set a C callback to access frame metadata and visualize inference results.

**Elements:** `gvadetect`, `gvaclassify` &nbsp;|&nbsp; **Models:** `centerface`, `dima806_facial_age_image_detection`, `dima806_fairface_gender_image_detection`, `dima806_face_emotions_image_detection`

</div>
</div>

<div class="sample-card">
<img class="sample-thumb" src="../_images/sample_app_template.jpg" alt="Draw Face Attributes (Python)" width="96">
<div class="sample-body">

**[Draw Face Attributes (Python)](https://github.com/open-edge-platform/dlstreamer/tree/main/samples/gstreamer/python/draw_face_attributes)** `Python`

Set a Python callback to access frame metadata and visualize inference results.

**Elements:** `gvadetect`, `gvaclassify` &nbsp;|&nbsp; **Models:** `centerface`, `dima806_facial_age_image_detection`, `dima806_fairface_gender_image_detection`, `dima806_face_emotions_image_detection`

</div>
</div>

<div class="sample-card">
<img class="sample-thumb" src="../_images/sample-open-close-valve-thumb.jpg" alt="Open Close Valve" width="96">
<div class="sample-body">

**[Open Close Valve](https://github.com/open-edge-platform/dlstreamer/tree/main/samples/gstreamer/python/open_close_valve)** `Python`

Open/close a GStreamer `valve` branch from a callback based on detection results.

**Elements:** `gvadetect`, `valve` &nbsp;|&nbsp; **Models:** `yolo11s`, `dima806_vehicle_10_types_image_detection`

</div>
</div>

<div class="sample-card">
<img class="sample-thumb" src="../_images/sample-hello-dlstreamer-thumb.jpg" alt="Hello DL Streamer" width="96">
<div class="sample-body">

**[Hello DL Streamer](https://github.com/open-edge-platform/dlstreamer/tree/main/samples/gstreamer/python/hello_dlstreamer)** `Python`

Build a detection pipeline, analyze metadata to count objects, and visualize results.

**Elements:** `gvadetect`, `gvawatermark` &nbsp;|&nbsp; **Models:** `yolo11n`

</div>
</div>

### Customization & extensibility

<div class="sample-card">
<img class="sample-thumb" src="../_images/sample_app_template.jpg" alt="Custom Post-Processing Library — Classification" width="96">
<div class="sample-body">

**[Custom Post-Processing Library — Classification](https://github.com/open-edge-platform/dlstreamer/tree/main/samples/gstreamer/gst_launch/custom_postproc/classify)** `CLI`, `C++`

Write a custom post-processing library that converts emotion-classification outputs to GstAnalytics metadata.

**Elements:** `gvaclassify` &nbsp;|&nbsp; **Models:** `centerface`, `hsemotion`

</div>
</div>

<div class="sample-card">
<img class="sample-thumb" src="../_images/sample_app_template.jpg" alt="Custom Post-Processing Library — Detection" width="96">
<div class="sample-body">

**[Custom Post-Processing Library — Detection](https://github.com/open-edge-platform/dlstreamer/tree/main/samples/gstreamer/gst_launch/custom_postproc/detect)** `CLI`, `C++`

Write a custom post-processing library that converts YOLOv11 tensor outputs to detection metadata.

**Elements:** `gvadetect` &nbsp;|&nbsp; **Models:** `yolo11s`

</div>
</div>

<div class="sample-card">
<img class="sample-thumb" src="../_images/sample_app_template.jpg" alt="gvapython — Face Detection and Classification" width="96">
<div class="sample-body">

**[gvapython — Face Detection and Classification](https://github.com/open-edge-platform/dlstreamer/tree/main/samples/gstreamer/gst_launch/gvapython/face_detection_and_classification)** `CLI`, `Python`

Customize a pipeline with a Python script for inference post-processing.

**Elements:** `gvapython`, `gvadetect`, `gvaclassify` &nbsp;|&nbsp; **Models:** `centerface`, `dima806_fairface_gender_image_detection`, `dima806_facial_age_image_detection`

</div>
</div>

<div class="sample-card">
<img class="sample-thumb" src="../_images/sample_app_template.jpg" alt="gvapython — Save Frames with ROI" width="96">
<div class="sample-body">

**[gvapython — Save Frames with ROI](https://github.com/open-edge-platform/dlstreamer/tree/main/samples/gstreamer/gst_launch/gvapython/save_frames_with_ROI_only)** `CLI`, `Python`

Use `gvapython` to save video frames containing detected objects to disk.

**Elements:** `gvapython`, `gvadetect` &nbsp;|&nbsp; **Models:** `centerface`

</div>
</div>

<div class="sample-card">
<img class="sample-thumb" src="../_images/sample_app_template.jpg" alt="python-elements — Face Detection and Classification" width="96">
<div class="sample-body">

**[python-elements — Face Detection and Classification](https://github.com/open-edge-platform/dlstreamer/tree/main/samples/gstreamer/gst_launch/python-elements/face_detection_and_classification)** `CLI`, `Python`

Build a custom Python GStreamer element using the GstAnalytics metadata API.

**Elements:** `gvaagelogger_py`, `gvadetect`, `gvaclassify` &nbsp;|&nbsp; **Models:** `YOLOv8-Face-Detection`, `fairface_age_image_detection`, `fairface_gender_image_detection`

</div>
</div>

<div class="sample-card">
<img class="sample-thumb" src="../_images/sample_app_template.jpg" alt="python-elements — Save Frames with ROI" width="96">
<div class="sample-body">

**[python-elements — Save Frames with ROI](https://github.com/open-edge-platform/dlstreamer/tree/main/samples/gstreamer/gst_launch/python-elements/save_frames_with_ROI_only)** `CLI`, `Python`

Build a custom Python GStreamer element to save frames with detected objects.

**Elements:** `gvaframesaver_py`, `gvadetect` &nbsp;|&nbsp; **Models:** `YOLOv8-Face-Detection`

</div>
</div>

<div class="sample-card">
<img class="sample-thumb" src="../_images/sample_app_template.jpg" alt="python-elements — Loitering Detection" width="96">
<div class="sample-body">

**[python-elements — Loitering Detection](https://github.com/open-edge-platform/dlstreamer/tree/main/samples/gstreamer/gst_launch/python-elements/loitering_detection)** `CLI`, `Python`

Measure object dwell time with a custom Python element and render a visual alert when the threshold is exceeded.

**Elements:** `gvaanalytics`, `gvawatermark` &nbsp;|&nbsp; **Models:** `yolo11s`

</div>
</div>

<div class="sample-card">
<img class="sample-thumb" src="../_images/sample_app_template.jpg" alt="Face Detection and Classification (Python)" width="96">
<div class="sample-body">

**[Face Detection and Classification (Python)](https://github.com/open-edge-platform/dlstreamer/tree/main/samples/gstreamer/python/face_detection_and_classification)** `Python`

Download models from Hugging Face, export to OpenVINO IR, and run inference.

**Elements:** `gvadetect`, `gvaclassify` &nbsp;|&nbsp; **Models:** `YOLOv8-Face-Detection`, `fairface`

</div>
</div>

### Performance & benchmarking

<div class="sample-card">
<img class="sample-thumb" src="../_images/sample_app_template.jpg" alt="Benchmark" width="96">
<div class="sample-body">

**[Benchmark](https://github.com/open-edge-platform/dlstreamer/tree/main/samples/gstreamer/benchmark)** `CLI`, `Python`

Measure the performance of single- or multi-channel video analytics pipelines.

**Elements:** `gvadetect`, `gvafpscounter` &nbsp;|&nbsp; **Models:** `centerface` (configurable)

</div>
</div>

<div class="sample-card">
<img class="sample-thumb" src="../_images/sample-e2e-performance-thumb.jpg" alt="DL Streamer E2E Performance" width="96">
<div class="sample-body">

**[DL Streamer E2E Performance](https://github.com/open-edge-platform/dlstreamer/tree/main/samples/gstreamer/e2e_performance)** `Python`

Compare DL Streamer vs. OpenCV + OpenVINO throughput with a YOLO26s INT8 model.

**Elements:** `gvadetect` &nbsp;|&nbsp; **Models:** `yolo26s` (INT8)

</div>
</div>

### Interoperability

<div class="sample-card">
<img class="sample-thumb" src="../_images/sample_app_template.jpg" alt="DL Streamer and DeepStream Coexistence" width="96">
<div class="sample-body">

**[DL Streamer and DeepStream Coexistence](https://github.com/open-edge-platform/dlstreamer/tree/main/samples/gstreamer/python/coexistence)** `Python`

Run pipelines on DL Streamer and/or NVIDIA DeepStream side by side.

**Elements:** `gvadetect` &nbsp;|&nbsp; **Models:** `yolov8` license-plate detector, `PP-OCRv4`

</div>
</div>

<div class="sample-card">
<img class="sample-thumb" src="../_images/sample_app_template.jpg" alt="Coexistence Benchmark" width="96">
<div class="sample-body">

**[Coexistence Benchmark](https://github.com/open-edge-platform/dlstreamer/tree/main/samples/gstreamer/python/coexistence_benchmark)** `Python`

Measure the maximum number of concurrent LPR streams on systems combining Intel and NVIDIA hardware.

**Elements:** `gvadetect` &nbsp;|&nbsp; **Models:** `yolov8` license-plate detector, `PP-OCRv4`

</div>
</div>

---

## Auto-generated reference applications

These end-to-end reference apps combine multiple elements into complete solutions.
Find them under
[samples/auto_generated_samples](https://github.com/open-edge-platform/dlstreamer/tree/main/samples/auto_generated_samples).

<div class="sample-card">
<img class="sample-thumb" src="../_images/sample_app_template.jpg" alt="DeepStream Test4 → DL Streamer Conversion" width="96">
<div class="sample-body">

**[DeepStream Test4 → DL Streamer Conversion](https://github.com/open-edge-platform/dlstreamer/tree/main/samples/auto_generated_samples/deepstream_python_conversion)** `CLI`

DL Streamer equivalent of NVIDIA's deepstream-test4 with YOLO11n detection and metadata publishing.

**Elements:** `gvadetect`, `gvametaconvert`, `gvametapublish` &nbsp;|&nbsp; **Models:** `yolo11n`

</div>
</div>

<div class="sample-card">
<img class="sample-thumb" src="../_images/sample_app_template.jpg" alt="DeepStream LPR App Conversion (C++)" width="96">
<div class="sample-body">

**[DeepStream LPR App Conversion (C++)](https://github.com/open-edge-platform/dlstreamer/tree/main/samples/auto_generated_samples/deepstream_cpp_conversion)** `C++`

C++ conversion of NVIDIA's DeepStream LPR app — license plate detection, tracking and text recognition.

**Elements:** `gvadetect`, `gvatrack`, `gvaclassify` &nbsp;|&nbsp; **Models:** YOLOv11, PaddleOCR

</div>
</div>

<div class="sample-card">
<img class="sample-thumb" src="../_images/sample_app_template.jpg" alt="License Plate Recognition" width="96">
<div class="sample-body">

**[License Plate Recognition](https://github.com/open-edge-platform/dlstreamer/tree/main/samples/auto_generated_samples/license_plate_recognition)** `CLI`

Detect license plates with YOLOv11 and recognize text with PaddleOCR.

**Elements:** `gvadetect`, `gvainference` &nbsp;|&nbsp; **Models:** `YOLOv11`, `PaddleOCR`

</div>
</div>

<div class="sample-card">
<img class="sample-thumb" src="../_images/sample_app_template.jpg" alt="Multi-Stream Compose" width="96">
<div class="sample-body">

**[Multi-Stream Compose](https://github.com/open-edge-platform/dlstreamer/tree/main/samples/auto_generated_samples/multi_stream_compose)** `CLI`

Multi-camera analytics with composite WebRTC output, on-demand recording and a 2x2 GPU-accelerated mosaic.

**Elements:** `gvadetect`, `gvastreammux`, `gvawatermark` &nbsp;|&nbsp; **Models:** `yolo11s`

</div>
</div>

<div class="sample-card">
<img class="sample-thumb" src="../_images/sample_app_template.jpg" alt="People Detection and Tracking with Deep SORT" width="96">
<div class="sample-body">

**[People Detection and Tracking with Deep SORT](https://github.com/open-edge-platform/dlstreamer/tree/main/samples/auto_generated_samples/people_detection_tracking)** `CLI`

Detect and track people using YOLO26m and Deep SORT with a Mars-Small-128 re-ID model.

**Elements:** `gvadetect`, `gvatrack` &nbsp;|&nbsp; **Models:** `yolo26m`, `mars-small128`

</div>
</div>

<div class="sample-card">
<img class="sample-thumb" src="../_images/sample_app_template.jpg" alt="Pose Estimation Compose" width="96">
<div class="sample-body">

**[Pose Estimation Compose](https://github.com/open-edge-platform/dlstreamer/tree/main/samples/auto_generated_samples/pose_estimation_compose)** `CLI`

Run 4 YOLO pose models in parallel on the same video and composite results into a 2x2 mosaic.

**Elements:** `gvaclassify`, `gvawatermark` &nbsp;|&nbsp; **Models:** `yolo26n-pose`, `yolo11n-pose`, `yolov8n-pose`, `yolov8l-pose`

</div>
</div>

<div class="sample-card">
<img class="sample-thumb" src="../_images/sample_app_template.jpg" alt="Safety Compliance Monitor" width="96">
<div class="sample-body">

**[Safety Compliance Monitor](https://github.com/open-edge-platform/dlstreamer/tree/main/samples/auto_generated_samples/safety_compliance)** `CLI`

Detect and track workers and use Qwen2.5-VL to verify helmet and harness compliance.

**Elements:** `gvadetect`, `gvatrack`, `gvagenai` &nbsp;|&nbsp; **Models:** `yolo26m`, `Qwen2.5-VL-3B`

</div>
</div>

<div class="sample-card">
<img class="sample-thumb" src="../_images/sample_app_template.jpg" alt="Smart NVR — Event-Based Recording" width="96">
<div class="sample-body">

**[Smart NVR — Event-Based Recording](https://github.com/open-edge-platform/dlstreamer/tree/main/samples/auto_generated_samples/smart_nvr)** `CLI`

Detect people with YOLO11n and record video only when a person is present.

**Elements:** `gvadetect` &nbsp;|&nbsp; **Models:** `yolo11n`

</div>
</div>
