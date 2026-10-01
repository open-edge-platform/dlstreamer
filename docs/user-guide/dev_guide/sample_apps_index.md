# Available Sample Apps

This page indexes all DL Streamer samples by use case. See
[Using Sample Apps](./using_sample_apps.md) for installation, model download, and run
instructions before trying any of the samples below.

Each entry below is a mini "card": name, a one-line preview thumbnail (when available), what it
demonstrates, the elements/models it uses, and its language — `CLI` (`gst-launch` command line),
`Python`, or `C++`.

---

## Browse by category

| | |
| --- | --- |
| [Object detection, classification & segmentation (9)](#object-detection-classification--segmentation) | [Object tracking & analytics (3)](#object-tracking--analytics) |
| [Vision-Language Models (VLM) & GenAI (5)](#vision-language-models-vlm--genai) | [Audio analytics (2)](#audio-analytics) |
| [3D: LiDAR & radar (5)](#3d-lidar--radar) | [Cameras & input sources (4)](#cameras--input-sources) |
| [Metadata: publishing, access & visualization (8)](#metadata-publishing-access--visualization) | [Customization & extensibility (8)](#customization--extensibility) |
| [Performance & benchmarking (2)](#performance--benchmarking) | [Interoperability (2)](#interoperability) |
| [Auto-generated reference applications (8)](#auto-generated-reference-applications) | |

> [!TIP]
> Use your browser's find-in-page (Ctrl+F / Cmd+F) to search across all samples by name,
> element (e.g. `gvadetect`), or model (e.g. `yolo11n`).

---

### Object detection, classification & segmentation

| Preview | Sample |
| --- | --- |
| ![Detection with YOLO](../_images/sample-detection-with-yolo-thumb.jpg) | [Detection with YOLO](https://github.com/open-edge-platform/dlstreamer/tree/main/samples/gstreamer/gst_launch/detection_with_yolo) `CLI`<br>Object detection and classification with publicly available YOLO models.<br>**Key elements:** `gvadetect`, `gvaclassify`<br>**Models:** `yolox_s` (default; many YOLO variants) |
| ![Face Detection and Classification](../_images/sample_app_template.png) | [Face Detection and Classification](https://github.com/open-edge-platform/dlstreamer/tree/main/samples/gstreamer/gst_launch/face_detection_and_classification) `CLI`<br>Detect faces and estimate age, gender, emotions and facial landmarks.<br>**Key elements:** `gvadetect`, `gvaclassify`<br>**Models:** `centerface`, `dima806_facial_age_image_detection`, `dima806_fairface_gender_image_detection`, `dima806_face_emotions_image_detection` |
| ![Instance Segmentation](../_images/sample-instance-segmentation-thumb.jpg) | [Instance Segmentation](https://github.com/open-edge-platform/dlstreamer/tree/main/samples/gstreamer/gst_launch/instance_segmentation) `CLI`<br>Instance segmentation with YOLO-seg models visualized by `gvawatermark`.<br>**Key elements:** `gvadetect`, `gvawatermark`<br>**Models:** `yolo26s-seg` (default; also `yolo11s-seg`) |
| ![Human Pose Estimation](../_images/sample_app_template.png) | [Human Pose Estimation](https://github.com/open-edge-platform/dlstreamer/tree/main/samples/gstreamer/gst_launch/human_pose_estimation) `CLI`<br>Full-frame human pose estimation.<br>**Key elements:** `gvaclassify`<br>**Models:** `yolo26s-pose` |
| ![Depth Estimation](../_images/sample_app_template.png) | [Depth Estimation](https://github.com/open-edge-platform/dlstreamer/tree/main/samples/gstreamer/gst_launch/depth_estimation) `CLI`<br>YOLO11n detection followed by Depth Anything V2 depth estimation on detected regions.<br>**Key elements:** `gvadetect`, `gvainference`<br>**Models:** `yolo11n`, `Depth-Anything-V2-Small-hf` |
| ![License Plate Recognition](../_images/sample_app_template.png) | [License Plate Recognition](https://github.com/open-edge-platform/dlstreamer/tree/main/samples/gstreamer/gst_launch/license_plate_recognition) `CLI`<br>YOLO detector combined with an optical character recognition model.<br>**Key elements:** `gvadetect`, `gvainference`<br>**Models:** `yolov8` license-plate detector, `PP-OCRv4` |
| ![Prompt-based Object Detection](../_images/sample-prompted-detection-thumb.jpg) | [Prompt-based Object Detection](https://github.com/open-edge-platform/dlstreamer/tree/main/samples/gstreamer/python/prompted_detection) `Python`<br>Search a video for user-defined objects using an open-vocabulary model (YOLOE).<br>**Key elements:** `gvadetect`<br>**Models:** `yoloe-26s-seg` (text-prompt, class baked in at export) |
| ![Deployment of Geti™ models](../_images/sample-geti-deployment-thumb.jpg) | [Deployment of Geti™ models](https://github.com/open-edge-platform/dlstreamer/tree/main/samples/gstreamer/gst_launch/geti_deployment) `CLI`<br>Deploy Geti™-trained models for detection, anomaly detection and classification.<br>**Key elements:** `gvadetect`, `gvaclassify`<br>**Models:** Geti™-trained (Padim / STFPM / UFlow) |
| ![Motion Detect](../_images/sample_app_template.png) | [Motion Detect](https://github.com/open-edge-platform/dlstreamer/tree/main/samples/gstreamer/gst_launch/motion_detect) `CLI`<br>Run detection only over motion ROIs (GPU and CPU paths).<br>**Key elements:** `gvamotiondetect`, `gvadetect`<br>**Models:** `yolov8n` |

### Object tracking & analytics

| Preview | Sample |
| --- | --- |
| ![Vehicle and Pedestrian Tracking](../_images/sample_app_template.png) | [Vehicle and Pedestrian Tracking](https://github.com/open-edge-platform/dlstreamer/tree/main/samples/gstreamer/gst_launch/vehicle_pedestrian_tracking) `CLI`<br>Object tracking across frames.<br>**Key elements:** `gvatrack`, `gvadetect`, `gvaclassify`<br>**Models:** `yolo26s`, `dima806_vehicle_10_types_image_detection` |
| ![Vehicle Counter with gvaanalytics Tripwires](../_images/sample-gvaanalytics-tripwire-thumb.jpg) | [Vehicle Counter with gvaanalytics Tripwires](https://github.com/open-edge-platform/dlstreamer/tree/main/samples/gstreamer/python/gvaanalytics_tripwire) `Python`<br>Count vehicles crossing a virtual line in both directions using tripwires.<br>**Key elements:** `gvaanalytics`, `gvatrack`<br>**Models:** `yolo11n` |
| ![Smart NVR for Lane Hogging Detection](../_images/sample-smart-nvr-thumb.jpg) | [Smart NVR for Lane Hogging Detection](https://github.com/open-edge-platform/dlstreamer/tree/main/samples/gstreamer/python/smart_nvr) `Python`<br>Build an NVR with custom analytics and video storage to detect lane-hogging events.<br>**Key elements:** `gvaanalytics_py`, `gvarecorder_py`<br>**Models:** `rtdetr_v2_r50vd` (RT-DETRv2) |

### Vision-Language Models (VLM) & GenAI

| Preview | Sample |
| --- | --- |
| ![Using VLM Models with gvagenai](../_images/sample_app_template.png) | [Using VLM Models with gvagenai](https://github.com/open-edge-platform/dlstreamer/tree/main/samples/gstreamer/gst_launch/gvagenai) `CLI`<br>Video summarization with MiniCPM-V.<br>**Key elements:** `gvagenai`<br>**Models:** `MiniCPM-V`, `Phi-4-multimodal-instruct` or `Gemma-3` |
| ![VLM Alerts](../_images/sample-vlm-alerts-thumb.jpg) | [VLM Alerts](https://github.com/open-edge-platform/dlstreamer/tree/main/samples/gstreamer/python/vlm_alerts) `Python`<br>Edge alerting pipeline that generates structured JSON alerts per frame with annotated video.<br>**Key elements:** `gvagenai`<br>**Models:** Configurable VLM (e.g. `Qwen2.5-VL`, `InternVL`) |
| ![VLM-assisted Self Checkout](../_images/sample-vlm-self-checkout-thumb.jpg) | [VLM-assisted Self Checkout](https://github.com/open-edge-platform/dlstreamer/tree/main/samples/gstreamer/python/vlm_self_checkout) `Python`<br>Combine CV object detection with a VLM for item classification, running both locally on edge.<br>**Key elements:** `gvadetect`, `gvagenai`<br>**Models:** `yolo26s`, `MiniCPM-V-4_5` |
| ![ONVIF Camera Analytics Validation](../_images/sample-onvif-camera-analytics-validation-thumb.jpg) | [ONVIF Camera Analytics Validation](https://github.com/open-edge-platform/dlstreamer/tree/main/samples/gstreamer/python/onvif_camera_analytics_validation) `Python`<br>Use a VLM as an additional validation layer for ONVIF-enabled analytics cameras.<br>**Key elements:** `gvagenai`<br>**Models:** Configurable VLM |
| ![Image Embeddings Generation with ViT](../_images/sample_app_template.png) | [Image Embeddings Generation with ViT](https://github.com/open-edge-platform/dlstreamer/tree/main/samples/gstreamer/gst_launch/lvm) `CLI`<br>Generate image embeddings using the Vision Transformer component of a CLIP model.<br>**Key elements:** `gvainference`<br>**Models:** `clip-vit-large-patch14` (CLIP ViT) |

### Audio analytics

| Preview | Sample |
| --- | --- |
| ![Audio Event Detection](../_images/sample_app_template.png) | [Audio Event Detection](https://github.com/open-edge-platform/dlstreamer/tree/main/samples/gstreamer/gst_launch/audio_detect) `CLI`<br>Audio event detection, converting results to JSON.<br>**Key elements:** `gvaaudiodetect`, `gvametaconvert`, `gvametapublish`<br>**Models:** `aclnet` |
| ![Audio Transcription](../_images/sample_app_template.png) | [Audio Transcription](https://github.com/open-edge-platform/dlstreamer/tree/main/samples/gstreamer/gst_launch/audio_transcribe) `CLI`<br>Speech transcription using an OpenVINO GenAI Whisper model.<br>**Key elements:** `gvaaudiotranscribe`<br>**Models:** `whisper` |

### 3D: LiDAR & radar

| Preview | Sample |
| --- | --- |
| ![PointPillars Inference with g3dinference](../_images/sample_app_template.png) | [PointPillars Inference with g3dinference](https://github.com/open-edge-platform/dlstreamer/tree/main/samples/gstreamer/gst_launch/g3dinference) `CLI`<br>Complete LiDAR-only 3D detection pipeline.<br>**Key elements:** `g3dlidarparse`, `g3dinference`<br>**Models:** `PointPillars` |
| ![LiDAR Parse](../_images/sample_app_template.png) | [LiDAR Parse](https://github.com/open-edge-platform/dlstreamer/tree/main/samples/gstreamer/gst_launch/g3dlidarparse) `CLI`<br>LiDAR parsing pipeline.<br>**Key elements:** `g3dlidarparse`<br>**Models:** — (parsing only) |
| ![Live LiDAR Capture](../_images/sample_app_template.png) | [Live LiDAR Capture](https://github.com/open-edge-platform/dlstreamer/tree/main/samples/gstreamer/gst_launch/g3dlidarsrc) `CLI`<br>Real-time LiDAR capture from a physical device (RoboSense via rs_driver).<br>**Key elements:** `g3dlidarsrc`, `g3dinference`<br>**Models:** `PointPillars` |
| ![Camera + 3D Object Fusion](../_images/sample_app_template.png) | [Camera + 3D Object Fusion](https://github.com/open-edge-platform/dlstreamer/tree/main/samples/gstreamer/gst_launch/g3dobjectfuser) `CLI`<br>Fuse 2D camera detections with 3D LiDAR detections.<br>**Key elements:** `g3dobjectfuser`, `gvastreammux`<br>**Models:** `yolo11n`, `PointPillars` |
| ![Radar Signal Process](../_images/sample_app_template.png) | [Radar Signal Process](https://github.com/open-edge-platform/dlstreamer/tree/main/samples/gstreamer/gst_launch/g3dradarprocess) `CLI`<br>mmWave radar signal processing with point-cloud detection, clustering and tracking.<br>**Key elements:** `g3dradarprocess`<br>**Models:** — (signal processing) |

### Cameras & input sources

| Preview | Sample |
| --- | --- |
| ![RealSense™ Camera](../_images/sample_app_template.png) | [RealSense™ Camera](https://github.com/open-edge-platform/dlstreamer/tree/main/samples/gstreamer/gst_launch/gvarealsense) `CLI`<br>Capture a video stream from a 3D Intel RealSense™ Depth Camera.<br>**Key elements:** `gvarealsense`<br>**Models:** — (capture only) |
| ![ONVIF Camera Discovery](../_images/sample-onvif-cameras-discovery-thumb.jpg) | [ONVIF Camera Discovery](https://github.com/open-edge-platform/dlstreamer/tree/main/samples/gstreamer/python/onvif_cameras_discovery) `Python`<br>Automatically discover ONVIF cameras on the network and launch pipelines for each.<br>**Key elements:** `gvadetect`<br>**Models:** Configurable detector |
| ![Multi-camera deployments](../_images/sample_app_template.png) | [Multi-camera deployments](https://github.com/open-edge-platform/dlstreamer/tree/main/samples/gstreamer/gst_launch/multi_stream) `CLI`<br>Handle video streams from multiple cameras in a single application.<br>**Key elements:** `gvadetect`, `gvafpscounter`<br>**Models:** `yolo11s` (many YOLO variants) |
| ![Multi-Stream Mux/Demux](../_images/sample_app_template.png) | [Multi-Stream Mux/Demux](https://github.com/open-edge-platform/dlstreamer/tree/main/samples/gstreamer/gst_launch/stream_mux_and_demux) `CLI`<br>Share a single inference pipeline across streams with per-source routing.<br>**Key elements:** `gvastreammux`, `gvastreamdemux`<br>**Models:** Configurable detector |

### Metadata: publishing, access & visualization

| Preview | Sample |
| --- | --- |
| ![Metadata Publishing](../_images/sample_app_template.png) | [Metadata Publishing](https://github.com/open-edge-platform/dlstreamer/tree/main/samples/gstreamer/gst_launch/metapublish) `CLI`<br>Convert inference metadata to JSON and publish to file or Kafka/MQTT.<br>**Key elements:** `gvametaconvert`, `gvametapublish`<br>**Models:** `centerface`, `dima806_fairface_gender_image_detection`, `dima806_facial_age_image_detection` |
| ![gvaattachroi](../_images/sample_app_template.png) | [gvaattachroi](https://github.com/open-edge-platform/dlstreamer/tree/main/samples/gstreamer/gst_launch/gvaattachroi) `CLI`<br>Define the regions on which inference should be performed.<br>**Key elements:** `gvaattachroi`, `gvadetect`<br>**Models:** `yolov8s` |
| ![FPS Throttle](../_images/sample_app_template.png) | [FPS Throttle](https://github.com/open-edge-platform/dlstreamer/tree/main/samples/gstreamer/gst_launch/gvafpsthrottle) `CLI`<br>Throttle framerate independently of sink sync, without frame duplication or dropping.<br>**Key elements:** `gvafpsthrottle`<br>**Models:** — |
| ![Watermark Metadata](../_images/sample-watermark-meta-thumb.jpg) | [Watermark Metadata](https://github.com/open-edge-platform/dlstreamer/tree/main/samples/gstreamer/python/watermark_meta) `Python`<br>Attach custom drawing primitives (hexagons, lines, circles, text) and render them.<br>**Key elements:** `gvawatermark`<br>**Models:** — (drawing only) |
| ![Draw Face Attributes (C++)](../_images/sample_app_template.png) | [Draw Face Attributes (C++)](https://github.com/open-edge-platform/dlstreamer/tree/main/samples/gstreamer/cpp/draw_face_attributes) `C++`<br>Set a C callback to access frame metadata and visualize inference results.<br>**Key elements:** `gvadetect`, `gvaclassify`<br>**Models:** `centerface`, `dima806_facial_age_image_detection`, `dima806_fairface_gender_image_detection`, `dima806_face_emotions_image_detection` |
| ![Draw Face Attributes (Python)](../_images/sample_app_template.png) | [Draw Face Attributes (Python)](https://github.com/open-edge-platform/dlstreamer/tree/main/samples/gstreamer/python/draw_face_attributes) `Python`<br>Set a Python callback to access frame metadata and visualize inference results.<br>**Key elements:** `gvadetect`, `gvaclassify`<br>**Models:** `centerface`, `dima806_facial_age_image_detection`, `dima806_fairface_gender_image_detection`, `dima806_face_emotions_image_detection` |
| ![Open Close Valve](../_images/sample-open-close-valve-thumb.jpg) | [Open Close Valve](https://github.com/open-edge-platform/dlstreamer/tree/main/samples/gstreamer/python/open_close_valve) `Python`<br>Open/close a GStreamer `valve` branch from a callback based on detection results.<br>**Key elements:** `gvadetect`, `valve`<br>**Models:** `yolo11s`, `dima806_vehicle_10_types_image_detection` |
| ![Hello DL Streamer](../_images/sample-hello-dlstreamer-thumb.jpg) | [Hello DL Streamer](https://github.com/open-edge-platform/dlstreamer/tree/main/samples/gstreamer/python/hello_dlstreamer) `Python`<br>Build a detection pipeline, analyze metadata to count objects, and visualize results.<br>**Key elements:** `gvadetect`, `gvawatermark`<br>**Models:** `yolo11n` |

### Customization & extensibility

| Preview | Sample |
| --- | --- |
| ![Custom Post-Processing Library — Classification](../_images/sample_app_template.png) | [Custom Post-Processing Library — Classification](https://github.com/open-edge-platform/dlstreamer/tree/main/samples/gstreamer/gst_launch/custom_postproc/classify) `CLI`, `C++`<br>Write a custom post-processing library that converts emotion-classification outputs to GstAnalytics metadata.<br>**Key elements:** `gvaclassify`<br>**Models:** `centerface`, `hsemotion` |
| ![Custom Post-Processing Library — Detection](../_images/sample_app_template.png) | [Custom Post-Processing Library — Detection](https://github.com/open-edge-platform/dlstreamer/tree/main/samples/gstreamer/gst_launch/custom_postproc/detect) `CLI`, `C++`<br>Write a custom post-processing library that converts YOLOv11 tensor outputs to detection metadata.<br>**Key elements:** `gvadetect`<br>**Models:** `yolo11s` |
| ![gvapython — Face Detection and Classification](../_images/sample_app_template.png) | [gvapython — Face Detection and Classification](https://github.com/open-edge-platform/dlstreamer/tree/main/samples/gstreamer/gst_launch/gvapython/face_detection_and_classification) `CLI`, `Python`<br>Customize a pipeline with a Python script for inference post-processing.<br>**Key elements:** `gvapython`, `gvadetect`, `gvaclassify`<br>**Models:** `centerface`, `dima806_fairface_gender_image_detection`, `dima806_facial_age_image_detection` |
| ![gvapython — Save Frames with ROI](../_images/sample_app_template.png) | [gvapython — Save Frames with ROI](https://github.com/open-edge-platform/dlstreamer/tree/main/samples/gstreamer/gst_launch/gvapython/save_frames_with_ROI_only) `CLI`, `Python`<br>Use `gvapython` to save video frames containing detected objects to disk.<br>**Key elements:** `gvapython`, `gvadetect`<br>**Models:** `centerface` |
| ![python-elements — Face Detection and Classification](../_images/sample_app_template.png) | [python-elements — Face Detection and Classification](https://github.com/open-edge-platform/dlstreamer/tree/main/samples/gstreamer/gst_launch/python-elements/face_detection_and_classification) `CLI`, `Python`<br>Build a custom Python GStreamer element using the GstAnalytics metadata API.<br>**Key elements:** `gvaagelogger_py`, `gvadetect`, `gvaclassify`<br>**Models:** `YOLOv8-Face-Detection`, `fairface_age_image_detection`, `fairface_gender_image_detection` |
| ![python-elements — Save Frames with ROI](../_images/sample_app_template.png) | [python-elements — Save Frames with ROI](https://github.com/open-edge-platform/dlstreamer/tree/main/samples/gstreamer/gst_launch/python-elements/save_frames_with_ROI_only) `CLI`, `Python`<br>Build a custom Python GStreamer element to save frames with detected objects.<br>**Key elements:** `gvaframesaver_py`, `gvadetect`<br>**Models:** `YOLOv8-Face-Detection` |
| ![python-elements — Loitering Detection](../_images/sample_app_template.png) | [python-elements — Loitering Detection](https://github.com/open-edge-platform/dlstreamer/tree/main/samples/gstreamer/gst_launch/python-elements/loitering_detection) `CLI`, `Python`<br>Measure object dwell time with a custom Python element and render a visual alert when the threshold is exceeded.<br>**Key elements:** `gvaanalytics`, `gvawatermark`<br>**Models:** `yolo11s` |
| ![Face Detection and Classification (Python)](../_images/sample_app_template.png) | [Face Detection and Classification (Python)](https://github.com/open-edge-platform/dlstreamer/tree/main/samples/gstreamer/python/face_detection_and_classification) `Python`<br>Download models from Hugging Face, export to OpenVINO IR, and run inference.<br>**Key elements:** `gvadetect`, `gvaclassify`<br>**Models:** `YOLOv8-Face-Detection`, `fairface` |

### Performance & benchmarking

| Preview | Sample |
| --- | --- |
| ![Benchmark](../_images/sample_app_template.png) | [Benchmark](https://github.com/open-edge-platform/dlstreamer/tree/main/samples/gstreamer/benchmark) `CLI`, `Python`<br>Measure the performance of single- or multi-channel video analytics pipelines.<br>**Key elements:** `gvadetect`, `gvafpscounter`<br>**Models:** `centerface` (configurable) |
| ![DL Streamer E2E Performance](../_images/sample-e2e-performance-thumb.jpg) | [DL Streamer E2E Performance](https://github.com/open-edge-platform/dlstreamer/tree/main/samples/gstreamer/e2e_performance) `Python`<br>Compare DL Streamer vs. OpenCV + OpenVINO throughput with a YOLO26s INT8 model.<br>**Key elements:** `gvadetect`<br>**Models:** `yolo26s` (INT8) |

### Interoperability

| Preview | Sample |
| --- | --- |
| ![DL Streamer and DeepStream Coexistence](../_images/sample_app_template.png) | [DL Streamer and DeepStream Coexistence](https://github.com/open-edge-platform/dlstreamer/tree/main/samples/gstreamer/python/coexistence) `Python`<br>Run pipelines on DL Streamer and/or NVIDIA DeepStream side by side.<br>**Key elements:** `gvadetect`<br>**Models:** `yolov8` license-plate detector, `PP-OCRv4` |
| ![Coexistence Benchmark](../_images/sample_app_template.png) | [Coexistence Benchmark](https://github.com/open-edge-platform/dlstreamer/tree/main/samples/gstreamer/python/coexistence_benchmark) `Python`<br>Measure the maximum number of concurrent LPR streams on systems combining Intel and NVIDIA hardware.<br>**Key elements:** `gvadetect`<br>**Models:** `yolov8` license-plate detector, `PP-OCRv4` |

## Auto-generated reference applications

These end-to-end reference apps combine multiple elements into complete solutions.
Find them under
[samples/auto_generated_samples](https://github.com/open-edge-platform/dlstreamer/tree/main/samples/auto_generated_samples).

| Preview | Sample |
| --- | --- |
| ![DeepStream Test4 → DL Streamer Conversion](../_images/sample_app_template.png) | [DeepStream Test4 → DL Streamer Conversion](https://github.com/open-edge-platform/dlstreamer/tree/main/samples/auto_generated_samples/deepstream_python_conversion) `CLI`<br>DL Streamer equivalent of NVIDIA's deepstream-test4 with YOLO11n detection and metadata publishing.<br>**Key elements:** `gvadetect`, `gvametaconvert`, `gvametapublish`<br>**Models:** `yolo11n` |
| ![DeepStream LPR App Conversion (C++)](../_images/sample_app_template.png) | [DeepStream LPR App Conversion (C++)](https://github.com/open-edge-platform/dlstreamer/tree/main/samples/auto_generated_samples/deepstream_cpp_conversion) `C++`<br>C++ conversion of NVIDIA's DeepStream LPR app — license plate detection, tracking and text recognition.<br>**Key elements:** `gvadetect`, `gvatrack`, `gvaclassify`<br>**Models:** YOLOv11, PaddleOCR |
| ![License Plate Recognition](../_images/sample_app_template.png) | [License Plate Recognition](https://github.com/open-edge-platform/dlstreamer/tree/main/samples/auto_generated_samples/license_plate_recognition) `CLI`<br>Detect license plates with YOLOv11 and recognize text with PaddleOCR.<br>**Key elements:** `gvadetect`, `gvainference`<br>**Models:** `YOLOv11`, `PaddleOCR` |
| ![Multi-Stream Compose](../_images/sample_app_template.png) | [Multi-Stream Compose](https://github.com/open-edge-platform/dlstreamer/tree/main/samples/auto_generated_samples/multi_stream_compose) `CLI`<br>Multi-camera analytics with composite WebRTC output, on-demand recording and a 2x2 GPU-accelerated mosaic.<br>**Key elements:** `gvadetect`, `gvastreammux`, `gvawatermark`<br>**Models:** `yolo11s` |
| ![People Detection and Tracking with Deep SORT](../_images/sample_app_template.png) | [People Detection and Tracking with Deep SORT](https://github.com/open-edge-platform/dlstreamer/tree/main/samples/auto_generated_samples/people_detection_tracking) `CLI`<br>Detect and track people using YOLO26m and Deep SORT with a Mars-Small-128 re-ID model.<br>**Key elements:** `gvadetect`, `gvatrack`<br>**Models:** `yolo26m`, `mars-small128` |
| ![Pose Estimation Compose](../_images/sample_app_template.png) | [Pose Estimation Compose](https://github.com/open-edge-platform/dlstreamer/tree/main/samples/auto_generated_samples/pose_estimation_compose) `CLI`<br>Run 4 YOLO pose models in parallel on the same video and composite results into a 2x2 mosaic.<br>**Key elements:** `gvaclassify`, `gvawatermark`<br>**Models:** `yolo26n-pose`, `yolo11n-pose`, `yolov8n-pose`, `yolov8l-pose` |
| ![Safety Compliance Monitor](../_images/sample_app_template.png) | [Safety Compliance Monitor](https://github.com/open-edge-platform/dlstreamer/tree/main/samples/auto_generated_samples/safety_compliance) `CLI`<br>Detect and track workers and use Qwen2.5-VL to verify helmet and harness compliance.<br>**Key elements:** `gvadetect`, `gvatrack`, `gvagenai`<br>**Models:** `yolo26m`, `Qwen2.5-VL-3B` |
| ![Smart NVR — Event-Based Recording](../_images/sample_app_template.png) | [Smart NVR — Event-Based Recording](https://github.com/open-edge-platform/dlstreamer/tree/main/samples/auto_generated_samples/smart_nvr) `CLI`<br>Detect people with YOLO11n and record video only when a person is present.<br>**Key elements:** `gvadetect`<br>**Models:** `yolo11n` |
