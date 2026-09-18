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

---

### Object detection, classification & segmentation

<div style="display:flex; gap:1.5rem; align-items:flex-start; margin:1.5rem 0;">
  <div style="flex:0 0 200px; max-width:200px;">

  ![Detection with YOLO](../_images/sample-detection-with-yolo-thumb.jpg)

  </div>
  <div style="flex:1;">
    <p style="font-size:1.1rem;"><strong><a href="https://github.com/open-edge-platform/dlstreamer/tree/main/samples/gstreamer/gst_launch/detection_with_yolo">Detection with YOLO</a></strong> — CLI</p>
    <p>Object detection and classification with publicly available YOLO models.</p>
    <p><strong>Elements:</strong> <code>gvadetect</code>, <code>gvaclassify</code></p>
    <p><strong>Models:</strong> <code>yolox_s</code> (default; many YOLO variants)</p>
  </div>
</div>

<div style="display:flex; gap:1.5rem; align-items:flex-start; margin:1.5rem 0;">
  <div style="flex:0 0 200px; max-width:200px;">

  ![Face Detection and Classification](../_images/sample_app_template.jpg)

  </div>
  <div style="flex:1;">
    <p style="font-size:1.1rem;"><strong><a href="https://github.com/open-edge-platform/dlstreamer/tree/main/samples/gstreamer/gst_launch/face_detection_and_classification">Face Detection and Classification</a></strong> — CLI</p>
    <p>Detect faces and estimate age, gender, emotions and facial landmarks.</p>
    <p><strong>Elements:</strong> <code>gvadetect</code>, <code>gvaclassify</code></p>
    <p><strong>Models:</strong> <code>centerface</code>, <code>dima806_facial_age_image_detection</code>, <code>dima806_fairface_gender_image_detection</code>, <code>dima806_face_emotions_image_detection</code></p>
  </div>
</div>

<div style="display:flex; gap:1.5rem; align-items:flex-start; margin:1.5rem 0;">
  <div style="flex:0 0 200px; max-width:200px;">

  ![Instance Segmentation](../_images/sample-instance-segmentation-thumb.jpg)

  </div>
  <div style="flex:1;">
    <p style="font-size:1.1rem;"><strong><a href="https://github.com/open-edge-platform/dlstreamer/tree/main/samples/gstreamer/gst_launch/instance_segmentation">Instance Segmentation</a></strong> — CLI</p>
    <p>Instance segmentation via the <code>object_detect</code> and <code>object_classify</code> bin elements.</p>
    <p><strong>Elements:</strong> <code>object_detect</code>, <code>object_classify</code></p>
    <p><strong>Models:</strong> <code>yolo26s-seg</code> (default; also <code>yolo11s-seg</code>)</p>
  </div>
</div>

<div style="display:flex; gap:1.5rem; align-items:flex-start; margin:1.5rem 0;">
  <div style="flex:0 0 200px; max-width:200px;">

  ![Human Pose Estimation](../_images/sample_app_template.jpg)

  </div>
  <div style="flex:1;">
    <p style="font-size:1.1rem;"><strong><a href="https://github.com/open-edge-platform/dlstreamer/tree/main/samples/gstreamer/gst_launch/human_pose_estimation">Human Pose Estimation</a></strong> — CLI</p>
    <p>Full-frame human pose estimation.</p>
    <p><strong>Elements:</strong> <code>gvaclassify</code></p>
    <p><strong>Models:</strong> <code>yolo26s-pose</code></p>
  </div>
</div>

<div style="display:flex; gap:1.5rem; align-items:flex-start; margin:1.5rem 0;">
  <div style="flex:0 0 200px; max-width:200px;">

  ![Depth Estimation](../_images/sample_app_template.jpg)

  </div>
  <div style="flex:1;">
    <p style="font-size:1.1rem;"><strong><a href="https://github.com/open-edge-platform/dlstreamer/tree/main/samples/gstreamer/gst_launch/depth_estimation">Depth Estimation</a></strong> — CLI</p>
    <p>YOLO11n detection followed by Depth Anything V2 depth estimation on detected regions.</p>
    <p><strong>Elements:</strong> <code>gvadetect</code>, <code>gvainference</code></p>
    <p><strong>Models:</strong> <code>yolo11n</code>, <code>Depth-Anything-V2-Small-hf</code></p>
  </div>
</div>

<div style="display:flex; gap:1.5rem; align-items:flex-start; margin:1.5rem 0;">
  <div style="flex:0 0 200px; max-width:200px;">

  ![License Plate Recognition](../_images/sample_app_template.jpg)

  </div>
  <div style="flex:1;">
    <p style="font-size:1.1rem;"><strong><a href="https://github.com/open-edge-platform/dlstreamer/tree/main/samples/gstreamer/gst_launch/license_plate_recognition">License Plate Recognition</a></strong> — CLI</p>
    <p>YOLO detector combined with an optical character recognition model.</p>
    <p><strong>Elements:</strong> <code>gvadetect</code>, <code>gvainference</code></p>
    <p><strong>Models:</strong> <code>yolov8</code> license-plate detector, <code>PP-OCRv4</code></p>
  </div>
</div>

<div style="display:flex; gap:1.5rem; align-items:flex-start; margin:1.5rem 0;">
  <div style="flex:0 0 200px; max-width:200px;">

  ![Prompt-based Object Detection](../_images/sample-prompted-detection-thumb.jpg)

  </div>
  <div style="flex:1;">
    <p style="font-size:1.1rem;"><strong><a href="https://github.com/open-edge-platform/dlstreamer/tree/main/samples/gstreamer/python/prompted_detection">Prompt-based Object Detection</a></strong> — Python</p>
    <p>Search a video for user-defined objects using an open-vocabulary model (YOLOE).</p>
    <p><strong>Elements:</strong> <code>gvadetect</code></p>
    <p><strong>Models:</strong> <code>yoloe-26s-seg</code> (text-prompt, class baked in at export)</p>
  </div>
</div>

<div style="display:flex; gap:1.5rem; align-items:flex-start; margin:1.5rem 0;">
  <div style="flex:0 0 200px; max-width:200px;">

  ![Deployment of Geti™ models](../_images/sample-geti-deployment-thumb.jpg)

  </div>
  <div style="flex:1;">
    <p style="font-size:1.1rem;"><strong><a href="https://github.com/open-edge-platform/dlstreamer/tree/main/samples/gstreamer/gst_launch/geti_deployment">Deployment of Geti™ models</a></strong> — CLI</p>
    <p>Deploy Geti™-trained models for detection, anomaly detection and classification.</p>
    <p><strong>Elements:</strong> <code>gvadetect</code>, <code>gvaclassify</code></p>
    <p><strong>Models:</strong> Geti™-trained (Padim / STFPM / UFlow)</p>
  </div>
</div>

<div style="display:flex; gap:1.5rem; align-items:flex-start; margin:1.5rem 0;">
  <div style="flex:0 0 200px; max-width:200px;">

  ![Motion Detect](../_images/sample_app_template.jpg)

  </div>
  <div style="flex:1;">
    <p style="font-size:1.1rem;"><strong><a href="https://github.com/open-edge-platform/dlstreamer/tree/main/samples/gstreamer/gst_launch/motion_detect">Motion Detect</a></strong> — CLI</p>
    <p>Run detection only over motion ROIs (GPU and CPU paths).</p>
    <p><strong>Elements:</strong> <code>gvamotiondetect</code>, <code>gvadetect</code></p>
    <p><strong>Models:</strong> <code>yolov8n</code></p>
  </div>
</div>

### Object tracking & analytics

<div style="display:flex; gap:1.5rem; align-items:flex-start; margin:1.5rem 0;">
  <div style="flex:0 0 200px; max-width:200px;">

  ![Vehicle and Pedestrian Tracking](../_images/sample_app_template.jpg)

  </div>
  <div style="flex:1;">
    <p style="font-size:1.1rem;"><strong><a href="https://github.com/open-edge-platform/dlstreamer/tree/main/samples/gstreamer/gst_launch/vehicle_pedestrian_tracking">Vehicle and Pedestrian Tracking</a></strong> — CLI</p>
    <p>Object tracking across frames.</p>
    <p><strong>Elements:</strong> <code>gvatrack</code>, <code>gvadetect</code>, <code>gvaclassify</code></p>
    <p><strong>Models:</strong> <code>yolo26s</code>, <code>dima806_vehicle_10_types_image_detection</code></p>
  </div>
</div>

<div style="display:flex; gap:1.5rem; align-items:flex-start; margin:1.5rem 0;">
  <div style="flex:0 0 200px; max-width:200px;">

  ![Vehicle Counter with gvaanalytics Tripwires](../_images/sample-gvaanalytics-tripwire-thumb.jpg)

  </div>
  <div style="flex:1;">
    <p style="font-size:1.1rem;"><strong><a href="https://github.com/open-edge-platform/dlstreamer/tree/main/samples/gstreamer/python/gvaanalytics_tripwire">Vehicle Counter with gvaanalytics Tripwires</a></strong> — Python</p>
    <p>Count vehicles crossing a virtual line in both directions using tripwires.</p>
    <p><strong>Elements:</strong> <code>gvaanalytics</code>, <code>gvatrack</code></p>
    <p><strong>Models:</strong> <code>yolo11n</code></p>
  </div>
</div>

<div style="display:flex; gap:1.5rem; align-items:flex-start; margin:1.5rem 0;">
  <div style="flex:0 0 200px; max-width:200px;">

  ![Smart NVR for Lane Hogging Detection](../_images/sample-smart-nvr-thumb.jpg)

  </div>
  <div style="flex:1;">
    <p style="font-size:1.1rem;"><strong><a href="https://github.com/open-edge-platform/dlstreamer/tree/main/samples/gstreamer/python/smart_nvr">Smart NVR for Lane Hogging Detection</a></strong> — Python</p>
    <p>Build an NVR with custom analytics and video storage to detect lane-hogging events.</p>
    <p><strong>Elements:</strong> <code>gvaanalytics_py</code>, <code>gvarecorder_py</code></p>
    <p><strong>Models:</strong> <code>rtdetr_v2_r50vd</code> (RT-DETRv2)</p>
  </div>
</div>

### Vision-Language Models (VLM) & GenAI

<div style="display:flex; gap:1.5rem; align-items:flex-start; margin:1.5rem 0;">
  <div style="flex:0 0 200px; max-width:200px;">

  ![Using VLM Models with gvagenai](../_images/sample_app_template.jpg)

  </div>
  <div style="flex:1;">
    <p style="font-size:1.1rem;"><strong><a href="https://github.com/open-edge-platform/dlstreamer/tree/main/samples/gstreamer/gst_launch/gvagenai">Using VLM Models with gvagenai</a></strong> — CLI</p>
    <p>Video summarization with MiniCPM-V.</p>
    <p><strong>Elements:</strong> <code>gvagenai</code></p>
    <p><strong>Models:</strong> <code>MiniCPM-V</code>, <code>Phi-4-multimodal-instruct</code> or <code>Gemma-3</code></p>
  </div>
</div>

<div style="display:flex; gap:1.5rem; align-items:flex-start; margin:1.5rem 0;">
  <div style="flex:0 0 200px; max-width:200px;">

  ![VLM Alerts](../_images/sample-vlm-alerts-thumb.jpg)

  </div>
  <div style="flex:1;">
    <p style="font-size:1.1rem;"><strong><a href="https://github.com/open-edge-platform/dlstreamer/tree/main/samples/gstreamer/python/vlm_alerts">VLM Alerts</a></strong> — Python</p>
    <p>Edge alerting pipeline that generates structured JSON alerts per frame with annotated video.</p>
    <p><strong>Elements:</strong> <code>gvagenai</code></p>
    <p><strong>Models:</strong> Configurable VLM (e.g. <code>Qwen2.5-VL</code>, <code>InternVL</code>)</p>
  </div>
</div>

<div style="display:flex; gap:1.5rem; align-items:flex-start; margin:1.5rem 0;">
  <div style="flex:0 0 200px; max-width:200px;">

  ![VLM-assisted Self Checkout](../_images/sample-vlm-self-checkout-thumb.jpg)

  </div>
  <div style="flex:1;">
    <p style="font-size:1.1rem;"><strong><a href="https://github.com/open-edge-platform/dlstreamer/tree/main/samples/gstreamer/python/vlm_self_checkout">VLM-assisted Self Checkout</a></strong> — Python</p>
    <p>Combine CV object detection with a VLM for item classification, running both locally on edge.</p>
    <p><strong>Elements:</strong> <code>gvadetect</code>, <code>gvagenai</code></p>
    <p><strong>Models:</strong> <code>yolo26s</code>, <code>MiniCPM-V-4_5</code></p>
  </div>
</div>

<div style="display:flex; gap:1.5rem; align-items:flex-start; margin:1.5rem 0;">
  <div style="flex:0 0 200px; max-width:200px;">

  ![ONVIF Camera Analytics Validation](../_images/sample-onvif-camera-analytics-validation-thumb.jpg)

  </div>
  <div style="flex:1;">
    <p style="font-size:1.1rem;"><strong><a href="https://github.com/open-edge-platform/dlstreamer/tree/main/samples/gstreamer/python/onvif_camera_analytics_validation">ONVIF Camera Analytics Validation</a></strong> — Python</p>
    <p>Use a VLM as an additional validation layer for ONVIF-enabled analytics cameras.</p>
    <p><strong>Elements:</strong> <code>gvagenai</code></p>
    <p><strong>Models:</strong> Configurable VLM</p>
  </div>
</div>

<div style="display:flex; gap:1.5rem; align-items:flex-start; margin:1.5rem 0;">
  <div style="flex:0 0 200px; max-width:200px;">

  ![Image Embeddings Generation with ViT](../_images/sample_app_template.jpg)

  </div>
  <div style="flex:1;">
    <p style="font-size:1.1rem;"><strong><a href="https://github.com/open-edge-platform/dlstreamer/tree/main/samples/gstreamer/gst_launch/lvm">Image Embeddings Generation with ViT</a></strong> — CLI</p>
    <p>Generate image embeddings using the Vision Transformer component of a CLIP model.</p>
    <p><strong>Elements:</strong> <code>gvainference</code></p>
    <p><strong>Models:</strong> <code>clip-vit-large-patch14</code> (CLIP ViT)</p>
  </div>
</div>

### Audio analytics

<div style="display:flex; gap:1.5rem; align-items:flex-start; margin:1.5rem 0;">
  <div style="flex:0 0 200px; max-width:200px;">

  ![Audio Event Detection](../_images/sample_app_template.jpg)

  </div>
  <div style="flex:1;">
    <p style="font-size:1.1rem;"><strong><a href="https://github.com/open-edge-platform/dlstreamer/tree/main/samples/gstreamer/gst_launch/audio_detect">Audio Event Detection</a></strong> — CLI</p>
    <p>Audio event detection, converting results to JSON.</p>
    <p><strong>Elements:</strong> <code>gvaaudiodetect</code>, <code>gvametaconvert</code>, <code>gvametapublish</code></p>
    <p><strong>Models:</strong> <code>aclnet</code></p>
  </div>
</div>

<div style="display:flex; gap:1.5rem; align-items:flex-start; margin:1.5rem 0;">
  <div style="flex:0 0 200px; max-width:200px;">

  ![Audio Transcription](../_images/sample_app_template.jpg)

  </div>
  <div style="flex:1;">
    <p style="font-size:1.1rem;"><strong><a href="https://github.com/open-edge-platform/dlstreamer/tree/main/samples/gstreamer/gst_launch/audio_transcribe">Audio Transcription</a></strong> — CLI</p>
    <p>Speech transcription using an OpenVINO GenAI Whisper model.</p>
    <p><strong>Elements:</strong> <code>gvaaudiotranscribe</code></p>
    <p><strong>Models:</strong> <code>whisper</code></p>
  </div>
</div>

### 3D: LiDAR & radar

<div style="display:flex; gap:1.5rem; align-items:flex-start; margin:1.5rem 0;">
  <div style="flex:0 0 200px; max-width:200px;">

  ![PointPillars Inference with g3dinference](../_images/sample_app_template.jpg)

  </div>
  <div style="flex:1;">
    <p style="font-size:1.1rem;"><strong><a href="https://github.com/open-edge-platform/dlstreamer/tree/main/samples/gstreamer/gst_launch/g3dinference">PointPillars Inference with g3dinference</a></strong> — CLI</p>
    <p>Complete LiDAR-only 3D detection pipeline.</p>
    <p><strong>Elements:</strong> <code>g3dlidarparse</code>, <code>g3dinference</code></p>
    <p><strong>Models:</strong> <code>PointPillars</code></p>
  </div>
</div>

<div style="display:flex; gap:1.5rem; align-items:flex-start; margin:1.5rem 0;">
  <div style="flex:0 0 200px; max-width:200px;">

  ![LiDAR Parse](../_images/sample_app_template.jpg)

  </div>
  <div style="flex:1;">
    <p style="font-size:1.1rem;"><strong><a href="https://github.com/open-edge-platform/dlstreamer/tree/main/samples/gstreamer/gst_launch/g3dlidarparse">LiDAR Parse</a></strong> — CLI</p>
    <p>LiDAR parsing pipeline.</p>
    <p><strong>Elements:</strong> <code>g3dlidarparse</code></p>
    <p><strong>Models:</strong> — (parsing only)</p>
  </div>
</div>

<div style="display:flex; gap:1.5rem; align-items:flex-start; margin:1.5rem 0;">
  <div style="flex:0 0 200px; max-width:200px;">

  ![Live LiDAR Capture](../_images/sample_app_template.jpg)

  </div>
  <div style="flex:1;">
    <p style="font-size:1.1rem;"><strong><a href="https://github.com/open-edge-platform/dlstreamer/tree/main/samples/gstreamer/gst_launch/g3dlidarsrc">Live LiDAR Capture</a></strong> — CLI</p>
    <p>Real-time LiDAR capture from a physical device (RoboSense via rs_driver).</p>
    <p><strong>Elements:</strong> <code>g3dlidarsrc</code>, <code>g3dinference</code></p>
    <p><strong>Models:</strong> <code>PointPillars</code></p>
  </div>
</div>

<div style="display:flex; gap:1.5rem; align-items:flex-start; margin:1.5rem 0;">
  <div style="flex:0 0 200px; max-width:200px;">

  ![Camera + 3D Object Fusion](../_images/sample_app_template.jpg)

  </div>
  <div style="flex:1;">
    <p style="font-size:1.1rem;"><strong><a href="https://github.com/open-edge-platform/dlstreamer/tree/main/samples/gstreamer/gst_launch/g3dobjectfuser">Camera + 3D Object Fusion</a></strong> — CLI</p>
    <p>Fuse 2D camera detections with 3D LiDAR detections.</p>
    <p><strong>Elements:</strong> <code>g3dobjectfuser</code>, <code>gvastreammux</code></p>
    <p><strong>Models:</strong> <code>yolo11n</code>, <code>PointPillars</code></p>
  </div>
</div>

<div style="display:flex; gap:1.5rem; align-items:flex-start; margin:1.5rem 0;">
  <div style="flex:0 0 200px; max-width:200px;">

  ![Radar Signal Process](../_images/sample_app_template.jpg)

  </div>
  <div style="flex:1;">
    <p style="font-size:1.1rem;"><strong><a href="https://github.com/open-edge-platform/dlstreamer/tree/main/samples/gstreamer/gst_launch/g3dradarprocess">Radar Signal Process</a></strong> — CLI</p>
    <p>mmWave radar signal processing with point-cloud detection, clustering and tracking.</p>
    <p><strong>Elements:</strong> <code>g3dradarprocess</code></p>
    <p><strong>Models:</strong> — (signal processing)</p>
  </div>
</div>

### Cameras & input sources

<div style="display:flex; gap:1.5rem; align-items:flex-start; margin:1.5rem 0;">
  <div style="flex:0 0 200px; max-width:200px;">

  ![RealSense™ Camera](../_images/sample_app_template.jpg)

  </div>
  <div style="flex:1;">
    <p style="font-size:1.1rem;"><strong><a href="https://github.com/open-edge-platform/dlstreamer/tree/main/samples/gstreamer/gst_launch/gvarealsense">RealSense™ Camera</a></strong> — CLI</p>
    <p>Capture a video stream from a 3D Intel RealSense™ Depth Camera.</p>
    <p><strong>Elements:</strong> <code>gvarealsense</code></p>
    <p><strong>Models:</strong> — (capture only)</p>
  </div>
</div>

<div style="display:flex; gap:1.5rem; align-items:flex-start; margin:1.5rem 0;">
  <div style="flex:0 0 200px; max-width:200px;">

  ![ONVIF Camera Discovery](../_images/sample-onvif-cameras-discovery-thumb.jpg)

  </div>
  <div style="flex:1;">
    <p style="font-size:1.1rem;"><strong><a href="https://github.com/open-edge-platform/dlstreamer/tree/main/samples/gstreamer/python/onvif_cameras_discovery">ONVIF Camera Discovery</a></strong> — Python</p>
    <p>Automatically discover ONVIF cameras on the network and launch pipelines for each.</p>
    <p><strong>Elements:</strong> <code>gvadetect</code></p>
    <p><strong>Models:</strong> Configurable detector</p>
  </div>
</div>

<div style="display:flex; gap:1.5rem; align-items:flex-start; margin:1.5rem 0;">
  <div style="flex:0 0 200px; max-width:200px;">

  ![Multi-camera deployments](../_images/sample_app_template.jpg)

  </div>
  <div style="flex:1;">
    <p style="font-size:1.1rem;"><strong><a href="https://github.com/open-edge-platform/dlstreamer/tree/main/samples/gstreamer/gst_launch/multi_stream">Multi-camera deployments</a></strong> — CLI</p>
    <p>Handle video streams from multiple cameras in a single application.</p>
    <p><strong>Elements:</strong> <code>gvadetect</code>, <code>gvafpscounter</code></p>
    <p><strong>Models:</strong> <code>yolo11s</code> (many YOLO variants)</p>
  </div>
</div>

<div style="display:flex; gap:1.5rem; align-items:flex-start; margin:1.5rem 0;">
  <div style="flex:0 0 200px; max-width:200px;">

  ![Multi-Stream Mux/Demux](../_images/sample_app_template.jpg)

  </div>
  <div style="flex:1;">
    <p style="font-size:1.1rem;"><strong><a href="https://github.com/open-edge-platform/dlstreamer/tree/main/samples/gstreamer/gst_launch/stream_mux_and_demux">Multi-Stream Mux/Demux</a></strong> — CLI</p>
    <p>Share a single inference pipeline across streams with per-source routing.</p>
    <p><strong>Elements:</strong> <code>gvastreammux</code>, <code>gvastreamdemux</code></p>
    <p><strong>Models:</strong> Configurable detector</p>
  </div>
</div>

### Metadata: publishing, access & visualization

<div style="display:flex; gap:1.5rem; align-items:flex-start; margin:1.5rem 0;">
  <div style="flex:0 0 200px; max-width:200px;">

  ![Metadata Publishing](../_images/sample_app_template.jpg)

  </div>
  <div style="flex:1;">
    <p style="font-size:1.1rem;"><strong><a href="https://github.com/open-edge-platform/dlstreamer/tree/main/samples/gstreamer/gst_launch/metapublish">Metadata Publishing</a></strong> — CLI</p>
    <p>Convert inference metadata to JSON and publish to file or Kafka/MQTT.</p>
    <p><strong>Elements:</strong> <code>gvametaconvert</code>, <code>gvametapublish</code></p>
    <p><strong>Models:</strong> <code>centerface</code>, <code>dima806_fairface_gender_image_detection</code>, <code>dima806_facial_age_image_detection</code></p>
  </div>
</div>

<div style="display:flex; gap:1.5rem; align-items:flex-start; margin:1.5rem 0;">
  <div style="flex:0 0 200px; max-width:200px;">

  ![gvaattachroi](../_images/sample_app_template.jpg)

  </div>
  <div style="flex:1;">
    <p style="font-size:1.1rem;"><strong><a href="https://github.com/open-edge-platform/dlstreamer/tree/main/samples/gstreamer/gst_launch/gvaattachroi">gvaattachroi</a></strong> — CLI</p>
    <p>Define the regions on which inference should be performed.</p>
    <p><strong>Elements:</strong> <code>gvaattachroi</code>, <code>gvadetect</code></p>
    <p><strong>Models:</strong> <code>yolov8s</code></p>
  </div>
</div>

<div style="display:flex; gap:1.5rem; align-items:flex-start; margin:1.5rem 0;">
  <div style="flex:0 0 200px; max-width:200px;">

  ![FPS Throttle](../_images/sample_app_template.jpg)

  </div>
  <div style="flex:1;">
    <p style="font-size:1.1rem;"><strong><a href="https://github.com/open-edge-platform/dlstreamer/tree/main/samples/gstreamer/gst_launch/gvafpsthrottle">FPS Throttle</a></strong> — CLI</p>
    <p>Throttle framerate independently of sink sync, without frame duplication or dropping.</p>
    <p><strong>Elements:</strong> <code>gvafpsthrottle</code></p>
    <p><strong>Models:</strong> —</p>
  </div>
</div>

<div style="display:flex; gap:1.5rem; align-items:flex-start; margin:1.5rem 0;">
  <div style="flex:0 0 200px; max-width:200px;">

  ![Watermark Metadata](../_images/sample-watermark-meta-thumb.jpg)

  </div>
  <div style="flex:1;">
    <p style="font-size:1.1rem;"><strong><a href="https://github.com/open-edge-platform/dlstreamer/tree/main/samples/gstreamer/python/watermark_meta">Watermark Metadata</a></strong> — Python</p>
    <p>Attach custom drawing primitives (hexagons, lines, circles, text) and render them.</p>
    <p><strong>Elements:</strong> <code>gvawatermark</code></p>
    <p><strong>Models:</strong> — (drawing only)</p>
  </div>
</div>

<div style="display:flex; gap:1.5rem; align-items:flex-start; margin:1.5rem 0;">
  <div style="flex:0 0 200px; max-width:200px;">

  ![Draw Face Attributes (C++)](../_images/sample_app_template.jpg)

  </div>
  <div style="flex:1;">
    <p style="font-size:1.1rem;"><strong><a href="https://github.com/open-edge-platform/dlstreamer/tree/main/samples/gstreamer/cpp/draw_face_attributes">Draw Face Attributes (C++)</a></strong> — C++</p>
    <p>Set a C callback to access frame metadata and visualize inference results.</p>
    <p><strong>Elements:</strong> <code>gvadetect</code>, <code>gvaclassify</code></p>
    <p><strong>Models:</strong> <code>centerface</code>, <code>dima806_facial_age_image_detection</code>, <code>dima806_fairface_gender_image_detection</code>, <code>dima806_face_emotions_image_detection</code></p>
  </div>
</div>

<div style="display:flex; gap:1.5rem; align-items:flex-start; margin:1.5rem 0;">
  <div style="flex:0 0 200px; max-width:200px;">

  ![Draw Face Attributes (Python)](../_images/sample_app_template.jpg)

  </div>
  <div style="flex:1;">
    <p style="font-size:1.1rem;"><strong><a href="https://github.com/open-edge-platform/dlstreamer/tree/main/samples/gstreamer/python/draw_face_attributes">Draw Face Attributes (Python)</a></strong> — Python</p>
    <p>Set a Python callback to access frame metadata and visualize inference results.</p>
    <p><strong>Elements:</strong> <code>gvadetect</code>, <code>gvaclassify</code></p>
    <p><strong>Models:</strong> <code>centerface</code>, <code>dima806_facial_age_image_detection</code>, <code>dima806_fairface_gender_image_detection</code>, <code>dima806_face_emotions_image_detection</code></p>
  </div>
</div>

<div style="display:flex; gap:1.5rem; align-items:flex-start; margin:1.5rem 0;">
  <div style="flex:0 0 200px; max-width:200px;">

  ![Open Close Valve](../_images/sample-open-close-valve-thumb.jpg)

  </div>
  <div style="flex:1;">
    <p style="font-size:1.1rem;"><strong><a href="https://github.com/open-edge-platform/dlstreamer/tree/main/samples/gstreamer/python/open_close_valve">Open Close Valve</a></strong> — Python</p>
    <p>Open/close a GStreamer <code>valve</code> branch from a callback based on detection results.</p>
    <p><strong>Elements:</strong> <code>gvadetect</code>, <code>valve</code></p>
    <p><strong>Models:</strong> <code>yolo11s</code>, <code>dima806_vehicle_10_types_image_detection</code></p>
  </div>
</div>

<div style="display:flex; gap:1.5rem; align-items:flex-start; margin:1.5rem 0;">
  <div style="flex:0 0 200px; max-width:200px;">

  ![Hello DL Streamer](../_images/sample-hello-dlstreamer-thumb.jpg)

  </div>
  <div style="flex:1;">
    <p style="font-size:1.1rem;"><strong><a href="https://github.com/open-edge-platform/dlstreamer/tree/main/samples/gstreamer/python/hello_dlstreamer">Hello DL Streamer</a></strong> — Python</p>
    <p>Build a detection pipeline, analyze metadata to count objects, and visualize results.</p>
    <p><strong>Elements:</strong> <code>gvadetect</code>, <code>gvawatermark</code></p>
    <p><strong>Models:</strong> <code>yolo11n</code></p>
  </div>
</div>

### Customization & extensibility

<div style="display:flex; gap:1.5rem; align-items:flex-start; margin:1.5rem 0;">
  <div style="flex:0 0 200px; max-width:200px;">

  ![Custom Post-Processing Library — Classification](../_images/sample_app_template.jpg)

  </div>
  <div style="flex:1;">
    <p style="font-size:1.1rem;"><strong><a href="https://github.com/open-edge-platform/dlstreamer/tree/main/samples/gstreamer/gst_launch/custom_postproc/classify">Custom Post-Processing Library — Classification</a></strong> — CLI, C++</p>
    <p>Write a custom post-processing library that converts emotion-classification outputs to GstAnalytics metadata.</p>
    <p><strong>Elements:</strong> <code>gvaclassify</code></p>
    <p><strong>Models:</strong> <code>centerface</code>, <code>hsemotion</code></p>
  </div>
</div>

<div style="display:flex; gap:1.5rem; align-items:flex-start; margin:1.5rem 0;">
  <div style="flex:0 0 200px; max-width:200px;">

  ![Custom Post-Processing Library — Detection](../_images/sample_app_template.jpg)

  </div>
  <div style="flex:1;">
    <p style="font-size:1.1rem;"><strong><a href="https://github.com/open-edge-platform/dlstreamer/tree/main/samples/gstreamer/gst_launch/custom_postproc/detect">Custom Post-Processing Library — Detection</a></strong> — CLI, C++</p>
    <p>Write a custom post-processing library that converts YOLOv11 tensor outputs to detection metadata.</p>
    <p><strong>Elements:</strong> <code>gvadetect</code></p>
    <p><strong>Models:</strong> <code>yolo11s</code></p>
  </div>
</div>

<div style="display:flex; gap:1.5rem; align-items:flex-start; margin:1.5rem 0;">
  <div style="flex:0 0 200px; max-width:200px;">

  ![gvapython — Face Detection and Classification](../_images/sample_app_template.jpg)

  </div>
  <div style="flex:1;">
    <p style="font-size:1.1rem;"><strong><a href="https://github.com/open-edge-platform/dlstreamer/tree/main/samples/gstreamer/gst_launch/gvapython/face_detection_and_classification">gvapython — Face Detection and Classification</a></strong> — CLI, Python</p>
    <p>Customize a pipeline with a Python script for inference post-processing.</p>
    <p><strong>Elements:</strong> <code>gvapython</code>, <code>gvadetect</code>, <code>gvaclassify</code></p>
    <p><strong>Models:</strong> <code>centerface</code>, <code>dima806_fairface_gender_image_detection</code>, <code>dima806_facial_age_image_detection</code></p>
  </div>
</div>

<div style="display:flex; gap:1.5rem; align-items:flex-start; margin:1.5rem 0;">
  <div style="flex:0 0 200px; max-width:200px;">

  ![gvapython — Save Frames with ROI](../_images/sample_app_template.jpg)

  </div>
  <div style="flex:1;">
    <p style="font-size:1.1rem;"><strong><a href="https://github.com/open-edge-platform/dlstreamer/tree/main/samples/gstreamer/gst_launch/gvapython/save_frames_with_ROI_only">gvapython — Save Frames with ROI</a></strong> — CLI, Python</p>
    <p>Use <code>gvapython</code> to save video frames containing detected objects to disk.</p>
    <p><strong>Elements:</strong> <code>gvapython</code>, <code>gvadetect</code></p>
    <p><strong>Models:</strong> <code>centerface</code></p>
  </div>
</div>

<div style="display:flex; gap:1.5rem; align-items:flex-start; margin:1.5rem 0;">
  <div style="flex:0 0 200px; max-width:200px;">

  ![python-elements — Face Detection and Classification](../_images/sample_app_template.jpg)

  </div>
  <div style="flex:1;">
    <p style="font-size:1.1rem;"><strong><a href="https://github.com/open-edge-platform/dlstreamer/tree/main/samples/gstreamer/gst_launch/python-elements/face_detection_and_classification">python-elements — Face Detection and Classification</a></strong> — CLI, Python</p>
    <p>Build a custom Python GStreamer element using the GstAnalytics metadata API.</p>
    <p><strong>Elements:</strong> <code>gvaagelogger_py</code>, <code>gvadetect</code>, <code>gvaclassify</code></p>
    <p><strong>Models:</strong> <code>YOLOv8-Face-Detection</code>, <code>fairface_age_image_detection</code>, <code>fairface_gender_image_detection</code></p>
  </div>
</div>

<div style="display:flex; gap:1.5rem; align-items:flex-start; margin:1.5rem 0;">
  <div style="flex:0 0 200px; max-width:200px;">

  ![python-elements — Save Frames with ROI](../_images/sample_app_template.jpg)

  </div>
  <div style="flex:1;">
    <p style="font-size:1.1rem;"><strong><a href="https://github.com/open-edge-platform/dlstreamer/tree/main/samples/gstreamer/gst_launch/python-elements/save_frames_with_ROI_only">python-elements — Save Frames with ROI</a></strong> — CLI, Python</p>
    <p>Build a custom Python GStreamer element to save frames with detected objects.</p>
    <p><strong>Elements:</strong> <code>gvaframesaver_py</code>, <code>gvadetect</code></p>
    <p><strong>Models:</strong> <code>YOLOv8-Face-Detection</code></p>
  </div>
</div>

<div style="display:flex; gap:1.5rem; align-items:flex-start; margin:1.5rem 0;">
  <div style="flex:0 0 200px; max-width:200px;">

  ![python-elements — Loitering Detection](../_images/sample_app_template.jpg)

  </div>
  <div style="flex:1;">
    <p style="font-size:1.1rem;"><strong><a href="https://github.com/open-edge-platform/dlstreamer/tree/main/samples/gstreamer/gst_launch/python-elements/loitering_detection">python-elements — Loitering Detection</a></strong> — CLI, Python</p>
    <p>Measure object dwell time with a custom Python element and render a visual alert when the threshold is exceeded.</p>
    <p><strong>Elements:</strong> <code>gvaanalytics</code>, <code>gvawatermark</code></p>
    <p><strong>Models:</strong> <code>yolo11s</code></p>
  </div>
</div>

<div style="display:flex; gap:1.5rem; align-items:flex-start; margin:1.5rem 0;">
  <div style="flex:0 0 200px; max-width:200px;">

  ![Face Detection and Classification (Python)](../_images/sample_app_template.jpg)

  </div>
  <div style="flex:1;">
    <p style="font-size:1.1rem;"><strong><a href="https://github.com/open-edge-platform/dlstreamer/tree/main/samples/gstreamer/python/face_detection_and_classification">Face Detection and Classification (Python)</a></strong> — Python</p>
    <p>Download models from Hugging Face, export to OpenVINO IR, and run inference.</p>
    <p><strong>Elements:</strong> <code>gvadetect</code>, <code>gvaclassify</code></p>
    <p><strong>Models:</strong> <code>YOLOv8-Face-Detection</code>, <code>fairface</code></p>
  </div>
</div>

### Performance & benchmarking

<div style="display:flex; gap:1.5rem; align-items:flex-start; margin:1.5rem 0;">
  <div style="flex:0 0 200px; max-width:200px;">

  ![Benchmark](../_images/sample_app_template.jpg)

  </div>
  <div style="flex:1;">
    <p style="font-size:1.1rem;"><strong><a href="https://github.com/open-edge-platform/dlstreamer/tree/main/samples/gstreamer/benchmark">Benchmark</a></strong> — CLI, Python</p>
    <p>Measure the performance of single- or multi-channel video analytics pipelines.</p>
    <p><strong>Elements:</strong> <code>gvadetect</code>, <code>gvafpscounter</code></p>
    <p><strong>Models:</strong> <code>centerface</code> (configurable)</p>
  </div>
</div>

<div style="display:flex; gap:1.5rem; align-items:flex-start; margin:1.5rem 0;">
  <div style="flex:0 0 200px; max-width:200px;">

  ![DL Streamer E2E Performance](../_images/sample-e2e-performance-thumb.jpg)

  </div>
  <div style="flex:1;">
    <p style="font-size:1.1rem;"><strong><a href="https://github.com/open-edge-platform/dlstreamer/tree/main/samples/gstreamer/e2e_performance">DL Streamer E2E Performance</a></strong> — Python</p>
    <p>Compare DL Streamer vs. OpenCV + OpenVINO throughput with a YOLO26s INT8 model.</p>
    <p><strong>Elements:</strong> <code>gvadetect</code></p>
    <p><strong>Models:</strong> <code>yolo26s</code> (INT8)</p>
  </div>
</div>

### Interoperability

<div style="display:flex; gap:1.5rem; align-items:flex-start; margin:1.5rem 0;">
  <div style="flex:0 0 200px; max-width:200px;">

  ![DL Streamer and DeepStream Coexistence](../_images/sample_app_template.jpg)

  </div>
  <div style="flex:1;">
    <p style="font-size:1.1rem;"><strong><a href="https://github.com/open-edge-platform/dlstreamer/tree/main/samples/gstreamer/python/coexistence">DL Streamer and DeepStream Coexistence</a></strong> — Python</p>
    <p>Run pipelines on DL Streamer and/or NVIDIA DeepStream side by side.</p>
    <p><strong>Elements:</strong> <code>gvadetect</code></p>
    <p><strong>Models:</strong> <code>yolov8</code> license-plate detector, <code>PP-OCRv4</code></p>
  </div>
</div>

<div style="display:flex; gap:1.5rem; align-items:flex-start; margin:1.5rem 0;">
  <div style="flex:0 0 200px; max-width:200px;">

  ![Coexistence Benchmark](../_images/sample_app_template.jpg)

  </div>
  <div style="flex:1;">
    <p style="font-size:1.1rem;"><strong><a href="https://github.com/open-edge-platform/dlstreamer/tree/main/samples/gstreamer/python/coexistence_benchmark">Coexistence Benchmark</a></strong> — Python</p>
    <p>Measure the maximum number of concurrent LPR streams on systems combining Intel and NVIDIA hardware.</p>
    <p><strong>Elements:</strong> <code>gvadetect</code></p>
    <p><strong>Models:</strong> <code>yolov8</code> license-plate detector, <code>PP-OCRv4</code></p>
  </div>
</div>

---

## Auto-generated reference applications

These end-to-end reference apps combine multiple elements into complete solutions.
Find them under
[samples/auto_generated_samples](https://github.com/open-edge-platform/dlstreamer/tree/main/samples/auto_generated_samples).

<div style="display:flex; gap:1.5rem; align-items:flex-start; margin:1.5rem 0;">
  <div style="flex:0 0 200px; max-width:200px;">

  ![DeepStream Test4 → DL Streamer Conversion](../_images/sample_app_template.jpg)

  </div>
  <div style="flex:1;">
    <p style="font-size:1.1rem;"><strong><a href="https://github.com/open-edge-platform/dlstreamer/tree/main/samples/auto_generated_samples/deepstream_python_conversion">DeepStream Test4 → DL Streamer Conversion</a></strong> — CLI</p>
    <p>DL Streamer equivalent of NVIDIA's deepstream-test4 with YOLO11n detection and metadata publishing.</p>
    <p><strong>Elements:</strong> <code>gvadetect</code>, <code>gvametaconvert</code>, <code>gvametapublish</code></p>
    <p><strong>Models:</strong> <code>yolo11n</code></p>
  </div>
</div>

<div style="display:flex; gap:1.5rem; align-items:flex-start; margin:1.5rem 0;">
  <div style="flex:0 0 200px; max-width:200px;">

  ![DeepStream LPR App Conversion (C++)](../_images/sample_app_template.jpg)

  </div>
  <div style="flex:1;">
    <p style="font-size:1.1rem;"><strong><a href="https://github.com/open-edge-platform/dlstreamer/tree/main/samples/auto_generated_samples/deepstream_cpp_conversion">DeepStream LPR App Conversion (C++)</a></strong> — C++</p>
    <p>C++ conversion of NVIDIA's DeepStream LPR app — license plate detection, tracking and text recognition.</p>
    <p><strong>Elements:</strong> <code>gvadetect</code>, <code>gvatrack</code>, <code>gvaclassify</code></p>
    <p><strong>Models:</strong> YOLOv11, PaddleOCR</p>
  </div>
</div>

<div style="display:flex; gap:1.5rem; align-items:flex-start; margin:1.5rem 0;">
  <div style="flex:0 0 200px; max-width:200px;">

  ![License Plate Recognition](../_images/sample_app_template.jpg)

  </div>
  <div style="flex:1;">
    <p style="font-size:1.1rem;"><strong><a href="https://github.com/open-edge-platform/dlstreamer/tree/main/samples/auto_generated_samples/license_plate_recognition">License Plate Recognition</a></strong> — CLI</p>
    <p>Detect license plates with YOLOv11 and recognize text with PaddleOCR.</p>
    <p><strong>Elements:</strong> <code>gvadetect</code>, <code>gvainference</code></p>
    <p><strong>Models:</strong> <code>YOLOv11</code>, <code>PaddleOCR</code></p>
  </div>
</div>

<div style="display:flex; gap:1.5rem; align-items:flex-start; margin:1.5rem 0;">
  <div style="flex:0 0 200px; max-width:200px;">

  ![Multi-Stream Compose](../_images/sample_app_template.jpg)

  </div>
  <div style="flex:1;">
    <p style="font-size:1.1rem;"><strong><a href="https://github.com/open-edge-platform/dlstreamer/tree/main/samples/auto_generated_samples/multi_stream_compose">Multi-Stream Compose</a></strong> — CLI</p>
    <p>Multi-camera analytics with composite WebRTC output, on-demand recording and a 2x2 GPU-accelerated mosaic.</p>
    <p><strong>Elements:</strong> <code>gvadetect</code>, <code>gvastreammux</code>, <code>gvawatermark</code></p>
    <p><strong>Models:</strong> <code>yolo11s</code></p>
  </div>
</div>

<div style="display:flex; gap:1.5rem; align-items:flex-start; margin:1.5rem 0;">
  <div style="flex:0 0 200px; max-width:200px;">

  ![People Detection and Tracking with Deep SORT](../_images/sample_app_template.jpg)

  </div>
  <div style="flex:1;">
    <p style="font-size:1.1rem;"><strong><a href="https://github.com/open-edge-platform/dlstreamer/tree/main/samples/auto_generated_samples/people_detection_tracking">People Detection and Tracking with Deep SORT</a></strong> — CLI</p>
    <p>Detect and track people using YOLO26m and Deep SORT with a Mars-Small-128 re-ID model.</p>
    <p><strong>Elements:</strong> <code>gvadetect</code>, <code>gvatrack</code></p>
    <p><strong>Models:</strong> <code>yolo26m</code>, <code>mars-small128</code></p>
  </div>
</div>

<div style="display:flex; gap:1.5rem; align-items:flex-start; margin:1.5rem 0;">
  <div style="flex:0 0 200px; max-width:200px;">

  ![Pose Estimation Compose](../_images/sample_app_template.jpg)

  </div>
  <div style="flex:1;">
    <p style="font-size:1.1rem;"><strong><a href="https://github.com/open-edge-platform/dlstreamer/tree/main/samples/auto_generated_samples/pose_estimation_compose">Pose Estimation Compose</a></strong> — CLI</p>
    <p>Run 4 YOLO pose models in parallel on the same video and composite results into a 2x2 mosaic.</p>
    <p><strong>Elements:</strong> <code>gvaclassify</code>, <code>gvawatermark</code></p>
    <p><strong>Models:</strong> <code>yolo26n-pose</code>, <code>yolo11n-pose</code>, <code>yolov8n-pose</code>, <code>yolov8l-pose</code></p>
  </div>
</div>

<div style="display:flex; gap:1.5rem; align-items:flex-start; margin:1.5rem 0;">
  <div style="flex:0 0 200px; max-width:200px;">

  ![Safety Compliance Monitor](../_images/sample_app_template.jpg)

  </div>
  <div style="flex:1;">
    <p style="font-size:1.1rem;"><strong><a href="https://github.com/open-edge-platform/dlstreamer/tree/main/samples/auto_generated_samples/safety_compliance">Safety Compliance Monitor</a></strong> — CLI</p>
    <p>Detect and track workers and use Qwen2.5-VL to verify helmet and harness compliance.</p>
    <p><strong>Elements:</strong> <code>gvadetect</code>, <code>gvatrack</code>, <code>gvagenai</code></p>
    <p><strong>Models:</strong> <code>yolo26m</code>, <code>Qwen2.5-VL-3B</code></p>
  </div>
</div>

<div style="display:flex; gap:1.5rem; align-items:flex-start; margin:1.5rem 0;">
  <div style="flex:0 0 200px; max-width:200px;">

  ![Smart NVR — Event-Based Recording](../_images/sample_app_template.jpg)

  </div>
  <div style="flex:1;">
    <p style="font-size:1.1rem;"><strong><a href="https://github.com/open-edge-platform/dlstreamer/tree/main/samples/auto_generated_samples/smart_nvr">Smart NVR — Event-Based Recording</a></strong> — CLI</p>
    <p>Detect people with YOLO11n and record video only when a person is present.</p>
    <p><strong>Elements:</strong> <code>gvadetect</code></p>
    <p><strong>Models:</strong> <code>yolo11n</code></p>
  </div>
</div>
