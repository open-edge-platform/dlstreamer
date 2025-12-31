# Deep Learning Streamer (DL Streamer) Pipeline Framework Release Notes

## Deep Learning Streamer (DL Streamer) Pipeline Framework Release 2025.2.0

Intel® Deep Learning Streamer (Intel® DL Streamer) Pipeline Framework is a streaming media analytics framework, based on GStreamer* multimedia framework, for creating complex media analytics pipelines. It ensures pipeline interoperability and provides optimized media, and inference operations using Intel® Distribution of OpenVINO™ Toolkit Inference Engine backend, across Intel® architecture, CPU, discrete GPU, integrated GPU and NPU.
The complete solution leverages:

- Open source GStreamer\* framework for pipeline management
- GStreamer* plugins for input and output such as media files and real-time streaming from camera or network
- Video decode and encode plugins, either CPU optimized plugins or GPU-accelerated plugins based on VAAPI
- Deep Learning models converted from training frameworks TensorFlow\*, Caffe\* etc.
- The following elements in the Pipeline Framework repository:

  | Element | Description |
  |---|---|
  | [gvaattachroi](./docs/source/elements/gvaattachroi.md) | Adds user-defined regions of interest to perform inference on,   instead of full frame. |
  | [gvaaudiodetect](./docs/source/elements/gvaaudiodetect.md) | Performs audio event detection using AclNet model. |
  | [gvaaudiotranscribe](./docs/source/elements/gvaaudiotranscribe.md) | Performs audio transcription using OpenVino GenAI Whisper model. |
  | [gvaclassify](./docs/source/elements/gvaclassify.md) | Performs object classification. Accepts the ROI as an input and   outputs classification results with the ROI metadata. |
  | [gvadetect](./docs/source/elements/gvadetect.md) | Performs object detection on a full-frame or region of interest (ROI)   using object detection models such as YOLOv4-v11, MobileNet SSD, Faster-RCNN etc. Outputs the ROI for detected   objects. |
  | [gvafpscounter](./docs/source/elements/gvafpscounter.md) | Measures frames per second across multiple streams in a single   process. |
  | [gvagenai](./docs/source/elements/gvagenai.md) | Performs inference with Vision Language Models using OpenVINO™ GenAI, accepts video and text prompt as an input, and outputs text description. It can be used to generate text summarization from video. |
  | [gvainference](./docs/source/elements/gvainference.md) | Runs deep learning inference on a full-frame or ROI using any model with an RGB or BGR input. |
  | [gvametaaggregate](./docs/source/elements/gvametaaggregate.md) | Aggregates inference results from multiple pipeline branches |
  | [gvametaconvert](./docs/source/elements/gvametaconvert.md) | Converts the metadata structure to the JSON format. |
  | [gvametapublish](./docs/source/elements/gvametapublish.md) | Publishes the JSON metadata to MQTT or Kafka message brokers or   files. |
  | [gvamotiondetect](./docs/source/elements/gvamotiondetect.md) | Performs lightweight motion detection on NV12 video frames and emits motion regions of interest (ROIs) as analytics metadata. |
  | [gvapython](./docs/source/elements/gvapython.md) | Provides a callback to execute user-defined Python functions on every   frame. Can be used for metadata conversion, inference post-processing, and other tasks. |
  | [gvarealsense](./docs/source/elements/gvarealsense.md) | Provides integration with Intel RealSense cameras, enabling video and depth stream capture for use in GStreamer pipelines. |
  | [gvatrack](./docs/source/elements/gvatrack.md) | Performs object tracking using zero-term, or imageless tracking algorithms.   Assigns unique object IDs to the tracked objects. |
  | [gvawatermark](./docs/source/elements/gvawatermark.md) | Overlays the metadata on the video frame to visualize the inference   results. |

For the details on supported platforms, please refer to [System Requirements](./docs/source/get_started/system_requirements.md).
For installing Pipeline Framework with the prebuilt binaries or Docker\* or to build the binaries from the open source, refer to [Intel® DL Streamer Pipeline Framework installation guide](./docs/source/get_started/install/install_guide_index.md).

### New in this Release
  | Title | High-level description |
  |---|---|
  |  Motion detection (gvamotiondetect) | Performs lightweight motion detection on NV12 video frames and emits motion regions of interest (ROIs) as analytics metadata. |
  | Audio transcription (gvaaudiotranscribe)  |  Transcribes audio content with OpenVino GenAI Whisper model. |
  | Gvagenai element added | Performs inference with Vision Language Models using OpenVINO™ GenAI, accepts video and text prompt as an input, and outputs text description. <br>Models supported: MiniCPM-V, Gemma3, Phi-4-multimodal-instruct. |
  | Deep SORT | Preview version of Deep SORT tracking algorithm in gvatrack element. |
  | gvawatermark element support on GPU | Gvawatermark implementation extended about GPU support (CPU default). |
  | Pipeline optimizer support | 1st version of DL Streamer optimizer implementation added allowing end user finding the most FPS optimized pipeline. |
  | GstAnalytics metadata support | Enabled GstAnalytics metadata support. |
  | OpenVINO custom operations | Add support for OpenVINO custom operations. |
  | D3D11 preprocessing enabled | Windows support extended about D3D11 preprocessing implementation. |
  | UX, Stability && Performance fixes | • memory management fixes <br> • automatically select pre-process-backend=va-surface-sharing for GPU <br>• adjusting caps negotiations and preproc backend selection <br>• removing deleted element from all shared reference lists. <br> • using OpenCV preproc to convert sparse tensors to contiguous tensors <br>• creation of new VADisplay ctx per each inference instance <br>• remove need for dual va+opencv image pre-processing |
  | Intel Core Ultra Panther Lake CPU/GPU support | Readiness for supporting Intel Core Ultra Panther Lake CPU/GPU. |
  | OpenVINO update | Update to 2025.3 version. |
  | GStreamer update | Update to 1.26.6 version. |
  | GPU drivers update | Update to 25.40 version (for Ubuntu24) |
  | NPU drivers update | Update to 1.23 version. |



### Fixed Issues

| **#**   | **Issue Description**  |
|----------------|------------------------|
  | <center>1</center> |Fixed issue with segmentation fault and early exit for testing scenarios with mixed GPU/CPU device combinations. |
  | <center>2</center> | Updated documentation for latency tracer. |
  | <center>3</center> | Fixed issue where NPU inference required inefficient CPU color processing. |
  | <center>4</center> | Fixed memory management for elements: gvawatermark, gvametaconvert, gvaclassify. |
  | <center>5</center> | Improved model-proc check logic for va backend. |
  | <center>6</center> | Fixed keypoints metadata processing issue for gvawatermark. |
  | <center>7</center> | Fixed issue with missed gvarealsense element in dlstreamer image. |
  | <center>8</center> | Fixed issue for scenario when vacompositor scale-method option didn't take affect. |
  | <center>9</center> | Fixed documentation bug in the installation guide. |
  | <center>10</center> | Fixed issue with same name for many python modules used by gvapython. |
  | <center>11</center> | Fixed issue with draw_face_attributes sample (cpp) on TGL Ubuntu 24. |
  | <center>12</center> | Fixed wrong pose estimation on ARL GPU with yolo11s-pose. |
  | <center>13</center> | Fixed inconsistent timestamp for vehicle_pedestrian_tracking sample on ARL. |
  | <center>14</center> | Fixed missing element 'qsvh264dec' in Ubuntu24 docker images. |



### Known Issues

| Issue | Issue Description |
|---|---|
| Preview Architecture 2.0 Samples | Preview Arch 2.0 samples have known issues with inference results. |
| Sporadic hang on vehicle_pedestrian_tracking_20_cpu sample | Using Tiger Lake CPU to run this sample may lead to sporadic hang at 99.9% of video processing. Rerun the sample as W/A or use GPU instead. |



## Deep Learning Streamer (DL Streamer) Pipeline Framework Release 2025.1.2

Intel® Deep Learning Streamer (Intel® DL Streamer) Pipeline Framework is a streaming media analytics framework, based on GStreamer* multimedia framework, for creating complex media analytics pipelines. It ensures pipeline interoperability and provides optimized media, and inference operations using Intel® Distribution of OpenVINO™ Toolkit Inference Engine backend, across Intel® architecture, CPU, discrete GPU, integrated GPU and NPU.
The complete solution leverages:

- Open source GStreamer\* framework for pipeline management
- GStreamer* plugins for input and output such as media files and real-time streaming from camera or network
- Video decode and encode plugins, either CPU optimized plugins or GPU-accelerated plugins based on VAAPI
- Deep Learning models converted from training frameworks TensorFlow\*, Caffe\* etc.
- The following elements in the Pipeline Framework repository:

  | Element | Description |
  |---|---|
  | [gvadetect](./docs/source/elements/gvadetect.md) | Performs object detection on a full-frame or region of interest (ROI)   using object detection models such as YOLOv4-v11, MobileNet SSD, Faster-RCNN etc. Outputs the ROI for detected   objects. |
  | [gvaclassify](./docs/source/elements/gvaclassify.md) | Performs object classification. Accepts the ROI as an input and   outputs classification results with the ROI metadata. |
  | [gvainference](./docs/source/elements/gvainference.md) | Runs deep learning inference on a full-frame or ROI using any model   with an RGB or BGR input. |
  | [gvatrack](./docs/source/elements/gvatrack.md) | Performs object tracking using zero-term, or imageless tracking algorithms.   Assigns unique object IDs to the tracked objects. |
  | [gvaaudiodetect](./docs/source/elements/gvaaudiodetect.md) | Performs audio event detection using AclNet model. |
  | [gvagenai](./docs/source/elements/gvagenai.md) | Performs inference with Vision Language Models using OpenVINO™ GenAI, accepts video and text prompt as an input, and outputs text description. It can be used to generate text summarization from video. |
  | [gvaattachroi](./docs/source/elements/gvaattachroi.md) | Adds user-defined regions of interest to perform inference on,   instead of full frame. |
  | [gvafpscounter](./docs/source/elements/gvafpscounter.md) | Measures frames per second across multiple streams in a single   process. |
  | [gvametaaggregate](./docs/source/elements/gvametaaggregate.md) | Aggregates inference results from multiple pipeline   branches |
  | [gvametaconvert](./docs/source/elements/gvametaconvert.md) | Converts the metadata structure to the JSON format. |
  | [gvametapublish](./docs/source/elements/gvametapublish.md) | Publishes the JSON metadata to MQTT or Kafka message brokers or   files. |
  | [gvapython](./docs/source/elements/gvapython.md) | Provides a callback to execute user-defined Python functions on every   frame. Can be used for metadata conversion, inference post-processing, and other tasks. |
  | [gvarealsense](./docs/source/elements/gvarealsense.md) | Provides integration with Intel RealSense cameras, enabling video and depth stream capture for use in GStreamer pipelines. |
  | [gvawatermark](./docs/source/elements/gvawatermark.md) | Overlays the metadata on the video frame to visualize the inference   results. |

For the details on supported platforms, please refer to [System Requirements](./get_started/system_requirements.md).
For installing Pipeline Framework with the prebuilt binaries or Docker\* or to build the binaries from the open source, refer to [Intel® DL Streamer Pipeline Framework installation guide](./get_started/install/install_guide_index.md).

### New in this Release

| Title | High-level description |
|---|---|
| Custom model post-processing | End user can now create a custom post-processing library (.so); [sample](./samples/gstreamer/gst_launch/custom_postproc) added as reference.  |
| Latency mode support | Default scheduling policy for DL Streamer is throughput. With this change user can add scheduling-policy=latency for scenarios that prioritize latency requirements over throughput. |
|  |  |
| Visual Embeddings enabled | New models enabled to convert input video into feature embeddings, validated with Clip-ViT-Base-B16/Clip-ViT-Base-B32 models; [sample](./samples/gstreamer/gst_launch/lvm) added as reference. |
| VLM models support | new gstgenai element added to convert video into text (with VLM models), validated with miniCPM2.6, available in advanced installation option when building from sources; [sample](./samples/gstreamer/gst_launch/gvagenai) added as reference. |
| INT8 automatic quantization support for Yolo models | Performance improvement, automatic INT8 quantization for Yolo models |
| MS Windows 11 support  | Native support for Windows 11 |
| New Linux distribution (Azure Linux derivative) | New distribution added, DL Streamer can be now installed on Edge Microvisor Toolkit. |
| License plate recognition use case support | Added support for models that allow to recognize license plates; [sample](./samples/gstreamer/gst_launch/license_plate_recognition) added as reference.  |
| Deep Scenario model support | Commercial 3D model support |
| Anomaly model support | Added support for anomaly model, [sample](./samples/gstreamer/gst_launch/geti_deployment) added as reference, sample added as reference. |
| RealSense element support | New [gvarealsense](./docs/source/elements/gvarealsense.md) element implementation providing basic integration with Intel RealSense cameras, enabling video and depth stream capture for use in GStreamer pipelines. |
| OpenVINO 2025.3 version support | Support of recent OpenVINO version added. |
| GStreamer 1.26.6 version support | Support of recent GStreamer version added. |
| NPU 1.19 version driver support | Support of recent NPU driver version added. |
| Docker image size reduction | Reduction for all images, e.g., Ubuntu 24 Release image size reduced to 1.6GB from 2.6GB |

### Known Issues

| Issue | Issue Description |
|---|---|
| VAAPI memory with `decodebin` | If you are using `decodebin` in conjunction with `vaapi-surface-sharing` preprocessing backend you should set caps filter using `""video/x-raw(memory:VASurface)""` after `decodebin` to avoid issues with pipeline initialization |
| Artifacts on `sycl_meta_overlay` | Running inference results visualization on GPU via `sycl_meta_overlay` may produce some partially drawn bounding boxes and labels |
| Preview Architecture 2.0 Samples | Preview Arch 2.0 samples have known issues with inference results. |
| Sporadic hang on `vehicle_pedestrian_tracking_20_cpu` sample | Using Tiger Lake CPU to run this sample may lead to sporadic hang at 99.9% of video processing. Rerun the sample as W/A or use GPU instead. |
| Simplified installation process for option 2 via script | In certain configurations, users may encounter visible errors |
| Error when using legacy YoloV5 models: Dynamic resize: Model width dimension shall be static | To avoid the issue, modify `samples/download_public_models.sh` by inserting the following snippet at lines 273 and 280: |
| | python3 - <<EOF ""${MODEL_NAME}""<br>import sys, os<br>from openvino.runtime import Core<br>from openvino.runtime import save_model<br>model_name = sys.argv[1]<br>core = Core()<br>os.rename(f""{model_name}_openvino_model"", f""{model_name}_openvino_modelD"")<br>model = core.read_model(f""{model_name}_openvino_modelD/{model_name}.xml"")<br>model.reshape([-1, 3, 640, 640]) |

## Deep Learning Streamer (DL Streamer) Pipeline Framework Release 2025.0.1.3

Intel® Deep Learning Streamer (Intel® DL Streamer) Pipeline Framework is a streaming media analytics framework, based on GStreamer\* multimedia framework, for creating complex media analytics pipelines. It ensures pipeline interoperability and provides optimized media, and inference operations using Intel® Distribution of OpenVINO™ Toolkit Inference Engine backend, across Intel® architecture, CPU, discrete GPU, integrated GPU and NPU.

This release includes DL Streamer Pipeline Framework elements to enable video and audio analytics capabilities, (e.g., object detection, classification, audio event detection), and other elements to build end-to-end optimized pipeline in GStreamer\* framework.

The complete solution leverages:

- Open source GStreamer\* framework for pipeline management
- GStreamer\* plugins for input and output such as media files and real-time streaming from camera or network
- Video decode and encode plugins, either CPU optimized plugins or GPU-accelerated plugins [based on VAAPI](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/tree/main/subprojects/gstreamer-vaapi)
- Deep Learning models converted from training frameworks TensorFlow\*, Caffe\* etc.
- The following elements in the Pipeline Framework repository:

| Element| Description|
|--------|------------|
| [gvadetect](./docs/source/elements/gvadetect.md)| Performs object detection on a full-frame or region of interest (ROI) using object detection models such as YOLOv4-v11, MobileNet SSD, Faster-RCNN etc. Outputs the ROI for detected objects.  |
| [gvaclassify](./docs/source/elements/gvaclassify.md) | Performs object classification. Accepts the ROI as an input and outputs classification results with the ROI metadata.                                                                      |
| [gvainference](./docs/source/elements/gvainference.md) | Runs deep learning inference on a full-frame or ROI using any model with an RGB or BGR input.|
| [gvatrack](./docs/source/elements/gvatrack.md)| Performs object tracking using zero-term, or imageless tracking algorithms. Assigns unique object IDs to the tracked objects.                                                   |
| [gvaaudiodetect](./docs/source/elements/gvaaudiodetect.md) | Performs audio event detection using AclNet model. |
| [gvaattachroi](./docs/source/elements/gvaattachroi.md) | Adds user-defined regions of interest to perform inference on, instead of full frame.|
| [gvafpscounter](./docs/source/elements/gvafpscounter.md) | Measures frames per second across multiple streams in a single process. |
| [gvametaaggregate](./docs/source/elements/gvametaaggregate.md) | Aggregates inference results from multiple pipeline branches |
| [gvametaconvert](./docs/source/elements/gvametaconvert.md) | Converts the metadata structure to the JSON format.|
| [gvametapublish](./docs/source/elements/gvametapublish.md) | Publishes the JSON metadata to MQTT or Kafka message brokers or files. |
| [gvapython](./docs/source/elements/gvapython.md) | Provides a callback to execute user-defined Python functions on every frame. Can be used for metadata conversion, inference post-processing, and other tasks.|
| [gvawatermark](./docs/source/elements/gvawatermark.md) | Overlays the metadata on the video frame to visualize the inference results. |


For the details of supported platforms, please refer to [System Requirements](./docs/source/get_started/system_requirements.md) section.

For installing Pipeline Framework with the prebuilt binaries or Docker\* or to build the binaries from the open source, please refer to [DL Streamer Pipeline Framework installation guide](./docs/source/get_started/install/install_guide_index.md)

## New in this Release

| **Title**      | **High-level description**      |
|----------------|---------------------------------|
| Installation process | Enhanced installation scripts for the 'installation on host' option |
| Post installation steps | Added a selection option for the YOLO model and device to the hello_dlstreamer.sh script |
| Download models | Improved download_public_models.sh script |
| Documentation updates | Improved installation processes descriptions and tutorial refresh |

## Known Issues

| **Issue**   | **Issue Description**  |
|----------------|------------------------|
| VAAPI memory with `decodebin` | If you are using `decodebin` in conjunction with `vaapi-surface-sharing` preprocessing backend you should set caps filter using `"video/x-raw(memory:VASurface)"` after `decodebin` to avoid issues with pipeline initialization |
| Artifacts on `sycl_meta_overlay` | Running inference results visualization on GPU via `sycl_meta_overlay` may produce some partially drawn bounding boxes and labels |
| **Preview** Architecture 2.0 Samples | Preview Arch 2.0 samples have known issues with inference results |
| Sporadic hang on vehicle_pedestrian_tracking_20_cpu sample | Using Tiger Lake CPU to run this sample may lead to sporadic hang at 99.9% of video processing, rerun the sample as W/A or use GPU instead |
| Simplified installation process for option 2 via script | In certain configurations, users may encounter visible errors  |
| Error when using legacy YoloV5 models: Dynamic resize: Model width dimension shall be static | To avoid the issue, modify samples/download_public_models.sh by inserting the following snippet at lines 273 and 280:<br><br>python3 - <<EOF "${MODEL_NAME}"<br>import sys, os<br>from openvino.runtime import Core<br>from openvino.runtime import save_model<br>model_name = sys.argv[1]<br>core = Core()<br>os.rename(f"{model_name}_openvino_model", f"{model_name}_openvino_modelD")<br>model = core.read_model(f"{model_name}_openvino_modelD/{model_name}.xml")<br>model.reshape([-1, 3, 640, 640]) |


# Deep Learning Streamer (DL Streamer) Pipeline Framework Release 2025.0.2

### New in this Release

| **Title**      | **High-level description**      |
|----------------|---------------------------------|
| Geti Models 2.7 version | Support for Geti Classification/Detection Models in 2.7 version  |
| GStreamer plugins | Support for gst-rswebrtc-plugins |
| Documentation updates | Documentation updates - "queue" element  |

# Deep Learning Streamer (DL Streamer) Pipeline Framework Release 2025.0.1

### New in this Release

| **Title**      | **High-level description**      |
|----------------|---------------------------------|
| LVM support | Support for Large Vision Models |
| LVM support | Sample demonstrating image embedding extraction with Visual Transformer (LVM)  |
| OpenVINO 2025.0 support | Update to the latest version of OpenVINO |
| GStreamer 1.24.12 support | Update GStreamer to 1.24.12 version |
| Updated NPU driver | Updated NPU driver to 1.13.0 version. |
| Documentation updates | Documentation how to convert from DeepStream to Deep Learning Steamer |

# Deep Learning Streamer (DL Streamer) Pipeline Framework Release 2025.0.0

### New in this Release

| **Title**      | **High-level description**      |
|----------------|---------------------------------|
|Enhanced support of Intel® Core™ Ultra Processors (Series 2) (formerly codenamed Lunar Lake); enabled va-surface-sharing pre-process backend. | Validated with Ubuntu 24.04, 6.12.3-061203-generic and the latest Intel® Graphics Compute Runtime for oneAPI Level Zero and OpenCL™ Driver v24.52.32224.5 |
|[preview] Enabled Intel® Arc™ B-Series Graphics [products formerly Battlemage] | Validated with Ubuntu 24.04, 6.12.3-061203-generic and the latest Intel® Graphics Compute Runtime for oneAPI Level Zero and OpenCL™ Driver v24.52.32224.5 + the latest public Intel Graphics Media Driver version + pre-rerelease Intel® Graphics Memory Management Library version |
| OpenVINO 2024.6 support | Update to the latest version of OpenVINO |
| Updated NPU driver | Updated NPU driver to 1.10.1 version. |
| Bug fixing | Running multiple gstreamer pipeline objects in the same process on dGPU leads to error; DL Streamer docker image build is failing (2024.2.2 and 2024.3.0 versions); Fixed installation scripts: minor fixes of GPU, NPU installation section; Updated documentation: cleanup, added missed parts, added DLS system requirements |


# Deep Learning Streamer (DL Streamer) Pipeline Framework Release 2024.3.0

### New in this Release

| **Title**      | **High-level description**      |
|----------------|---------------------------------|
| GStreamer 1.24.10  | Updated GStreamer to the 1.24.10 version |
| Documentation for MQTT  |	Documentation for MQTT updated |
| Added support for numactl  |	Added support for numactl in the docker image |
| Enabled Intel® Core™ Ultra Processors (Series 2) (formerly codenamed Lunar Lake)  | Validated with Ubuntu 24.04, 6.12.3-061203-generic  |


# Deep Learning Streamer (DL Streamer) Pipeline Framework Release 2024.2.2

### New in this Release

| **Title**      | **High-level description**      |
|----------------|---------------------------------|
| Installation of DL Streamer Pipeline Framework from Debian packages using APT repository | Support for apt-get install has been added. |
| Yolo11s-pose support | Added support for Yolo11s-pose model. |
| Change in gvafpscounter element | Reset FPS counters whenever a stream is added/removed. |
| OpenVINO updated | OpenVINO updated to the 2024.5 version. |
| GStreamer 1.24.9 | Updated GStreamer to the 1.24.9 version. |
| NPU 1.10.0 | NPU drivers updated to NPU 1.10.0 version. |
| Bugs fixing | Fixed issue with failing performance tests ; Fixed fuzzy tests ; Enabled debug mode ; Created TLS configuration that allows for secure communication between DL Streamer and MQTT broker; Fixed python error: init_threadstate: thread state already initialized; Fixed problem with DLS compilation / GSTreamer base plugin error.; Fixed issue with sample_test: python_draw_face_attributes on Ubuntu 24.04; Fixed issue with sample_test: gvapython cpu/gpu on Ubuntu 24.04 |

## Deep Learning Streamer (DL Streamer) Pipeline Framework Release 2024.2.1

### New in this Release

| **Title**      | **High-level description**      |
|----------------|---------------------------------|
| Update NPU drivers to version 1.8.0 | Update NPU driver version inside docker images|
| Yolo 11 | Added support for YOLO 11 model (CPU and GPU only) |
| GStreamer | GStreamer updated to the 1.24.8 version  |
| Fix Github issue: [#440](https://github.com/dlstreamer/dlstreamer/issues/440) | gvapython error: Fatal Python error: init_threadstate: thread state already initialized |

## Deep Learning Streamer (DL Streamer) Pipeline Framework Release 2024.2.0

### New in this Release

| **Title**      | **High-level description**      |
|----------------|---------------------------------|
|New models support: Yolov10 for GPU, DeepLabv3 | Support for most recent Yolov10 model for GPU and DeepLabv3 (semantic segmentation) |
|UTC format for timestamp| Timestamp can be shown in UTC format based on system time with option to synchronize it from NTP server |
|OpenVINO 2024.4 support | Update to latest version of OpenVINO |
|GStreamer 1.24.7 support | Update to latest version of GStreamer |
|Intel® NPU 1.6.0 driver support | Support for newer version of Intel® NPU Linux driver |
|Simplified installation process for option#1 (i.e. Ubuntu packages) via script|Development of the script that enhances user experience during installation of DL Streamer with usage of option#1. |
|Documentation improvements|Descriptions enhancements in various points.|
[Preview feature] Simplified installation process for option#2 via script|Development of the script that enhances user experience during installation of DL Streamer with usage of option#2..|

### Fixed Issues

| **Issue**   | **Issue Description**  |
|----------------|------------------------|
|Github issue: [#431](https://github.com/dlstreamer/dlstreamer/issues/431)|WARNING: erroneous pipeline: no element "gvadetect"|
|Github issue: [#433](https://github.com/dlstreamer/dlstreamer/issues/433)|WARNING: erroneous pipeline: no element "gvaattachroi" inside Docker image 2024.1.2-dev-ubuntu24|
|Github issue: [#434](https://github.com/dlstreamer/dlstreamer/issues/434)| Proper way to use object-class under gvadetect |
|Github issue: #[435](https://github.com/dlstreamer/dlstreamer/issues/435)| No such element or plugin 'gvadetect' |
|Internal findings|installation via option#3 documentation fixes; fixed hangs on MTL NPU for INT8 models; fixed issues with using 4xFlex170 system|

## Deep Learning Streamer (DL Streamer) Pipeline Framework Release 2024.1.2

### New in this Release

| **Title**      | **High-level description**      |
|----------------|---------------------------------|
|New models support: Yolov10 for CPU only,Yolov8 instance segmentation | Support for most recent Yolov10 model for CPU and extension for Yolov8 |
|New elements: gvaattachroi including documentation update + samples) | Added element documentation and sample development which introduces ability to define the area of interest on which the inference should be performed |
|OpenVINO 2024.3 support | Update to latest version of OpenVINO |
|GStreamer 1.24.6 support | Update to latest version of GStreamer |
|Ubuntu 24.04 support | Support for newer version of Ubuntu |
|Documentation updates for DeepStream to DL Streamer migration process | Updates to the migration process from Deep Stream |
|Documentation improvements | Descriptions enhancements in various points |
[Preview feature] Simplified installation process for option#1 via script | Development of the script that enhances user experience during installation of DL Streamer with usage of option#1 |

### Fixed Issues

| **Issue**   | **Issue Description**  |
|----------------|------------------------|
| [#425](https://github.com/dlstreamer/dlstreamer/issues/425) | when using inference-region=roi-list vs full-frame in my classification pipeline, classification data does not get published |
| [#432](https://github.com/dlstreamer/dlstreamer/issues/432) | Installation issues with gst-ugly plugins |
| [#397](https://github.com/dlstreamer/dlstreamer/issues/397) | Installation Error DL Streamer - Both Debian Packages and Compile from Sources |
| Internal findings | custom efficientnetb0 fix, issue with selection region before inference, Geti classification model fix, dGPU vah264enc element not found error fix, sample: face_detection_and_classifiation fix|

## Deep Learning Streamer (DL Streamer) Pipeline Framework Release 2024.1.1

### New in this Release

| **Title**      | **High-level description**      |
|----------------|---------------------------------|
| Missing git package | Git package added to DL Streamer docker runtime image |
| VTune when running DL Streamer | Publish instructions to install and run VTune to analyze media + gpu when running DL Streamer  |
| Update NPU drivers to version 1.5.0 | Update NPU driver version inside docker images|
| Instance_segmentation sample | Add new Mask-RCNN segmentation sample |
| Documentation updates | Enhance Performance Guide and Model Preparation section |
| Fix samples errors | Fixed errors on action_recognition, geti, yolo and ffmpeg (customer issue) samples |
| Fix memory grow with `meta_overlay` | Fix for Meta Overlay memory leak with DLS Arch 2.0 |
| Fix pipeline which failed to start with mobilenet-v2-1.0-224 model  |
| Fix batch-size error -> with yolov8 model and other yolo models |

## Deep Learning Streamer (DL Streamer) Pipeline Framework Release 2024.1.0

### New in this Release


| **Title**      | **High-level description**      |
|----------------|---------------------------------|
| Switch to ‘gst-va’ as default processing path instead of ‘gst-vaapi’ | Switch to ‘gst-va’ as default processing path instead of ‘gst-vaapi’ |
| Add support for ‘gst-qsv’ plugins | Add support for ‘qsv’ plugins |
| New public ONNX models: Centerface and HSEmotion | New public ONNX models: Centerface and HSEmotion |
| Update Gstreamer version to the latest one (current 1.24) | Update Gstreamer version to the latest one (1.24.4) |
| Update OpenVINO version to latest one (2024.2.0) | Update OpenVINO version to latest one (2024.2.0) |
| Release docker images on DockerHUB: runtime and dev | Release docker images on DockerHUB: runtime and dev |
| Bugs fixing | Bug fixed: GPU not detected in Docker container Dlstreamer - MTL platform; Updated docker images with proper GPU and NPU packages; yolo5 model failed with batch-size >1; Remove excessive ‘mbind failed:...’ warning logs |
| Documentation updates | Added sample applications for Mask-RCNN instance segmentation. Added list of supported models from Open Model Zoo and public repos. Added scripts to generate DL Streamer-consumable models from public repos. Document usage of ModelAPI properties in OpenVINO IR (model.xml) instead of creating custom model_proc files. Updated installation instructions for docker images. |

### Fixed issues

| **Issue \#**   | **Issue Description**  | **Fix**  | **Affected platforms**  |
|----------------|------------------------|-----------------|-------------------------|
| 421 | [Can we specify the IOU threshold in yolov8 post-process json like yolov5?](https://github.com/dlstreamer/dlstreamer/issues/421) | Same solution as in [#394](https://github.com/dlstreamer/dlstreamer/issues/394) | All |
| 420 | [there is a customer's detect model need to support](https://github.com/dlstreamer/dlstreamer/issues/420) | Support for Centerface and HSEmotion added | All |

## Deep Learning Streamer (DL Streamer) Pipeline Framework Release 2024.0.2

### New in this Release

| **Title**      | **High-level description**      |
|----------------|---------------------------------|
| Support for ‘gst-va’ in addition to ‘gst-vaapi’ | Support for ‘gst-va’ in addition to ‘gst-vaapi’ |
| Add support for EfficentNetv2 (classification), MaskRCNN (instance segmentation) and Yolo8-OBB (oriented bounding box) | New classification model supported EfficentNetv2, new instance segmentation model supported MaskRCNN and oriented bounding box model as well added Yolo8-OBB |
| Support additional GETI models: segmentation, obb | GETI public models support added |
| Generalized method to deploy new models without need for model-proc file | Support model information embedded into AI model descriptors according to OpenVINO Model API |
| Release docker images on DockerHUB: runtime | Added docker images on DockerHUB: runtime |
| Added support for OpenVINO 2024.1.0 | Added support for OpenVINO 2024.1.0 |

### Fixed issues

| **Issue \#**   | **Issue Description**  | **Fix**  | **Affected platforms**  |
|----------------|------------------------|-----------------|-------------------------|
| 407 | [EfficientNet-B1 support](https://github.com/dlstreamer/dlstreamer/issues/407) | We do not plan to support older DL Streamer releases with API1.0, I highly recommend to switch to newer version compatible with latest OpenVINO | All |
| 410 | [cant run againts my camera feed](https://github.com/dlstreamer/dlstreamer/issues/410) | Config error, user opened a new issue for tracking the yolo issue and was able to see cameras now | All |
| 412 | [with Docker cmd, cant create and dowload models inside docker](https://github.com/dlstreamer/dlstreamer/issues/412) | Config error. Without $ sign, when assigning a value (using $ to retrieve the value of a variable, e.g. to print the value): `$ export MODELS_PATH=/home/dlstreamer/temp/models1 | All |
| 413 | [ffmpeg_openvino build failed, LibAV](https://github.com/dlstreamer/dlstreamer/issues/413) | Possible config error, missing libraries but no feedback given for 2 weeks so the issue was closed | All |
| 415 | [cant run against Efficientnet-b0 due to model exceeds allowable size of 10MB](https://github.com/dlstreamer/dlstreamer/issues/415) | Resolved, user was able to get it running with last suggestion to use implemented RealSense specific gstreamer plugins, like •https://github.com/WKDSMRT/realsense-gstreamer => `realsensesrc` •https://gitlab.com/aivero/legacy/public/gstreamer/gst-realsense => `realsensesrc` A couple years old... | All |
| 416 | [detection with yolo not available on latest](https://github.com/dlstreamer/dlstreamer/issues/416) | Continue with "merged" command-line, using `videobox` and or `videomixer` (or many different other ways from the internet). You might need to start again... and checking the setup on your HOST. I'm using Ubuntu 22.04LTS. Created a non-root-user. Adding the user to video and render groups. Installed docker and configured to use Docker as non-root (without using "sudo" when using "docker run"). Before starting the container, I just call xhost +. Passing the render-group-id to "docker run" (in my case `--group-add=110`) `docker run -it --net=host --device=/dev/dri --device=/dev/video0 --device=/dev/video1 --group-add=110 -v ~/.Xauthority:/home/dlstreamer/.Xauthority -v /tmp/.X11-unix -e DISPLAY=$DISPLAY -v /dev/bus/usb dlstreamer /bin/bash` (not using `-u 0 --privileged`)  | All |

## Deep Learning Streamer (DL Streamer) Pipeline Framework Release 2024.0.1

### New in this Release


| **Title**      | **High-level description**      |
|----------------|---------------------------------|
| Add support for latest Ultralytics YOLO models | Add support for latest Ultralytics YOLO models: -v7, -v8, -v9 |
| Add support for YOLOX models | Add support for YOLOX models |
| Support deployment of GETI-trained models | Support models trained by GETI v1.8: bounding-box detection and classification (single and multi-label) |
| Automatic pre-/post-processing based on model descriptor | Automatic pre-/post-processing based on model descriptor (model-proc file not required): yolov8, yolov9 and GETI |
| Docker image size reduction | Reduced docker image size generated from the published docker file |

### Fixed issues

| **Issue \#**   | **Issue Description**  | **Fix**  | **Affected platforms**  |
|----------------|------------------------|-----------------|-------------------------|
| 390 | [How to install packages with sudo inside the docker container intel/dlstreamer:latest](https://github.com/dlstreamer/dlstreamer/issues/390) | start the container as mentioned above with root-user `(-u 0) docker run -it -u 0 --rm`... and then are able to update binaries | All |
| 392 | [installation error dlstreamer with openvino 2023.2](https://github.com/dlstreamer/dlstreamer/issues/392) | 2024.0 version supports API 2.0 so I highly recommend to check it and in case if this problem is still valid please raise new issue | All |
| 393 | [Debian file location for DL Streamer 2022.3](https://github.com/dlstreamer/dlstreamer/issues/393) | Error no longer occurring for user | All |
| 394 | [Custom YoloV5m Accuracy Drop in dlstreamer with model proc](https://github.com/dlstreamer/dlstreamer/issues/394) | Procedure to transform crowdhuman_yolov5m.pt model to the openvino version that can be used directly in DL Streamer with Yolo_v7 converter (no layer cutting required) * `git clone https://github.com/ultralytics/yolov5 * cd yolov5 * pip install -r requirements.txt openvino-dev * python export.py --weights crowdhuman_yolov5m.pt --include openvino` | All |
| 396 | [Segfault when reuse same model with same model-instance-id.](https://github.com/dlstreamer/dlstreamer/issues/396) | 2024.0 version supports API 2.0 so I highly recommend to check it and in case if this problem is still valid please raise new issue | All |
| 404 | [How to generate model proc file for yolov8?](https://github.com/dlstreamer/dlstreamer/issues/404) | Added as a feature in this release | All |
| 406 | [yolox support](https://github.com/dlstreamer/dlstreamer/issues/406) | Added as a feature in this release | All |
| 409 | [ERROR: from element /GstPipeline:pipeline0/GstGvaDetect:gvadetect0: base_inference plugin initialization failed](https://github.com/dlstreamer/dlstreamer/issues/409) | Suggested temporarily - to use a root-user when running the container image, like `docker run -it -u 0 [... .add here your other parameters.. ...]`, to get more permissions | All |

## Deep Learning Streamer (DL Streamer) Pipeline Framework Release 2024.0

### New in this Release

| **Title**      | **High-level description**      |
|----------------|---------------------------------|
| Intel® Core™ Ultra processors NPU support | Inference on NPU devices has been added, validated with Intel(R) Core(TM) Ultra 7 155H |
| Compatibility with OpenVINO™ Toolkit 2024.0 | Pipeline Framework has been updated to use the 2024.0.0 version of the OpenVINO™ Toolkit |
| Compatibility with GStreamer 1.22.9 | Pipeline Framework has been updated to use GStreamer framework version 1.22.9 |
| Updated to FFmpeg 6.1.1 | Updated FFmpeg from 5.1.3 to 6.1.1 |
| Performance optimizations | 8% geomean gain across tested scenarios, up to 50% performance gain in multi-stream scenarios |
| Docker image replaced with Docker file | Ubuntu 22.04 docker file is released instead of docker image. |

## Additional Information

### System Requirements

Please refer to [DL Streamer documentation](./docs/source/get_started/system_requirements.md).

## Installation Notes

There are several installation options for Pipeline Framework:

1. Install Pipeline Framework from pre-built Debian packages
1. Build Docker image from docker file and run Docker image
1. Build Pipeline Framework from source code

For more detailed instructions please refer to [DL Streamer Pipeline Framework installation guide](./docs/source/get_started/install/install_guide_index.md).

## Samples

The [samples](https://github.com/open-edge-platform/dlstreamer/tree/master/samples) folder in DL Streamer Pipeline Framework repository contains command line, C++ and Python examples.

## Legal Information

Intel technologies’ features and benefits depend on system configuration and may require enabled hardware, software, or service activation. Learn more at intel.com, or from the OEM or retailer.

No computer system can be absolutely secure. Intel does not assume any liability for lost or stolen data or systems or any damages resulting from such losses.

You may not use or facilitate the use of this document in connection with any infringement or other legal analysis concerning Intel products described herein. You agree to grant Intel a non-exclusive, royalty-free license to any patent claim thereafter drafted which includes subject matter disclosed herein.

No license (express or implied, by estoppel or otherwise) to any intellectual property rights is granted by this document.

Intel disclaims all express and implied warranties, including without limitation, the implied warranties of merchantability, fitness for a particular purpose, and non-infringement, as well as any warranty arising from course of performance, course of dealing, or usage in trade.

This document contains information on products, services and/or processes in development. All information provided here is subject to change without notice. Contact your Intel representative to obtain the latest forecast, schedule, specifications and roadmaps.

The products and services described may contain defects or errors which may cause deviations from published specifications. Current characterized errata are available on request.

Intel, the Intel logo, and Xeon are trademarks of Intel Corporation in the U.S. and/or other countries.

FFmpeg is an open source project licensed under LGPL and GPL. See https://www.ffmpeg.org/legal.html. You are solely responsible for determining if your use of FFmpeg requires any additional licenses. Intel is not responsible for obtaining any such licenses, nor liable for any licensing fees due, in connection with your use of FFmpeg.

GStreamer is an open source framework licensed under LGPL. See https://gstreamer.freedesktop.org/documentation/frequently-asked-questions/licensing.html. You are solely responsible for determining if your use of GStreamer requires any additional licenses. Intel is not responsible for obtaining any such licenses, nor liable for any licensing fees due, in connection with your use of GStreamer.

*Other names and brands may be claimed as the property of others.

© 2025 Intel Corporation.
