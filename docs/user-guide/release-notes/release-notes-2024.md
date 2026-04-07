# Release Notes 2024

## Version 2024.3.0

**New**

| **Title**      | **High-level description**      |
|----------------|---------------------------------|
| GStreamer 1.24.10 | Updated GStreamer to the 1.24.10 version |
| Documentation for MQTT | Documentation for MQTT updated |
| Added support for numactl  |Added support for numactl in the docker image |
| Enabled Intel® Core™ Ultra Processors (Series 2) (formerly codenamed Lunar Lake) | Validated with Ubuntu 24.04, 6.12.3-061203-generic  |

## Version 2024.2.2

**New**

| **Title**      | **High-level description**      |
|----------------|---------------------------------|
| Installation of DL Streamer Pipeline Framework from Debian packages using APT repository | Support for apt-get install has been added. |
| Yolo11s-pose support | Added support for Yolo11s-pose model. |
| Change in gvafpscounter element | Reset FPS counters whenever a stream is added/removed. |
| OpenVINO updated | OpenVINO updated to the 2024.5 version. |
| GStreamer 1.24.9 | Updated GStreamer to the 1.24.9 version. |
| NPU 1.10.0 | NPU drivers updated to NPU 1.10.0 version. |
| Bugs fixing | Fixed issue with failing performance tests ; Fixed fuzzy tests ; Enabled debug mode ; Created TLS configuration that allows for secure communication between DL Streamer and MQTT broker; Fixed python error: init_threadstate: thread state already initialized; Fixed problem with DLS compilation / GSTreamer base plugin error.; Fixed issue with sample_test: python_draw_face_attributes on Ubuntu 24.04; Fixed issue with sample_test: gvapython cpu/gpu on Ubuntu 24.04 |

## Version 2024.2.1

**New**

| **Title**      | **High-level description**      |
|----------------|---------------------------------|
| Update NPU drivers to version 1.8.0 | Update NPU driver version inside docker images|
| Yolo 11 | Added support for YOLO 11 model (CPU and GPU only) |
| GStreamer | GStreamer updated to the 1.24.8 version  |
| Fix Github issue: [#440](https://github.com/open-edge-platform/dlstreamer/issues/440) | gvapython error: Fatal Python error: init_threadstate: thread state already initialized |

## Version 2024.2.0

**New**

| **Title**      | **High-level description**      |
|----------------|---------------------------------|
|New models support: Yolov10 for GPU, DeepLabv3 | Support for most recent Yolov10 model for GPU and DeepLabv3 (semantic segmentation) |
|UTC format for timestamp| Timestamp can be shown in UTC format based on system time with option to synchronize it from NTP server |
|OpenVINO 2024.4 support | Update to latest version of OpenVINO |
|GStreamer 1.24.7 support | Update to latest version of GStreamer |
|Intel® NPU 1.6.0 driver support | Support for newer version of Intel® NPU Linux driver |
|Simplified installation process for option#1 (i.e. Ubuntu packages) via script|Development of the script that enhances user experience during installation of DL Streamer with usage of option#1. |
|Documentation improvements|Descriptions enhancements in various points.|
(Preview feature) Simplified installation process for option#2 via script|Development of the script that enhances user experience during installation of DL Streamer with usage of option#2..|

**Fixed**

| **Issue**   | **Issue Description**  |
|----------------|------------------------|
|Github issue: [#431](https://github.com/open-edge-platform/dlstreamer/issues/431)|WARNING: erroneous pipeline: no element "gvadetect"|
|Github issue: [#433](https://github.com/open-edge-platform/dlstreamer/issues/433)|WARNING: erroneous pipeline: no element "gvaattachroi" inside Docker image 2024.1.2-dev-ubuntu24|
|Github issue: [#434](https://github.com/open-edge-platform/dlstreamer/issues/434)| Proper way to use object-class under gvadetect |
|Github issue: #[435](https://github.com/open-edge-platform/dlstreamer/issues/435)| No such element or plugin 'gvadetect' |
|Internal findings|installation via option#3 documentation fixes; fixed hangs on MTL NPU for INT8 models; fixed issues with using 4xFlex170 system|

## Version 2024.1.2

**New**

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

**Fixed**

| **Issue**   | **Issue Description**  |
|----------------|------------------------|
| [#425](https://github.com/open-edge-platform/dlstreamer/issues/425) | when using inference-region=roi-list vs full-frame in my classification pipeline, classification data does not get published |
| [#432](https://github.com/open-edge-platform/dlstreamer/issues/432) | Installation issues with gst-ugly plugins |
| [#397](https://github.com/open-edge-platform/dlstreamer/issues/397) | Installation Error DL Streamer - Both Debian Packages and Compile from Sources |
| Internal findings | custom efficientnetb0 fix, issue with selection region before inference, Geti classification model fix, dGPU vah264enc element not found error fix, sample: face_detection_and_classifiation fix|

## Version 2024.1.1

**New**

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

## Version 2024.1.0

**New**

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

**Fixed**

| **Issue \#**   | **Issue Description**  | **Fix**  | **Affected platforms**  |
|----------------|------------------------|-----------------|-------------------------|
| 421 | [Can we specify the IOU threshold in yolov8 post-process json like yolov5?](https://github.com/open-edge-platform/dlstreamer/issues/421) | Same solution as in [#394](https://github.com/open-edge-platform/dlstreamer/issues/394) | All |
| 420 | [there is a customer's detect model need to support](https://github.com/open-edge-platform/dlstreamer/issues/420) | Support for Centerface and HSEmotion added | All |

## Version 2024.0.2

**New**

| **Title**      | **High-level description**      |
|----------------|---------------------------------|
| Support for ‘gst-va’ in addition to ‘gst-vaapi’ | Support for ‘gst-va’ in addition to ‘gst-vaapi’ |
| Add support for EfficentNetv2 (classification), MaskRCNN (instance segmentation) and Yolo8-OBB (oriented bounding box) | New classification model supported EfficentNetv2, new instance segmentation model supported MaskRCNN and oriented bounding box model as well added Yolo8-OBB |
| Support additional GETI models: segmentation, obb | GETI public models support added |
| Generalized method to deploy new models without need for model-proc file | Support model information embedded into AI model descriptors according to OpenVINO Model API |
| Release docker images on DockerHUB: runtime | Added docker images on DockerHUB: runtime |
| Added support for OpenVINO 2024.1.0 | Added support for OpenVINO 2024.1.0 |

**Fixed**

| **Issue \#**   | **Issue Description**  | **Fix**  | **Affected platforms**  |
|----------------|------------------------|-----------------|-------------------------|
| 407 | [EfficientNet-B1 support](https://github.com/open-edge-platform/dlstreamer/issues/407) | We do not plan to support older DL Streamer releases with API1.0, I highly recommend to switch to newer version compatible with latest OpenVINO | All |
| 410 | [cant run againts my camera feed](https://github.com/open-edge-platform/dlstreamer/issues/410) | Config error, user opened a new issue for tracking the yolo issue and was able to see cameras now | All |
| 412 | [with Docker cmd, cant create and dowload models inside docker](https://github.com/open-edge-platform/dlstreamer/issues/412) | Config error. Without $ sign, when assigning a value (using $ to retrieve the value of a variable, e.g. to print the value): `$ export MODELS_PATH=/home/dlstreamer/temp/models1 | All |
| 413 | [ffmpeg_openvino build failed, LibAV](https://github.com/open-edge-platform/dlstreamer/issues/413) | Possible config error, missing libraries but no feedback given for 2 weeks so the issue was closed | All |
| 415 | [cant run against Efficientnet-b0 due to model exceeds allowable size of 10MB](https://github.com/open-edge-platform/dlstreamer/issues/415) | Resolved, user was able to get it running with last suggestion to use implemented RealSense specific gstreamer plugins, like •https://github.com/WKDSMRT/realsense-gstreamer => `realsensesrc` •https://gitlab.com/aivero/legacy/public/gstreamer/gst-realsense => `realsensesrc` A couple years old... | All |
| 416 | [detection with yolo not available on latest](https://github.com/open-edge-platform/dlstreamer/issues/416) | Continue with "merged" command-line, using `videobox` and or `videomixer` (or many different other ways from the internet). You might need to start again... and checking the setup on your HOST. I'm using Ubuntu 22.04LTS. Created a non-root-user. Adding the user to video and render groups. Installed docker and configured to use Docker as non-root (without using "sudo" when using "docker run"). Before starting the container, I just call xhost +. Passing the render-group-id to "docker run" (in my case `--group-add=110`) `docker run -it --net=host --device=/dev/dri --device=/dev/video0 --device=/dev/video1 --group-add=110 -v ~/.Xauthority:/home/dlstreamer/.Xauthority -v /tmp/.X11-unix -e DISPLAY=$DISPLAY -v /dev/bus/usb dlstreamer /bin/bash` (not using `-u 0 --privileged`)  | All |

## Version 2024.0.1

**New**

| **Title**      | **High-level description**      |
|----------------|---------------------------------|
| Add support for latest Ultralytics YOLO models | Add support for latest Ultralytics YOLO models: -v7, -v8, -v9 |
| Add support for YOLOX models | Add support for YOLOX models |
| Support deployment of GETI-trained models | Support models trained by GETI v1.8: bounding-box detection and classification (single and multi-label) |
| Automatic pre-/post-processing based on model descriptor | Automatic pre-/post-processing based on model descriptor (model-proc file not required): yolov8, yolov9 and GETI |
| Docker image size reduction | Reduced docker image size generated from the published docker file |

**Fixed**

| **Issue \#**   | **Issue Description**  | **Fix**  | **Affected platforms**  |
|----------------|------------------------|-----------------|-------------------------|
| 390 | [How to install packages with sudo inside the docker container intel/dlstreamer:latest](https://github.com/open-edge-platform/dlstreamer/issues/390) | start the container as mentioned above with root-user `(-u 0) docker run -it -u 0 --rm`... and then are able to update binaries | All |
| 392 | [installation error dlstreamer with openvino 2023.2](https://github.com/open-edge-platform/dlstreamer/issues/392) | 2024.0 version supports API 2.0 so I highly recommend to check it and in case if this problem is still valid please raise new issue | All |
| 393 | [Debian file location for DL Streamer 2022.3](https://github.com/open-edge-platform/dlstreamer/issues/393) | Error no longer occurring for user | All |
| 394 | [Custom YoloV5m Accuracy Drop in dlstreamer with model proc](https://github.com/open-edge-platform/dlstreamer/issues/394) | Procedure to transform crowdhuman_yolov5m.pt model to the openvino version that can be used directly in DL Streamer with Yolo_v7 converter (no layer cutting required) * `git clone https://github.com/ultralytics/yolov5 * cd yolov5 * pip install -r requirements.txt openvino-dev * python export.py --weights crowdhuman_yolov5m.pt --include openvino` | All |
| 396 | [Segfault when reuse same model with same model-instance-id.](https://github.com/open-edge-platform/dlstreamer/issues/396) | 2024.0 version supports API 2.0 so I highly recommend to check it and in case if this problem is still valid please raise new issue | All |
| 404 | [How to generate model proc file for yolov8?](https://github.com/open-edge-platform/dlstreamer/issues/404) | Added as a feature in this release | All |
| 406 | [yolox support](https://github.com/open-edge-platform/dlstreamer/issues/406) | Added as a feature in this release | All |
| 409 | [ERROR: from element /GstPipeline:pipeline0/GstGvaDetect:gvadetect0: base_inference plugin initialization failed](https://github.com/open-edge-platform/dlstreamer/issues/409) | Suggested temporarily - to use a root-user when running the container image, like `docker run -it -u 0 [... .add here your other parameters.. ...]`, to get more permissions | All |

## Version 2024.0

**New**

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

Please refer to [DL Streamer documentation](../get_started/system_requirements.md).

## Installation Notes

There are several installation options for Pipeline Framework:

1. Install Pipeline Framework from pre-built Debian packages
1. Build Docker image from docker file and run Docker image
1. Build Pipeline Framework from source code

For more detailed instructions please refer to [DL Streamer Pipeline Framework installation guide](../get_started/install/install_guide_index.md).

## Samples

The [samples](https://github.com/open-edge-platform/dlstreamer/tree/main/samples) folder in DL Streamer Pipeline Framework repository contains command line, C++ and Python examples.
