# Release Notes: Deep Learning Streamer (DL Streamer) Pipeline Framework

## Version 2026.2

**Release Date**: September 9, 2026

**Key highlights**:

- Heterogeneous multi-sensor batching: gvastreammux was reimplemented on PTS-based cross-stream batching and now multiplexes video and LiDAR streams in a single pipeline.
- Flexible generative AI deployment: the new gvagenai backend registry with an openai-http backend lets Vision Language Model inference run locally on Intel hardware or be delegated to a remote service without changing the pipeline.
- Broader standards-based metadata: segmentation, raw tensor (GstAnalyticsTensorMtd) and 3D object detection results are now carried as upstream GstAnalytics metadata, easing migration away from Intel-specific extensions.
- New analytics capabilities: dwell time and evaluation-point support in gvaanalytics enable time-in-zone and loitering detection use cases, shipped with a ready-to-run sample.
- Power-aware optimization: the DL Streamer Optimizer can now optimize pipelines against power consumption in addition to throughput.
- End-to-end 3D sensor pipeline: three new elements (g3dlidarsrc, g3dobjectfuser, g3drender) complete the LiDAR workflow from source through fusion to rendering entirely within DL Streamer.
- Expanded model coverage: RF-DETR and RF-DETR-SEG converters add modern transformer-based detection and segmentation models.
- Improved developer onboarding: a new end-to-end tutorial, restructured documentation site, simplified Get Started flow, and per-source model download scripts.
- Component updates: OpenVINO 2026.2, NPU driver 1.33, and refreshed GPU drivers.

Deep Learning Streamer (DL Streamer) 2026.2 extends the framework beyond classic video analytics into full 3D sensor processing, broadens standards-based metadata coverage, and opens generative AI inference to remote backends.

Metadata standardization continues: segmentation results, raw tensors and 3D object detections are now carried as upstream GstAnalytics metadata, reducing reliance on Intel-specific extensions ahead of their planned deprecation. The gvagenai element gained a pluggable backend registry with an openai-http backend, so Vision Language Model inference can run locally on Intel hardware or be delegated to a remote service without changing the pipeline.

This release completes the 3D sensing pipeline with three new elements (g3dlidarsrc, g3dobjectfuser, g3drender), so a LiDAR workload can now be sourced, parsed, inferred, fused and rendered entirely within DL Streamer. The stream multiplexer (gvastreammux) was reimplemented on PTS-based cross-stream batching and gained heterogeneous-source support, allowing video and LiDAR to be batched together in one pipeline.

> **Note:** DL Streamer 2026.2 updates to OpenVINO 2026.2 and NPU driver 1.33. Model downloading has migrated to new per-source scripts (HuggingFace, TIMM, Ultralytics); existing automation that calls the previous scripts should be reviewed.

Deep Learning Streamer (DL Streamer) Pipeline Framework is a streaming media analytics framework, based on GStreamer* multimedia framework, for creating complex media analytics pipelines. It ensures pipeline interoperability and provides optimized media, and inference operations using Intel® Distribution of OpenVINO™ Toolkit Inference Engine backend, across Intel® architecture, CPU, discrete GPU, integrated GPU and NPU.

The complete solution leverages:

- Open source GStreamer* framework for pipeline management
- GStreamer* plugins for input and output such as media files and real-time streaming from camera or network
- Video decode and encode plugins, either CPU optimized plugins or GPU-accelerated plugins based on VAAPI
- Deep Learning models converted from training frameworks TensorFlow*, Caffe* etc.
- The following elements in the Pipeline Framework repository:

  | Element | Description |
  | --- | --- |
  | [gvaanalytics](./elements/gvaanalytics.md) | Provides a framework for custom analytics logic on detected objects and metadata. |
  | [gvaattachroi](./elements/gvaattachroi.md) | Adds user-defined regions of interest to perform inference on, instead of full frame. |
  | [gvaaudiodetect](./elements/gvaaudiodetect.md) | Performs audio event detection using AclNet model. |
  | [gvaaudiotranscribe](./elements/gvaaudiotranscribe.md) | Performs audio transcription using OpenVino GenAI Whisper model. |
  | [gvaclassify](./elements/gvaclassify.md) | Performs object classification. Accepts the ROI as an input and outputs classification results with the ROI metadata. |
  | [gvadetect](./elements/gvadetect.md) | Performs object detection on a full-frame or region of interest (ROI) using object detection models such as YOLOv4-v11, MobileNet SSD, Faster-RCNN etc. Outputs the ROI for detected objects. |
  | [gvafpscounter](./elements/gvafpscounter.md) | Measures frames per second across multiple streams in a single process. |
  | [gvafpsthrottle](./elements/gvafpsthrottle.md) | Throttles the frame rate of a pipeline to a specified FPS value. |
  | [gvagenai](./elements/gvagenai.md) | Performs inference with Vision Language Models using OpenVINO™ GenAI, accepts video and text prompt as an input, and outputs text description. It can be used to generate text summarization from video. |
  | [gvainference](./elements/gvainference.md) | Runs deep learning inference on a full-frame or ROI using any model with an RGB or BGR input. |
  | [gvametaaggregate](./elements/gvametaaggregate.md) | Aggregates inference results from multiple pipeline branches. |
  | [gvametaconvert](./elements/gvametaconvert.md) | Converts the metadata structure to the JSON format. |
  | [gvametapublish](./elements/gvametapublish.md) | Publishes the JSON metadata to MQTT or Kafka message brokers or files. |
  | [gvamotiondetect](./elements/gvamotiondetect.md) | Performs lightweight motion detection on NV12 video frames and emits motion regions of interest (ROIs) as analytics metadata. |
  | [gvapython](./elements/gvapython.md) | Provides a callback to execute user-defined Python functions on every frame. Can be used for metadata conversion, inference post-processing, and other tasks. |
  | [gvarealsense](./elements/gvarealsense.md) | Provides integration with Intel RealSense cameras, enabling video and depth stream capture for use in GStreamer pipelines. |
  | [gvastreammux](./elements/gvastreammux.md) | Multiplexes multiple input streams into a single pipeline with batch metadata support. |
  | [gvatrack](./elements/gvatrack.md) | Performs object tracking using zero-term, or imageless tracking algorithms. Assigns unique object IDs to the tracked objects. |
  | [gvawatermark](./elements/gvawatermark.md) | Overlays the metadata on the video frame to visualize the inference results. |
  | [g3dinference](./elements/g3dinference.md) | Performs deep learning inference on 3D LiDAR sensor data. |
  | [g3dlidarparse](./elements/g3dlidarparse.md) | Parses 3D LiDAR data for use in analytics pipelines. |
  | [g3dlidarsrc](./elements/g3dlidarsrc.md) | Source element that feeds 3D LiDAR data directly into analytics pipelines. |
  | [g3dobjectfuser](./elements/g3dobjectfuser.md) | Fuses 3D object detections across multiple sensors into a single coherent object set. |
  | [g3drender](./elements/g3drender.md) | Renders 3D analytics results with configurable camera projection. |
  | [g3dradarprocess](./elements/g3dradarprocess.md) | Processes 3D radar data for use in analytics pipelines. |

For the details on supported platforms, please refer to [System Requirements](./system_requirements.md).
For installing Pipeline Framework with the prebuilt binaries or Docker* or to build the binaries from the open source, refer to [Intel® DL Streamer Pipeline Framework installation guide](./install/install_guide_index.md).

**New**:

| Feature | Description |
| --------- | ------------- |
| GstAnalytics segmentation metadata | Segmentation results are now published as standard GstAnalytics metadata for consistent downstream consumption and JSON serialization. |
| Raw tensor metadata (GstAnalyticsTensorMtd) | New support for carrying raw inference tensors as standard analytics metadata, with accompanying reference documentation. |
| 3D object detection metadata | New metadata type for 3D object detections, documented together with the broader 3D sensor and detection metadata model. |
| Evaluation-point support in gvaanalytics | Custom analytics logic can now be bound to explicit evaluation points, giving precise control over where in the pipeline rules are applied. |
| Dwell time analytics | New dwell time logic in gvaanalytics enables time-in-zone use cases, delivered together with a loitering detection sample application. |
| gvagenai backend registry and openai-http backend | The gvagenai element now supports pluggable inference backends, including a new openai-http backend for remote/hosted VLM inference alongside local OpenVINO GenAI. |
| RF-DETR and RF-DETR-SEG converters | Added converter implementations enabling RF-DETR detection and RF-DETR-SEG segmentation models. |
| 3D LiDAR source element (g3dlidarsrc) | New source element that feeds 3D LiDAR data directly into analytics pipelines, removing the need for external ingestion tooling. |
| 3D object fuser element (g3dobjectfuser) | New element that fuses 3D object detections across multiple sensors into a single coherent object set. |
| 3D render element (g3drender) | New element for rendering 3D analytics results, with a configurable cam-proj camera projection parameter. |
| Heterogeneous gvastreammux sources | gvastreammux can now multiplex heterogeneous sources, batching video and LiDAR streams together in a single pipeline. |
| Loitering detection sample | New Python sample demonstrating dwell-time based loitering detection, including a custom watermark plugin. |
| Coexistence benchmark sample | New benchmark sample comparing DL Streamer and DeepStream on a license plate recognition workload. |
| New DL Streamer tutorial | New end-to-end tutorial added to the documentation site to accelerate developer onboarding. |
| New model downloading scripts | Model download reworked into dedicated per-source scripts for HuggingFace, TIMM and Ultralytics, each with its own model list and requirements file. |
| Skill Scanner CI workflow | New workflow performing automated security and validation scans on the DL Streamer Coding Agent skill. |
| Coding agent evaluation prompts and benchmark | Added evaluation prompts and a benchmark definition for measuring DL Streamer Coding Agent skill performance. |
| diff_report CI tool | New CI tool for visual inspection of failing tests, comparing produced JSON output against ground-truth reports. |
| Latency tracer and gvafpscounter in monolithic build | Latency tracer and gvafpscounter were backported to the monolithic build, making performance instrumentation available in that configuration. |
| DL Streamer video tutorials | New YouTube video tutorials embedded in the documentation and README to accelerate developer onboarding. |

**Improved**:

| Feature | Description |
| --------- | ------------- |
| gvastreammux reimplementation | Reimplemented to use presentation timestamps (PTS) for cross-stream batching, improving synchronization correctness across streams. |
| gvawatermark GPU blurring | Object blurring is now GPU-accelerated, with a stronger blur ratio for better privacy masking. |
| gvamotiondetect format coverage | Added I420 and YV12 support for system memory, broadening the range of pipelines that can use motion detection. |
| gvagenai configuration parsing | Reworked configuration parsing and refreshed the associated samples. |
| Optimizer power-based optimization | Added power-based optimization and reworked streams functionality to operate together with power optimizations. |
| Optimizer results handling | Reworked result reporting to properly represent fail states, with additional documentation and stability fixes. |
| g3dinference tuning | Default score threshold changed from 0 to 0.7 and the 3D detection z-coordinate was adjusted for more accurate output. |
| g3dlidarparse improvements | Added PTS and duration timestamp management, and corrected a log message unit from floats to points. |
| OpenVINO update | Updated to OpenVINO 2026.2, with OpenVINO GenAI export requirements aligned to the matching release. |
| NPU driver update | Updated to NPU driver 1.33 and removed the ZE_ENABLE_ALT_DRIVERS workaround. |
| GPU driver update | Updated GPU drivers, including the Windows GPU and NPU driver versions. |
| Documentation site restructure | New samples documentation site, simplified Get Started structure, separate table-of-contents sections, and a revamped main README and documentation landing page. |
| API reference documentation | Added detailed API reference documentation covering inference output and metadata. |
| gvaanalytics documentation and discoverability | Updated gvaanalytics documentation and added the gvaanalytics sample to the top-level Python samples README. |
| Python samples testability | Selected Python samples updated with additional inputs and pinned model versions, with the CI workflow adjusted accordingly. |
| ONVIF sample suite | Reworked the ONVIF application into a multi-library suite with dedicated sample apps. |
| Prompted Detection sample | Removed in-sample model downloading, improved inputs and updated the README. |
| face_detection sample | Extracted model downloading from the sample and bumped torch to 2.13.0. |
| Ultralytics from HuggingFace | Model download scripts can now fetch Ultralytics models from HuggingFace. |
| Smart NVR sample | Updated to comply with DL Streamer Coding Agent guidelines. |
| Depth estimation sample | Applied changes and fixes to the depth estimation sample. |
| open_close_valve sample | Updated the model used by the open_close_valve Python sample. |
| Windows build experience | Enhanced the Windows build script with winget repair and improved Python detection, and added a warning about DLL shadowing. |
| Dependency updates | Updated pip to 26.1.2, pillow to 12.3.0, onnx to 1.22.0, torch to 2.13.0, setuptools, and the GitHub Actions dependency group. |
| Test execution | Sped up unit tests to avoid timeouts on slow CI machines, standardized test environment setup via setup_dls_env.sh, and adjusted the g3dinference PointPillars test to run with score-threshold=0. |

**Fixed**:

| # | Description |
| --- | ------------- |
| 1 | Fixed an issue with resource management causing leaks under sustained operation. |
| 2 | Resolved CodeQL findings raised by static analysis. |
| 3 | Fixed incorrect dimension ordering passed into cv::Size in image processing. |
| 4 | Fixed HuggingFace model conversion failures. |
| 5 | Fixed the queue element destination pad name check. |
| 6 | Forced the RAW converter for gvainference regardless of model metadata, ensuring raw output is produced as expected. |
| 7 | Fixed HTTP Error 403 (Forbidden) when retrieving assets in Python samples. |
| 8 | Fixed Python requirements and the shared_utils path in Smart NVR and Face Detection samples. |
| 9 | Corrected the setup instructions in the vlm_alerts sample README. |
| 10 | Fixed the download paths for the pallet defect detection model. |
| 11 | Corrected the SHA in the HuggingFace download README and improved error handling. |
| 12 | Correctly handle exported model files and CLI exit codes in model download scripts. |
| 13 | Fixed template path resolution in add_vs_version_resource and dropped a redundant project() call in a subdirectory CMakeLists. |
| 14 | Added process checking to prevent installation conflicts. |
| 15 | Removed references to local GStreamer binaries to avoid conflicts with DL Streamer-installed binaries. |
| 16 | Preserved environment variables such as proxy settings when adding the Intel GPU PPA repository. |
| 17 | Fixed broken references in Supported Models and other documentation links. |
| 18 | Added a missing return value. |
| 19 | Fixed issues in hello_dlstreamer.sh. |

**Known Issues**:

| Issue | Description |
| ------- | ------------- |
| Preview Architecture 2.0 samples | Preview Architecture 2.0 samples have known issues with inference results and should not be used as a correctness reference. |
| Model download script migration | Model downloading moved to new per-source scripts. Automation built on the previous scripts may need to be updated; the earlier scripts were restored for compatibility but are considered legacy. |

## Legacy Features and Deprecation Timeline - 2026.2

List of the features and components to be deprecated in the future.

| Feature | Target | Replacement |
|---------|--------|-------------|
| gvapython element | end of Q4'2026 | usage of regular GStreamer Python bindings |
| GstVideoRegionOfInterest meta + Intel extensions | end of Q4'2026 | usage of GstAnalyticsMtd |
| Architecture 2.0 elements | end of Q4'2026 | Preserve essential components and ideas |
| Tiger Lake support | end of Q4'2026 | new supported units: Arrow Lake and Panther Lake |
| Ubuntu 22.04 | end of Q4'2026 | newer Ubuntu versions, including Ubuntu 26 |
| Availability of download_public_models.sh and download_omz_models.sh | end of Q4'2026 | new models downloading solution in `scripts/download_models` directory |

**Legal Information**:

GStreamer is an open source framework licensed under LGPL. See <https://gstreamer.freedesktop.org/documentation/frequently-asked-questions/licensing.html>. You are solely responsible for determining if your use of GStreamer requires any additional licenses. Intel is not responsible for obtaining any such licenses, nor liable for any licensing fees due, in connection with your use of GStreamer.

## Version 2026.1

**Key highlights**:

- DL Streamer Coding Agent: AI-assisted pipeline builder that translates natural-language descriptions into working DL Streamer Python apps or GStreamer command lines
- GstAnalytics metadata migration: full-frame GstAnalytics support, keypoints switched to GstAnalytics API, migration to upstream GStreamer 1.28+ API
- New elements: gvastreammux, gvaanalytics, g3dinference
- New model support: YOLO classification, PaddleOCRv5, GETI semantic segmentation
- New D3D11 backend for inference elements for Windows GPU processing
- PyTorch Image Models (TIMM) model import script
- Windows installer and expanded Windows sample coverage
- gvawatermark enhancements: custom full-frame text, dynamic Gaussian blur sized to ROI, metadata-controlled drawing
- Inference enhancements: CPU thread affinity parameter, skip-raw-tensors property, DepthConverter
- Optimizer enhancements: time reduction, pause/resume, partial results, file output, cross-stream batch/nireq grouping, detection-based validation
- Deep SORT tracker refactoring with enhanced detection and track structures
- New Python samples: gvaanalytics, inference performance optimizations, custom frame selection for VLM queries
- Dynamic ONVIF camera discovery with DLS pipeline startup
- DL Streamer ONVIF package as installable Python wheel
- Component updates: OpenVINO 2026.1, GStreamer 1.28.2, OpenCV 4.13.0, NPU driver 1.32.1, Ubuntu 24.04.4 kernel 6.17
- Introduced support for Intel® Core™ Processors - series 3 (Wildcat Lake)
- CI: self-hosted Kubernetes infrastructure, configurable tests, Fedora 41 image building

Deep Learning Streamer (DL Streamer) Pipeline Framework is a streaming media analytics framework, based on GStreamer* multimedia framework, for creating complex media analytics pipelines. It ensures pipeline interoperability and provides optimized media, and inference operations using Intel® Distribution of OpenVINO™ Toolkit Inference Engine backend, across Intel® architecture, CPU, discrete GPU, integrated GPU and NPU.

The complete solution leverages:

- Open source GStreamer* framework for pipeline management
- GStreamer* plugins for input and output such as media files and real-time streaming from camera or network
- Video decode and encode plugins, either CPU optimized plugins or GPU-accelerated plugins based on VAAPI
- Deep Learning models converted from training frameworks TensorFlow*, Caffe* etc.
- The following elements in the Pipeline Framework repository:

  | Element | Description |
  | --- | --- |
  | [gvaanalytics](./elements/gvaanalytics.md) | Provides a framework for custom analytics logic on detected objects and metadata. |
  | [gvaattachroi](./elements/gvaattachroi.md) | Adds user-defined regions of interest to perform inference on,   instead of full frame. |
  | [gvaaudiodetect](./elements/gvaaudiodetect.md) | Performs audio event detection using AclNet model. |
  | [gvaaudiotranscribe](./elements/gvaaudiotranscribe.md) | Performs audio transcription using OpenVino GenAI Whisper model. |
  | [gvaclassify](./elements/gvaclassify.md) | Performs object classification. Accepts the ROI as an input and   outputs classification results with the ROI metadata. |
  | [gvadetect](./elements/gvadetect.md) | Performs object detection on a full-frame or region of interest (ROI)   using object detection models such as YOLOv4-v11, MobileNet SSD, Faster-RCNN etc. Outputs the ROI for detected objects. |
  | [gvafpscounter](./elements/gvafpscounter.md) | Measures frames per second across multiple streams in a single   process. |
  | [gvafpsthrottle](./elements/gvafpsthrottle.md) | Throttles the frame rate of a pipeline to a specified FPS value. |
  | [gvagenai](./elements/gvagenai.md) | Performs inference with Vision Language Models using OpenVINO™ GenAI, accepts video and text prompt as an input, and outputs text description. It can be used to generate text summarization from video. |
  | [gvainference](./elements/gvainference.md) | Runs deep learning inference on a full-frame or ROI using any model with an RGB or BGR input. |
  | [gvametaaggregate](./elements/gvametaaggregate.md) | Aggregates inference results from multiple pipeline branches |
  | [gvametaconvert](./elements/gvametaconvert.md) | Converts the metadata structure to the JSON format. |
  | [gvametapublish](./elements/gvametapublish.md) | Publishes the JSON metadata to MQTT or Kafka message brokers or files. |
  | [gvamotiondetect](./elements/gvamotiondetect.md) | Performs lightweight motion detection on NV12 video frames and emits motion regions of interest (ROIs) as analytics metadata. |
  | [gvapython](./elements/gvapython.md) | Provides a callback to execute user-defined Python functions on every  frame. Can be used for metadata conversion, inference post-processing, and other tasks. |
  | [gvarealsense](./elements/gvarealsense.md) | Provides integration with Intel RealSense cameras, enabling video and depth stream capture for use in GStreamer pipelines. |
  | [gvastreammux](./elements/gvastreammux.md) | Multiplexes multiple input streams into a single pipeline with batch metadata support. |
  | [gvatrack](./elements/gvatrack.md) | Performs object tracking using zero-term, or imageless tracking algorithms. Assigns unique object IDs to the tracked objects. |
  | [gvawatermark](./elements/gvawatermark.md) | Overlays the metadata on the video frame to visualize the inference results. |
  | [g3dinference](./elements/g3dinference.md) | Performs deep learning inference on 3D LiDAR sensor data. |
  | [g3dlidarparse](./elements/g3dlidarparse.md) | Parses 3D LiDAR data for use in analytics pipelines. |
  | [g3dradarprocess](./elements/g3dradarprocess.md) | Processes 3D radar data for use in analytics pipelines. |

For the details on supported platforms, please refer to
[System Requirements](https://github.com/open-edge-platform/dlstreamer/blob/v2026.1.0/docs/user-guide/get_started/system_requirements.md).

For installing Pipeline Framework with the prebuilt binaries or Docker* or to build the binaries from the open source, refer to
[Intel® DL Streamer Pipeline Framework installation guide](https://github.com/open-edge-platform/dlstreamer/blob/v2026.1.0/docs/user-guide/get_started/install/install_guide_index.md).

**New**:

| Feature | Description |
| --------- | ------------- |
| DL Streamer Coding Agent | AI-powered coding assistant that translates natural-language pipeline descriptions into working DL Streamer applications (Python or GStreamer CLI). Ships with a structured skill comprising a requirements questionnaire, model preparation guide, pipeline construction rules, design patterns, sample index, and debugging hints. Seven example prompts included: pose estimation, people detection & tracking, VLM safety compliance checks, event-based smart NVR, license-plate recognition, multi-stream compose, and DeepStream-to-DLStreamer Python conversion. |
| Upstream GstAnalytics API migration | Removed custom gstanalyticsgroupmtd and gstanalyticskeypointmtd implementations, migrating to upstream GStreamer 1.28+ API. |
| Full-frame GstAnalytics support | Added full-frame GstAnalytics support so inference results without a parent object detection are properly stored in GstAnalyticsRelationMeta and serialized to JSON. Includes frame-level classification and keypoint conversion. |
| Keypoints GstAnalytics metadata | Switched keypoints to GstAnalytics metadata API for standardized metadata handling. |
| Analytics element (gvaanalytics) | New element providing a framework for custom analytics logic on detected objects and metadata. |
| Stream multiplexer element (gvastreammux) | New element for multiplexing multiple input streams into a single pipeline with GstAnalyticsBatchMeta support. |
| 3D inference element (g3dinference) | New element for performing deep learning inference on 3D LiDAR sensor data. |
| YOLO classification support | Added YOLO classification model support for image classification tasks. |
| PaddleOCRv5 model support | Added support for PaddleOCRv5 models for character recognition. |
| GETI semantic segmentation support | Added support for GETI semantic segmentation models. |
| CPU thread affinity parameter | Added affinity CPU thread affinity parameter to gvadetect/gvaclassify for performance tuning. |
| Skip raw tensors property | Added skip-raw-tensors property to gvaclassify and related components to reduce metadata overhead. |
| DepthConverter and pre-processing | Added DepthConverter and enhanced pre-processing capabilities for depth data. |
| New D3D11 backend for inference elements  | New D3D11 GPU processing backend for inference elements including model sharing support. |
| Windows installer | Added Windows installer for simplified DL Streamer deployment. |
| Windows samples expansion | Added new Windows samples including YOLO, metapublish Kafka/MQTT. |
| Dynamic ONVIF camera discovery sample | New sample for dynamic ONVIF camera discovery with DLS pipeline startup. |
| gvaanalytics sample | New sample demonstrating the gvaanalytics element usage. |
| Inference performance optimization sample | New sample app demonstrating inference performance optimizations DLS can provide. |
| Custom frame selection for VLM sample | New sample pipeline with custom frame selection logic for VLM queries. |
| DL Streamer ONVIF Python wheel | Introduced dlstreamer.onvif package as an installable Python wheel (whl file). |
| PyTorch Image Models (TIMM) import | Added PyTorch Image Models (TIMM) model import script for model onboarding. |
| Python environment setup script | Added Python environment setup script for streamlined development setup. |

**Improved**:

| Feature | Description |
| --------- | ------------- |
| gvawatermark enhancements | Custom full-frame text support, dynamic Gaussian blur kernel sized to ROI, metadata-controlled watermark drawing, explicit enable-blur flag documentation. |
| DLS Optimizer enhancements | Time reduction, ability to pause/resume progress, retrieve partial results, output results to file, cross-stream grouping for batches and nireqs, pipeline validation based on counted detections. |
| Deep SORT tracker refactoring | Refactored Deep SORT Tracker with enhanced detection and track structures for improved tracking accuracy. |
| metaaggregate improvements | Fixed id=0 hash lookup with g_hash_table_lookup_extended, fixed shared group member duplication, and added generic semantic_tag copy for all mtd types. |
| gvaattachroi GstAnalytics dual-write | Full-frame keypoints attached by gvaattachroi now also write GstAnalytics metadata for visibility to gvawatermark. |
| jsonconverter enhancements | Added frame-level keypoint group conversion, frame-level classification via semantic_tag keys, and per-keypoint semantic_tag to JSON output. |
| ROI label handling | Enhanced ROI label handling: append label regardless of confidence and show confidence only if valid. |
| YOLOv10/YOLOv26 converters | Updated YOLOv10 and YOLOv26 converters to enforce NMS for improved detection results. |
| OpenVINO update | Update to OpenVINO 2026.1. |
| GStreamer update | Update to GStreamer 1.28.2 with GStreamer 1.28 build support. Removed GStreamer 1.26 support. |
| OpenCV update | Update to OpenCV 4.13.0. |
| NPU driver update | Update to NPU driver version 1.32.1. |
| GPU driver update | Updated GPU drivers to latest version. |
| Ubuntu update | Bump Ubuntu to 24.04.4 and kernel 6.17. |
| LidarMeta refactoring | Refactored LidarMeta implementation from C++ to C for improved portability. |
| Model downloading scripts | Improved Ultralytics and HuggingFace model downloading scripts with better error handling. Windows YOLO script now supports YOLOv26. |
| gvapython samples conversion | Converted gvapython samples to standalone Python GStreamer elements. |
| gvametaconvert documentation | Added missing documentation about add-rtp-timestamp property. |
| gvametapublish Windows support | Enabled gvametapublishkafka and gvametapublishmqtt on Windows. |
| CI infrastructure | Switched to self-hosted Kubernetes infrastructure, enabled configurable tests in Makefile, added Fedora 41 image building, modified PR workflow to save CI resources. |
| Windows build improvements | Build gstanalytics library from source, PDB file generation, fixed environment variable paths. |
| Python samples overview | Updated Python samples overview and READMEs. |
| Tests | Expanded optimizer unit tests, added GStreamer check framework unit tests for gvawatermark, run optimizer tests always. Added 41 unit tests for metaaggregate copy functions, extended tensor_convert_test with full-frame classification and keypoint round-trip tests. |

**Fixed**:

| # | Description |
| --- | ------------- |
| 1 | Fixed downloading of pallet defect detection model. |
| 2 | Fixed codeQL scan issues. |
| 3 | Fixed Coverity data race warnings in gvastreammux/demux. |
| 4 | Fixed GStreamer cache cleaning by prepending directories to PATH. |
| 5 | Fixed draw_face_attributes sample failing to build on Ubuntu 24 deb install due to missing OpenCV paths. |
| 6 | Fixed instant EOS and tracking metadata copy failure in multi-branch pipelines. |
| 7 | Fixed detections with 0 height/width causing issues. |
| 8 | Fixed DLSPS building errors. |
| 9 | Fixed path for downloading Windows assets. |
| 10 | Fixed Windows set environment path issue. |
| 11 | Fixed GIR to typelib compilation for Windows. |
| 12 | Fixed WinGet PowerShell module crash. |
| 13 | Fixed mapping of VA memory in gvawatermark. |
| 14 | Fixed incorrect default devices being used in optimizer. |
| 15 | Fixed images not showing in docs website. |
| 16 | Fixed analytics metadata documentation. |
| 17 | Fixed linking dlstreamer to home directory. |
| 18 | Fixed triggering CI when pushed to main. |
| 19 | Fixed documentation for DL Streamer and DeepStream coexistence. |
| 20 | Fixed queue name check and removed mutex unlock (reverted). |
| 21 | Fixed yamllint issues. |
| 22 | Removed deprecated ffmpeg_openvino and decode_resize_inference samples. |

**Known Issues**:

| Issue | Description |
| ------- | ------------- |
| Preview Architecture 2.0 Samples | Preview Arch 2.0 samples have known issues with inference results. |

**Legacy Features and Deprecation Timeline**:

List of the features and components to be deprecated in the future.

| Feature | Target | Replacement |
| --------- | -------- | ------------- |
| OMZ models | end of Q3'2026 | Use HuggingFace, Ultralytics, TIMM |
| Model-proc-file | end of Q3'2026 | ModelAPI (common with Geti) |
| gvapython element | end of Q4'2026 | usage of regular GStreamer Python bindings |
| GstVideoRegionOfInterest meta + Intel extensions | end of Q4'2026 | usage of GstAnalyticsMtd |
| Architecture 2.0 elements | end of Q4'2026 | Preserve essential components and ideas |
| Alchemist GPU support | end of Q3'2026 | Battlemage discrete GPUs |
| Tiger Lake support | end of Q4'2026 | new supported units: Arrow Lake and Panther Lake |
| Ubuntu 22.04 | end of Q4'2026 | newer Ubuntu versions, including Ubuntu 26 |

**Legal Information**:

GStreamer is an open source framework licensed under LGPL. See
<https://gstreamer.freedesktop.org/documentation/frequently-asked-questions/licensing.html>.
You are solely responsible for determining if your use of GStreamer requires any additional
licenses. Intel is not responsible for obtaining any such licenses, nor liable for any
licensing fees due, in connection with your use of GStreamer.

## Version 2026.0

**Key highlights**:

- New elements: gvafpsthrottle, g3dradarprocess, g3dlidarparse
- New model support: YOLOv26, YOLO-E, RT-DETR, HuggingFace ViT
- Streamlined integration with Ultralytics and HuggingFace model hubs
- GstAnalytics metadata support: DL Streamer supports GstAnalytics metadata for object detection, classification, tracking and adds custom GstAnalytics extension for keypoints
- gvawatermark overhaul: object bluring, text backgrounds, label filtering, extra fonts, thickness/color options, FPS overlay
- Inference enhancements: batch timeout, OpenCV tensor compression for all devices
- Windows platform: GPU inference via D3D11, gvapython support, CI integration, build/setup improvements
- New Python samples: VLM Alerts, Smart NVR, ONVIF Discovery, face detection/age classification, open-vocabulary detection, RealSense, DL Streamer + DeepStream
- Optimizer: multi-stream optimization, cross-stream batching, device selection, refactored with tests
- Component updates: OpenVINO 2026.0.0, NPU driver 1.30, RealSense SDK 2.57.5
- Library consolidation: merged gvawatermark3d, gvadeskew, gvamotiondetect, gvagenai into gstvideoanalytics
- CI: Zizmor security scanning, Windows CI, Docker image size checks

Deep Learning Streamer (DL Streamer) Pipeline Framework is a streaming media analytics framework, based on GStreamer* multimedia framework, for creating complex media analytics pipelines. It ensures pipeline interoperability and provides optimized media, and inference operations using Intel® Distribution of OpenVINO™ Toolkit Inference Engine backend, across Intel® architecture, CPU, discrete GPU, integrated GPU and NPU.
The complete solution leverages:

- Open source GStreamer\* framework for pipeline management
- GStreamer* plugins for input and output such as media files and real-time streaming from camera or network
- Video decode and encode plugins, either CPU optimized plugins or GPU-accelerated plugins based on VAAPI
- Deep Learning models converted from training frameworks TensorFlow\*, Caffe\* etc.
- The following elements in the Pipeline Framework repository:

  | Element | Description |
  | --- | --- |
  | [gvaattachroi](./elements/gvaattachroi.md) | Adds user-defined regions of interest to perform inference on, instead of full frame. |
  | [gvaaudiodetect](./elements/gvaaudiodetect.md) | Performs audio event detection using AclNet model. |
  | [gvaaudiotranscribe](./elements/gvaaudiotranscribe.md) | Performs audio transcription using OpenVino GenAI Whisper model. |
  | [gvaclassify](./elements/gvaclassify.md) | Performs object classification. Accepts the ROI as an input and outputs classification results with the ROI metadata. |
  | [gvadetect](./elements/gvadetect.md) | Performs object detection on a full-frame or region of interest (ROI) using object detection models such as YOLOv4-v11, MobileNet SSD, Faster-RCNN etc. Outputs the ROI for detected objects. |
  | [gvafpscounter](./elements/gvafpscounter.md) | Measures frames per second across multiple streams in a single process. |
  | [gvafpsthrottle](./elements/gvafpsthrottle.md) | Throttles the frame rate of a pipeline to a specified FPS value. |
  | [gvagenai](./elements/gvagenai.md) | Performs inference with Vision Language Models using OpenVINO™ GenAI, accepts video and text prompt as an input, and outputs text description. It can be used to generate text summarization from video. |
  | [gvainference](./elements/gvainference.md) | Runs deep learning inference on a full-frame or ROI using any model with an RGB or BGR input. |
  | [gvametaaggregate](./elements/gvametaaggregate.md) | Aggregates inference results from multiple pipeline branches |
  | [gvametaconvert](./elements/gvametaconvert.md) | Converts the metadata structure to the JSON format. |
  | [gvametapublish](./elements/gvametapublish.md) | Publishes the JSON metadata to MQTT or Kafka message brokers or files. |
  | [gvamotiondetect](./elements/gvamotiondetect.md) | Performs lightweight motion detection on NV12 video frames and emits motion regions of interest (ROIs) as analytics metadata. |
  | [gvapython](./elements/gvapython.md) | Provides a callback to execute user-defined Python functions on every frame. Can be used for metadata conversion, inference post-processing, and other tasks. |
  | [gvarealsense](./elements/gvarealsense.md) | Provides integration with Intel RealSense cameras, enabling video and depth stream capture for use in GStreamer pipelines. |
  | [gvatrack](./elements/gvatrack.md) | Performs object tracking using zero-term, or imageless tracking algorithms. Assigns unique object IDs to the tracked objects. |
  | [gvawatermark](./elements/gvawatermark.md) | Overlays the metadata on the video frame to visualize the inference results. |
  | [g3dradarprocess](./elements/g3dradarprocess.md) | Processes 3D radar data for use in analytics pipelines. |
  | [g3dlidarparse](./elements/g3dlidarparse.md) | Parses 3D lidar data for use in analytics pipelines. |

For the details on supported platforms, please refer to [System Requirements](./system_requirements.md).
For installing Pipeline Framework with the prebuilt binaries or Docker\* or to build the binaries from the open source, refer to [DL Streamer Pipeline Framework installation guide](./install/install_guide_index.md).

**New**:

| Title | High-level description |
| --- | --- |
| 3D elements (g3dradarprocess, g3dlidarparse) | New 3D plugin support with g3dradarprocess element for radar data processing and g3dlidarparse element for lidar data parsing, enabling 3D analytics pipelines. |
| FPS throttle element (gvafpsthrottle) | New element to throttle the frame rate of a pipeline to a specified FPS value. |
| YOLOv26 model support | Added converters and post-processing for YOLOv26 models, including oriented bounding box (OBB) support and INT8 GPU inference. Added YOLOv26 to supported models in samples. |
| RT-DETR model support | Added RT-DETR support implementation for real-time detection transformer models. |
| HuggingFace ViT classifier support | Added HuggingFace Vision Transformer (ViT) classifier config parser for inference. |
| Batch timeout for inference elements | Added batch-timeout parameter to inference elements, allowing control over batching wait time. |
| VLM Alerts sample | New Python sample for VLM-based alerts with displaying results on produced video. |
| Smart NVR sample | New Python sample for Smart NVR with added custom analytics logic (gvaAnalytics) and custom storage (gvaRecorder) elements. |
| ONVIF Camera Discovery sample | New Python sample demonstrating ONVIF camera discovery and DL Streamer pipeline launcher. |
| Face detection & age classification sample | New Python sample for face detection and age classification using HuggingFace models. |
| Open-vocabulary object detection sample | New Python sample with open-vocabulary prompt for object detection. |
| DL Streamer + DeepStream coexistence sample | New sample demonstrating DL Streamer and DeepStream working in one system. |
| Motion detect sample (Windows) | New sample demonstrating DL Streamer gvamotiondetect functionality |
| RealSense element usage sample | New sample demonstrating gvarealsense element usage. |

**Improved**:

| Title | High-level description |
| --- | --- |
| gvawatermark enhancements | Major enhancements to the gvawatermark element: display configuration options (thickness, color index), text background support, inclusive/exclusive label filtering, additional font support, average FPS info overlay, and visual documentation. |
| DLS Optimizer enhancements | Optimizer refactored with multi-stream optimization, cross-stream batching, improved FPS reporting, and device selection improvements. |
| gvametaconvert enhancements | Added reference NTP timestamp from RTCP sender meta extraction to gvametaconvert element. |
| ROI object construction enhacement | For existing GstAnalyticsODMtd only, creates GstVideoRegionOfInterestMeta until full GstAnalytics migration. |
| Latency tracer multi-source/sink support | Extended latency_tracer to support multiple sources and multiple sinks. |
| Detection anomaly converter | Refactored and enhanced anomaly logic in DetectionAnomalyConverter. |
| FP32 precision in BoxesLabelsConverter | Added FP32 precision support in BoxesLabelsConverter label parsing. |
| Bounding box validation | Added extra validation of bounding boxes to improve robustness. |
| OpenCV tensor compression for all devices | Use OpenCV tensor compression for all inference devices, yielding best performance across CPU/GPU/NPU. |
| Model API refactoring | Moved Model API parser to separate files; added conversion from Ultralytics and HuggingFace metadata to Model API. |
| Python samples overview | Added overview section for Python samples; updated READMEs. |
| Tests | Expanded coverage of functional and unit tests. |
| Windows: GPU inference with D3D11 | Added support for GPU inference on Windows using D3D11. |
| Windows: gvapython support | Added Windows support for gvapython element and gstgva Python bindings. |
| Windows: enhanced build & setup | Enhanced Windows build/setup scripts, added remove script, Visual C++ runtime handling, and JSON output for Windows samples. |
| Windows: CI integration | Enabled Windows tests in GitHub Actions workflow, model downloads on Windows. |
| Library consolidation | Merged gvawatermark3d, gvadeskew, gvamotiondetect, and gvagenai into the gstvideoanalytics library. |
| OpenVINO update | Update to OpenVINO 2026.0.0. |
| NPU driver update | Update to NPU driver version 1.30. |
| RealSense update | Update to Intel RealSense SDK 2.57.5. |
| Model download script improvements | Simplified YOLO model download script, enhanced INT8 quantization, refactored YOLOv8+ export/quantize, added model validation. |
| CI: Zizmor security scanning | Added Zizmor GitHub Actions security scanner. |

**Fixed**:

| **#** | **Issue Description** |
| ---------------- | ------------------------ |
| 1 | Fixed YOLO26 model inference on GPU FP16/FP32. |
| 2 | Fixed threshold parameter in gvadetect not working with PDD model. |
| 3 | Fixed yolov8-seg inference result different from OpenVINO. |
| 4 | Fixed gvapython failing to read yolo-pose keypoint metadata. |
| 5 | Fixed NV12 frame data in Python by removing padding correctly. |
| 6 | Fixed watermark default text background behaviour. |
| 7 | Fixed check for pad_value in model XML file. |
| 8 | Fixed yolo_v10.cpp compile error on Windows. |
| 9 | Fixed DLL output paths on Windows. |
| 10 | Fixed compilation warnings on Windows. |
| 11 | Fixed timestamp on VS 2026. |
| 12 | Fixed GStreamer downloader by adding UserAgent. |
| 13 | Fixed libva path setup in setup_dls_env.ps1 |
| 14 | Removed libva dependency for monolithic elements on Windows. |
| 15 | Fixed latency tracker for smart intersection pipelines. |
| 16 | Fixed environment variable paths in Ubuntu install guide. |
| 17 | Fixed directory already exists error during build. |
| 18 | Removed duplicate gvametapublish element register. |
| 19 | Reverted RTP timestamp feature due to issues. |
| 20 | Fixed download public models script - versions of NumPy, Onnx, and Seaborn. |
| 21 | Fixed missing context in Build Docker instruction. |
| 22 | Fixed formatting in installation guide and developer guide documentation. |

**Known Issues**:

| Issue | Issue Description |
| --- | --- |
| Preview Architecture 2.0 Samples | Preview Arch 2.0 samples have known issues with inference results. |

**Legacy Features and Deprecation Timeline**:

List of the features and components to be deprecated in the future.

| Feature | End of Support Date | Replacement strategy |
| --- | --- | --- |
| OMZ models | end of Q3'2026 | Use HuggingFace, Ultralytics, TIMM |
| Model-proc-file | end of Q3'2026 | ModelAPI (common with Geti) |
| WSL support | end of Q3'2026 | native Windows support |
| GstVideoRegionOfInterest meta + Intel extensions | end of Q4'2026 | usage of GstAnalyticsMtd |
| Architecture 2.0 elements | end of Q4'2026 | Preserve essential components and ideas |
| FFMpeg integration samples | end of Q2'2026 | no replacement |

**Legal Information**:

- GStreamer is an open source framework licensed under LGPL.
See [GStreamer Licensing FAQ](https://gstreamer.freedesktop.org/documentation/frequently-asked-questions/licensing.html).
You are solely responsible for determining if your use of GStreamer requires any additional licenses.
Intel is not responsible for obtaining any such licenses, nor liable for any licensing fees due, in connection with your use of GStreamer.

- FFmpeg is an open source project licensed under LGPL and GPL.
See [FFmpeg Legal Information](https://www.ffmpeg.org/legal.html).
You are solely responsible for determining if your use of FFmpeg requires any additional licenses.
Intel is not responsible for obtaining any such licenses, nor liable for any licensing fees due, in connection with your use of FFmpeg.

<!--hide_directive
:::{toctree}
:hidden:

release-notes/release-notes-2025
release-notes/release-notes-2024

:::
hide_directive-->