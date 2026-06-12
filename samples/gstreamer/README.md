# Deep Learning Streamer (DL Streamer) Samples

Samples are simple applications that demonstrate how to use the DL Streamer. The samples are available in the `/opt/intel/dlstreamer/samples` directory.

Samples separated into several categories:
1. gst_launch command-line samples (samples construct GStreamer pipeline via [gst-launch-1.0](https://gstreamer.freedesktop.org/documentation/tools/gst-launch.html) command-line utility)
    * [Action Recognition Sample](./gst_launch/action_recognition/README.md) - demonstrates action recognition via video_inference bin element
    * [Audio Event Detection Sample](./gst_launch/audio_detect/README.md) - constructs audio event detection pipeline example with [gvaaudiodetect](../../docs/user-guide/elements/gvaaudiodetect.md) element and uses [gvametaconvert](../../docs/user-guide/elements/gvametaconvert.md), [gvametapublish](../../docs/user-guide/elements/gvametapublish.md) elements to convert audio event metadata with inference results into JSON format and to print on standard out
    * [Audio Transcription Sample](./gst_launch/audio_transcribe/README.md) - performs audio transcription using OpenVino GenAI model (whisper) with [gvaaudiotranscribe](../..//docs/user-guide/elements/gvaaudiotranscribe.md)
    * [Custom Post-Processing Library Sample - Classification](./gst_launch/custom_postproc/classify/README.md) - demonstrates how to create custom post-processing library for emotion classification model outputs conversion to classification metadata using GStreamer Analytics framework
    * [Custom Post-Processing Library Sample - Detection](./gst_launch/custom_postproc/detect/README.md) - demonstrates how to create custom post-processing library for YOLOv11 tensor outputs conversion to detection metadata using GStreamer Analytics framework
    * [Depth Estimation Sample](./gst_launch/depth_estimation/README.md) - demonstrates YOLO11n object detection followed by Depth Anything V2 depth estimation on detected regions
    * [Detection with Yolo](./gst_launch/detection_with_yolo/README.md) - demonstrates how to use publicly available Yolo models for object detection and classification
    * [Face Detection And Classification Sample](./gst_launch/face_detection_and_classification/README.md) - constructs object detection and classification pipeline example with [gvadetect](../../docs/user-guide/elements/gvadetect.md) and [gvaclassify](../../docs/user-guide/elements/gvaclassify.md) elements to detect faces and estimate age, gender, emotions and landmark points
    * [PointPillars Inference with g3dinference](./gst_launch/g3dinference/README.md) - demonstrates a complete LiDAR-only 3D detection pipeline based on `g3dlidarparse` and `g3dinference` elements
    * [LiDAR Parse Sample](./gst_launch/g3dlidarparse/README.md) - demonstrates LiDAR parsing pipeline with `g3dlidarparse` element
    * [Radar Signal Process Sample](./gst_launch/g3dradarprocess/README.md) - demonstrates how to use the `g3dradarprocess` element for millimeter-wave radar signal processing with point cloud detection, clustering, and tracking
    * [Deployment of Geti™ models](./gst_launch/geti_deployment/README.md) - demonstrates how to deploy models trained with Geti™ Platform for object detection, anomaly detection and classification tasks
    * [gvaattachroi](./gst_launch/gvaattachroi/README.md) - demonstrates how to use gvaattachroi to define the regions on which the inference should be performed
    * [FPS Throttle](./gst_launch/gvafpsthrottle/README.md) - demonstrates how to use [gvafpsthrottle](../../docs/user-guide/elements/gvafpsthrottle.md) element to throttle framerate independent of sink synchronization and without frame duplication or dropping
    * [Using VLM Models With gvagenai Element](./gst_launch/gvagenai/README.md) - demonstrates how to use the `gvagenai` element with MiniCPM-V for video summarization
    * [gvapython face_detection_and_classification Sample](./gst_launch/gvapython/face_detection_and_classification/README.md) - demonstrates pipeline customization with [gvapython](../../docs/user-guide/elements/gvapython.md) element and application provided Python script for inference post-processing
    * [gvapython save frames with ROI Sample](./gst_launch/gvapython/save_frames_with_ROI_only/README.md) - demonstrates [gvapython](../../docs/user-guide/elements/gvapython.md) element for saving video frames with detected objects to disk
    * [Real Sense camera sample](./gst_launch/gvarealsense/README.md) - demonstrates how to capture video stream from a 3D RealSense™ Depth Camera using DL Streamer's gvarealsense element
    * [Human Pose Estimation Sample](./gst_launch/human_pose_estimation/README.md) - demonstrates human pose estimation with full-frame inference via [gvaclassify](../../docs/user-guide/elements/gvaclassify.md) element
    * [Instance Segmentation Sample](./gst_launch/instance_segmentation/README.md) - demonstrates Instance Segmentation via object_detect and object_classify bin elements
    * [License Plate Recognition Sample](./gst_launch/license_plate_recognition/README.md) - demonstrates the use of the Yolo detector together with the optical character recognition model
    * [Image Embeddings Generation with ViT](./gst_launch/lvm/README.md) - demonstrates how to generate image embeddings using the Vision Transformer component of a CLIP model
    * [Metadata Publishing Sample](./gst_launch/metapublish/README.md) - demonstrates how [gvametaconvert](../../docs/user-guide/elements/gvametaconvert.md) and [gvametapublish](../../docs/user-guide/elements/gvametapublish.md) elements are used for converting metadata with inference results into JSON format and publishing to file or Kafka/MQTT message bus
    * [Motion Detect Sample](./gst_launch/motion_detect/README.md) - demonstrates the `gvamotiondetect` element for running object detection over motion ROIs, supporting GPU and CPU paths
    * [Multi-camera deployments](./gst_launch/multi_stream/README.md) - demonstrates how to handle video streams from multiple cameras with one instance of DL Streamer application
    * [python-elements Face Detection and Classification Sample](./gst_launch/python-elements/face_detection_and_classification/README.md) - demonstrates face detection and age logging using `gvadetect`, `gvaclassify` and a custom Python GStreamer element (`gvaagelogger_py`) with GstAnalytics metadata API
    * [python-elements Save Frames with ROI Sample](./gst_launch/python-elements/save_frames_with_ROI_only/README.md) - demonstrates saving video frames with detected objects using a custom Python GStreamer element (`gvaframesaver_py`) with GstAnalytics metadata API
    * [Multi-Stream Mux/Demux Sample](./gst_launch/stream_mux_and_demux/README.md) - demonstrates how to use `gvastreammux` and `gvastreamdemux` elements for multi-stream video inference through a single shared pipeline with PTS-aligned batching, configurable cross-pad PTS normalization (`sync-mode`), and per-source output routing
    * [Vehicle and Pedestrian Tracking Sample](./gst_launch/vehicle_pedestrian_tracking/README.md) - demonstrates object tracking via [gvatrack](../../docs/user-guide/elements/gvatrack.md) element
2. C++ samples
    * [Draw Face Attributes C++ Sample](./cpp/draw_face_attributes/README.md) - constructs pipeline and sets "C" callback to access frame metadata and visualize inference results
3. Python samples
     * [Draw Face Attributes Python Sample](./python/draw_face_attributes/README.md) - constructs pipeline and sets Python callback to access frame metadata and visualize inference results
    * [Face Detection and Classification Python Sample](./python/face_detection_and_classification/README.md) - downloads face detection and classification models from Hugging Face, exports to OpenVINO IR, and runs inference with `gvadetect` and `gvaclassify`
    * [Vehicle Counter with gvaanalytics Tripwires](./python/gvaanalytics_tripwire/README.md) - uses the `gvaanalytics` element with tripwires to count vehicles crossing a virtual line in both directions
    * [Hello DL Streamer Sample](./python/hello_dlstreamer/README.md) - constructs an object detection pipeline, add logic to analyze metadata and count objects and visualize results along with object count summary in a local window
    * [ONVIF Camera Discovery Sample](./python/onvif_cameras_discovery/README.md) - demonstrates automatic discovery of ONVIF-compatible cameras on the network and launches corresponding DL Streamer pipelines for video analysis.
    * [ONVIF Camera Analytics Validation Sample](./python/onvif_camera_analytics_validation/README.md) - sample application that uses VLM with DL Streamer to provide an additional validation layer for ONVIF-enabled analytics cameras.
    * [Open Close Valve Sample](./python/open_close_valve/README.md) - constructs pipeline with two sinks. One of them has [GStreamer valve element](https://gstreamer.freedesktop.org/documentation/coreelements/valve.html?gi-language=python), which is managed based object detection result and opened/closed by callback.
    * [Prompt-based Object Detection](./python/prompted_detection/README.md) - searches a video for user-defined objects using an open vocabulary detection model (YOLOE) integrated with a DL Streamer pipeline
    * [Smart NVR for Lane Hogging Detection](./python/smart_nvr/README.md) - builds a simple NVR with custom analytics (`gvaanalytics_py`) and custom video storage (`gvarecorder_py`) to detect line-hogging events
    * [VLM Alerts](./python/vlm_alerts/README.md) - edge AI alerting pipeline using Vision-Language Models to generate structured JSON alerts per frame with confidence scores and annotated video output
    * [VLM-assisted Self Checkout](./python/vlm_self_checkout/README.md) - self-checkout pipeline combining CV object detection with a VLM for item classification, running both models locally on edge
    * [Watermark Metadata Sample](./python/watermark_meta/README.md) - attaches custom drawing primitives (hexagons, lines, circles, text) to video frames using the watermark metadata API and renders them with `gvawatermark`
4. Benchmark
    * [Benchmark Sample](./benchmark/README.md) - measures overall performance of single-channel or multi-channel video analytics pipelines
5. DL Streamer and DeepStream Coexistence
   * [DL Streamer and DeepStream Coexistence Sample](./python/coexistence/README.md) - runs pipelines on DL Streamer and/or DeepStream
6. E2E Performance
    * [DL Streamer E2E Performance](./e2e_performance/README.md) - benchmarking sample showcasing higher throughput on Intel Core Ultra processors via DL Streamer vs. OpenCV + OpenVINO, using YOLO26s INT8 model with pipelining, hardware video decoding, and zero-copy inference on iGPU
7. Auto-Generated Samples
    * [DeepStream Test4 to DL Streamer Conversion](../auto_generated_samples/deepstream_conversion/README.md) - DL Streamer equivalent of NVIDIA's deepstream-test4 with YOLO11n detection and metadata publishing to file/Kafka/MQTT
    * [License Plate Recognition](../auto_generated_samples/license_plate_recognition/README.md) - detects license plates with YOLOv11 and recognizes plate text with PaddleOCR, optimized for Intel Core Ultra 3
    * [Multi-Stream Compose](../auto_generated_samples/multi_stream_compose/README.md) - multi-camera video analytics with composite WebRTC output, on-demand recording, and 2x2 GPU-accelerated mosaic from 4 streams
    * [People Detection and Tracking with Deep SORT](../auto_generated_samples/people_detection_tracking/README.md) - detects and tracks people using YOLO26m and Deep SORT with Mars-Small-128 re-ID model
    * [Pose Estimation Compose](../auto_generated_samples/pose_estimation_compose/README.md) - runs 4 YOLO pose models in parallel on the same video and composites results into a 2x2 mosaic
    * [Safety Compliance Monitor](../auto_generated_samples/safety_compliance/README.md) - monitors construction site safety by detecting workers, tracking them, and using Qwen2.5-VL to verify helmet and harness compliance
    * [Smart NVR — Event-Based Recording](../auto_generated_samples/smart_nvr/README.md) - detects people with YOLO11n and records video only when a person is present

## How To Build And Run

Samples with C/C++ code provide `build_and_run.sh` shell script to build application via cmake before execution.

Other samples (without C/C++ code) provide .sh script for constructing and executing gst-launch or Python command line.

## DL Models

DL Streamer samples use pre-trained models from OpenVINO™ Toolkit [Open Model Zoo](https://github.com/openvinotoolkit/open_model_zoo)

Before running samples, run script `download_omz_models.sh` once to download all models required for samples. The script located in `samples` top folder.
> **NOTE**: To install all necessary requirements for `download_omz_models.sh` script run this command:
```sh
python3 -m pip install --upgrade pip
python3 -m pip install openvino-dev[onnx]
```
> **NOTE**: To install all available frameworks run this command:
```sh
python3 -m pip install openvino-dev[caffe,onnx,tensorflow2,pytorch,mxnet]
```

## Input video

First command-line parameter in DL Streamer samples specifies input video and supports
* local video file
* web camera device (ex. `/dev/video0`)
* RTSP camera (URL starting with `rtsp://`) or other streaming source (ex URL starting with `http://`)

If command-line parameter not specified, most samples by default stream video example from predefined HTTPS link, so require internet connection.

> **NOTE**: Most samples set property `sync=false` in video sink element to disable real-time synchronization and run pipeline as fast as possible. Change to `sync=true` to run pipeline with real-time speed.

## Running on remote machine

In order to run samples on remote machine over SSH with X Forwarding you should force usage of `ximagesink` as video sink first:
```sh
source ./force_ximagesink.sh
```

---

*Intel, the Intel logo and Intel Geti are trademarks of Intel Corporation or its subsidiaries.*
