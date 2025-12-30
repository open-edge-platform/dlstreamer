# Intel® Deep Learning Streamer (Intel® DL Streamer) Pipeline Framework
## DL Streamer is now part of Open Edge Platform

### Overview
<div align="center"><img src="intro.gif" width=900/></div>

[Deep Learning Streamer](./docs/source/index.md) (**DL Streamer**) Pipeline Framework is an open-source streaming media analytics framework, based on [GStreamer*](https://gstreamer.freedesktop.org) multimedia framework, for creating complex media analytics pipelines for the Cloud or at the Edge.

**Media analytics** is the analysis of audio & video streams to detect, classify, track, identify and count objects, events and people. The analyzed results can be used to take actions, coordinate events, identify patterns and gain insights across multiple domains: retail store and events facilities analytics, warehouse and parking management, industrial inspection, safety and regulatory compliance, security monitoring, and many other.

## Backend libraries
DL Streamer Pipeline Framework is optimized for performance and functional interoperability between GStreamer* plugins built on various backend libraries
* Inference plugins use [OpenVINO™ inference engine](https://docs.openvino.ai) optimized for Intel CPU, GPU and VPU platforms
* Video decode and encode plugins utilize [GPU-acceleration based on VA-API](https://github.com/GStreamer/gstreamer-vaapi)
* Image processing plugins based on [OpenCV](https://opencv.org/) and [DPC++](https://www.intel.com/content/www/us/en/develop/documentation/oneapi-programming-guide/top/oneapi-programming-model/data-parallel-c-dpc.html)
* Hundreds other [GStreamer* plugins](https://gstreamer.freedesktop.org/documentation/plugins_doc.html) built on various open-source libraries for media input and output, muxing and demuxing, decode and encode

[This page](./docs/source/elements/elements.md) contains a list of elements provided in this repository.

## Prerequisites
Please refer to [System Requirements](./docs/source/get_started/system_requirements.md) for details.

## Installation
Please refer to [Install Guide](./docs/source/get_started/install/install_guide_ubuntu.md) for installation options
1. [Install APT packages](./docs/source/get_started/install/install_guide_ubuntu.md#option-1-install-intel-dl-streamer-pipeline-framework-from-debian-packages-using-apt-repository)
2. [Run Docker image](./docs/source/get_started/install/install_guide_ubuntu.md#option-2-install-docker-image-from-docker-hub-and-run-it)
3. [Compile from source code](./docs/source/dev_guide/advanced_install/advanced_install_guide_compilation.md)
4. [Build Docker image from source code](./docs/source/dev_guide/advanced_install/advanced_build_docker_image.md)

To see the full list of installed components check the [dockerfile content for Ubuntu24](https://github.com/open-edge-platform/dlstreamer/blob/master/docker/ubuntu/ubuntu24.Dockerfile)

## Samples
[Samples](https://github.com/open-edge-platform/dlstreamer/tree/master/samples) available for C/C++ and Python programming, and as gst-launch command lines and scripts.

## NN models
DL Streamer supports NN models in OpenVINO™ IR and ONNX* formats.
List of [supported models](./docs/source/supported_models.md) which include LVM, object detection, object classification, human pose detection, sound classification, semantic segmentation, and other use cases on SSD, MobileNet, YOLO, Tiny YOLO, EfficientDet, ResNet, FasterRCNN and other backbones also pre-trained with [Intel® Geti™ Software](<https://www.intel.com/content/www/us/en/developer/tools/tiber/edge-platform/model-builder.html>)

Or you can start from over 70 pre-trained models in [OpenVINO™ Open Model Zoo](https://docs.openvino.ai/latest/omz_models_group_intel.html) and corresponding model-proc files (pre- and post-processing specification) in [/opt/intel/dlstreamer/samples/model_proc](https://github.com/dlstreamer/dlstreamer/tree/master/samples/model_proc) folder.
These models include object detection, object classification, human pose detection, sound classification, semantic segmentation, and other use cases on SSD, MobileNet, YOLO, Tiny YOLO, EfficientDet, ResNet, FasterRCNN and other backbones.

## Other Useful Links
* [Get Started](./docs/source/get_started/get_started_index.md)
* [Developer Guide](./docs/source/dev_guide/dev_guide_index.md)
* [API Reference](./docs/source/api_ref/api_reference.rst)

---
\* Other names and brands may be claimed as the property of others.

## License

The **DL Streamer** project is licensed under the [MIT License](./LICENSE) license.

