# Release Notes: Deep Learning Streamer (DL Streamer) Pipeline Framework Release 2026.0

## [Preview] Version 2026.0

## Key highlights:
* New elements: gvafpsthrottle, g3dradarprocess, g3dlidarparse
* New model support: YOLOv26 (including OBB), RT-DETR, HuggingFace ViT
* gvawatermark overhaul: object bluring, text backgrounds, label filtering, extra fonts, thickness/color options, FPS overlay
* Inference enhancements: batch timeout, OpenCV tensor compression for all devices, FP32 precision, custom GstAnalytics data API
* Windows platform: GPU inference via D3D11, gvapython support, CI integration, build/setup improvements
* New Python samples: VLM Alerts, Smart NVR, ONVIF Discovery, face detection/age classification, open-vocabulary detection, RealSense, DL Streamer + DeepStream
* Optimizer: multi-stream optimization, cross-stream batching, device selection, refactored with tests
* Component updates: OpenVINO 2026.0.0, NPU driver 1.30, RealSense SDK 2.57.5
* Library consolidation: merged gvawatermark3d, gvadeskew, gvamotiondetect, gvagenai into gstvideoanalytics
* CI: Zizmor security scanning, Windows CI, Docker image size checks

Deep Learning Streamer (DL Streamer) Pipeline Framework is a streaming media analytics framework, based on GStreamer* multimedia framework, for creating complex media analytics pipelines. It ensures pipeline interoperability and provides optimized media, and inference operations using Intel® Distribution of OpenVINO™ Toolkit Inference Engine backend, across Intel® architecture, CPU, discrete GPU, integrated GPU and NPU.
The complete solution leverages:

- Open source GStreamer\* framework for pipeline management
- GStreamer* plugins for input and output such as media files and real-time streaming from camera or network
- Video decode and encode plugins, either CPU optimized plugins or GPU-accelerated plugins based on VAAPI
- Deep Learning models converted from training frameworks TensorFlow\*, Caffe\* etc.
- The following elements in the Pipeline Framework repository:

  | Element | Description |
  |---|---|
  | [gvaattachroi](../elements/gvaattachroi.md) | Adds user-defined regions of interest to perform inference on,   instead of full frame. |
  | [gvaaudiodetect](../elements/gvaaudiodetect.md) | Performs audio event detection using AclNet model. |
  | [gvaaudiotranscribe](../elements/gvaaudiotranscribe.md) | Performs audio transcription using OpenVino GenAI Whisper model. |
  | [gvaclassify](../elements/gvaclassify.md) | Performs object classification. Accepts the ROI as an input and   outputs classification results with the ROI metadata. |
  | [gvadetect](../elements/gvadetect.md) | Performs object detection on a full-frame or region of interest (ROI)   using object detection models such as YOLOv4-v11, MobileNet SSD, Faster-RCNN etc. Outputs the ROI for detected   objects. |
  | [gvafpscounter](../elements/gvafpscounter.md) | Measures frames per second across multiple streams in a single   process. |
  | [gvafpsthrottle](../elements/gvafpsthrottle.md) | Throttles the frame rate of a pipeline to a specified FPS value. |
  | [gvagenai](../elements/gvagenai.md) | Performs inference with Vision Language Models using OpenVINO™ GenAI, accepts video and text prompt as an input, and outputs text description. It can be used to generate text summarization from video. |
  | [gvainference](../elements/gvainference.md) | Runs deep learning inference on a full-frame or ROI using any model with an RGB or BGR input. |
  | [gvametaaggregate](../elements/gvametaaggregate.md) | Aggregates inference results from multiple pipeline branches |
  | [gvametaconvert](../elements/gvametaconvert.md) | Converts the metadata structure to the JSON format. |
  | [gvametapublish](../elements/gvametapublish.md) | Publishes the JSON metadata to MQTT or Kafka message brokers or   files. |
  | [gvamotiondetect](../elements/gvamotiondetect.md) | Performs lightweight motion detection on NV12 video frames and emits motion regions of interest (ROIs) as analytics metadata. |
  | [gvapython](../elements/gvapython.md) | Provides a callback to execute user-defined Python functions on every   frame. Can be used for metadata conversion, inference post-processing, and other tasks. |
  | [gvarealsense](../elements/gvarealsense.md) | Provides integration with Intel RealSense cameras, enabling video and depth stream capture for use in GStreamer pipelines. |
  | [gvatrack](../elements//gvatrack.md) | Performs object tracking using zero-term, or imageless tracking algorithms.   Assigns unique object IDs to the tracked objects. |
  | [gvawatermark](../elements//gvawatermark.md) | Overlays the metadata on the video frame to visualize the inference   results. |
  | [g3dradarprocess](../elements/g3dradarprocess.md) | Processes 3D radar data for use in analytics pipelines. |
  | [g3dlidarparse](../elements/g3dlidarparse.md) | Parses 3D lidar data for use in analytics pipelines. |

For the details on supported platforms, please refer to [System Requirements](../get_started/system_requirements.md).
For installing Pipeline Framework with the prebuilt binaries or Docker\* or to build the binaries from the open source, refer to [Intel® DL Streamer Pipeline Framework installation guide](../get_started/install/install_guide_index.md).

**New in this Release**

| Title | High-level description |
|---|---|
| 3D elements (g3dradarprocess, g3dlidarparse) |	New 3D plugin support with g3dradarprocess element for radar data processing and g3dlidarparse element for lidar data parsing, enabling 3D analytics pipelines.|
| FPS throttle element (gvafpsthrottle) |	New element to throttle the frame rate of a pipeline to a specified FPS value. |
| YOLOv26 model support|	Added converters and post-processing for YOLOv26 models, including oriented bounding box (OBB) support and FP16/FP32 GPU inference. Added YOLOv26 to supported models in samples.|
| RT-DETR model support |	Added RT-DETR support with dedicated converter implementation for real-time detection transformer models. |
| HuggingFace ViT classifier support |	Added HuggingFace Vision Transformer (ViT) classifier config parser for inference.|
| gvawatermark enhancements|	Major enhancements to the gvawatermark element: display configuration options (thickness, color index), text background support, inclusive/exclusive label filtering, additional font support, average FPS info overlay, and visual documentation.|
| Batch timeout for inference elements |	Added batch-timeout parameter to inference elements, allowing control over batching wait time.|
| Reference timestamp metadata|Added reference timestamp meta extraction to gvametaconvert element.|
| Custom GstAnalytics data	| Enabled custom code to add GstAnalytics data outside of DL Streamer components.|
| Latency tracer multi-source/sink support|	Extended latency_tracer to support multiple sources and multiple sinks.|
| Detection anomaly converter |	Refactored and enhanced anomaly logic in DetectionAnomalyConverter.|
| FP32 precision in BoxesLabelsConverter	| Added FP32 precision support in BoxesLabelsConverter label parsing.|
| Bounding box validation |	Added extra validation of bounding boxes to improve robustness.|
| OpenCV tensor compression for all devices	| Use OpenCV tensor compression for all inference devices, yielding best performance across CPU/GPU/NPU.|
| Model API refactoring	| Moved Model API parser to separate files; added conversion from third-party metadata to Model API.|
| VLM Alerts sample (Python) | New Python sample for VLM-based alerts with displaying results on produced video. |
| Smart NVR sample (Python)	 | New Python sample for Smart NVR with prototype elements. |
| ONVIF Camera Discovery sample | New sample demonstrating ONVIF camera discovery and DL Streamer pipeline launcher. |
| Face detection & age classification sample  | New Python sample for face detection and age classification using HuggingFace models. |
| Open-vocabulary object detection sample	 | New Python sample with open-vocabulary prompt for object detection. |
| RealSense element usage sample	| New sample demonstrating gvarealsense element usage. |
| DL Streamer + DeepStream concurrent sample | New sample demonstrating concurrent DL Streamer and DeepStream usage. |
| Python samples overview	 | Added overview section for Python samples; updated READMEs. |
| Windows: GPU inference with D3D11	 | Added support for GPU inference on Windows using D3D11. |
| Windows: gvapython support  | Added Windows support for gvapython element and gstgva Python bindings. |
| Windows: enhanced build & setup	 | Enhanced Windows build/setup scripts, added remove script, Visual C++ runtime handling, and JSON output for Windows samples. |
| Windows: CI integration	 | Enabled Windows tests in GitHub Actions workflow, model downloads on Windows. |
| DL Optimizer enhancements	 | Optimizer refactored with multi-stream optimization, cross-stream batching, improved FPS reporting, and device selection improvements. Added functional tests and unit tests. |
| CI: Zizmor security scanning |	Added Zizmor GitHub Actions security scanner. |
| Library consolidation	| Merged gvawatermark3d, gvadeskew, gvamotiondetect, and gvagenai into the gstvideoanalytics library. |
| OpenVINO update	| Update to OpenVINO 2026.0.0. |
| NPU driver update	 | Update to NPU driver version 1.30. |
| RealSense update	 | Update to Intel RealSense SDK 2.57.5. |
| Model download script improvements | Simplified YOLO model download script, enhanced INT8 quantization, refactored YOLOv8+ export/quantize, added model validation. |


**Fixed**

| **#**   | **Issue Description**  |
|----------------|------------------------|
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

**Known Issues**

| Issue | Issue Description |
|---|---|
| Preview Architecture 2.0 Samples | Preview Arch 2.0 samples have known issues with inference results. |

## Legal Information ##

* GStreamer is an open source framework licensed under LGPL.
See https://gstreamer.freedesktop.org/documentation/frequently-asked-questions/licensing.html.
You are solely responsible for determining if your use of GStreamer requires any additional licenses.
Intel is not responsible for obtaining any such licenses, nor liable for any licensing fees due, in connection with your use of GStreamer.

* FFmpeg is an open source project licensed under LGPL and GPL.
See https://www.ffmpeg.org/legal.html.
You are solely responsible for determining if your use of FFmpeg requires any additional licenses.
Intel is not responsible for obtaining any such licenses, nor liable for any licensing fees due, in connection with your use of FFmpeg.

<!--hide_directive
```{toctree}
:hidden:

release-notes/release-notes-2025.md
release-notes/release-notes-2024.md
```
hide_directive-->
