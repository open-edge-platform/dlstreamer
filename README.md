<div align="center">

# Deep Learning Streamer (DL Streamer)

**GPU-accelerated video analytics pipelines — from a single line of code to production-grade edge AI**

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](./LICENSE)
[![Ubuntu](https://img.shields.io/badge/Ubuntu-22.04%20%7C%2024.04-orange?logo=ubuntu)](./docs/user-guide/get_started/system_requirements.md)
[![Windows](https://img.shields.io/badge/Windows-11-blue?logo=windows)](./docs/user-guide/get_started/system_requirements.md)
[![OpenVINO](https://img.shields.io/badge/OpenVINO-powered-blue)](https://docs.openvino.ai)
[![GStreamer](https://img.shields.io/badge/GStreamer-based-brightgreen)](https://gstreamer.freedesktop.org)
[![Docker](https://img.shields.io/badge/Docker-available-2496ED?logo=docker)](https://hub.docker.com/r/intel/dlstreamer)
[![Part of Open Edge Platform](https://img.shields.io/badge/Open%20Edge%20Platform-member-0071C5)](https://github.com/open-edge-platform)

<img src="./docs/hero_samples.jpg" width="800" alt="DL Streamer sample outputs">

[Get Started](#-quick-start) • [Samples](./samples/gstreamer/README.md) • [Elements](./docs/user-guide/elements/elements.md) • [Documentation](./docs/user-guide/index.md) • [Contributing](./CONTRIBUTING.md)

</div>

---

## What is DL Streamer?

**DL Streamer** is an open-source media analytics framework built on [GStreamer](https://gstreamer.freedesktop.org). It lets you build video and audio intelligence pipelines — from a simple object detection command line to a multi-stream, multi-sensor production deployment — with minimal code.

- Powered by **[OpenVINO™](https://docs.openvino.ai)** for optimized inference on Intel CPU, GPU, and NPU.
- Pipelines are described as **simple strings** (or Python/C++ code) and executed with full hardware acceleration.
- Ships with **30+ ready-to-run samples** covering detection, classification, tracking, VLMs, LiDAR and more.
- Part of the **[Intel Open Edge Platform](https://github.com/open-edge-platform)**.

---

## Why DL Streamer?

| Benefit | Details |
|---|---|
| **One-line pipelines** | Build a working detection pipeline in a single `gst-launch-1.0` command |
| **Hardware acceleration** | Targets CPU, GPU, and NPU on Intel platforms from a single codebase |
| **VLM & GenAI ready** | Run Vision-Language Models (MiniCPM-V, CLIP, Whisper) in a GStreamer pipeline |
| **Production metadata** | Structured JSON output to MQTT, Kafka, or file with built-in elements |
| **Python-first extensibility** | Add custom logic as Python callbacks or full Python GStreamer elements — no C++ required |
| **Multi-stream, multi-sensor** | Mux/demux dozens of RTSP streams, LiDAR frames, and radar point clouds in one process |
| **Geti™ & ONNX support** | Deploy models from Intel Geti™ Studio or any ONNX/OpenVINO IR model directly |

---

## ⚡ Quick Start

### Option A — Docker (recommended, zero setup)

```bash
docker run -it --rm \
  --device /dev/dri \
  -e DISPLAY=$DISPLAY \
  -v /tmp/.X11-unix:/tmp/.X11-unix \
  intel/dlstreamer:latest
```

> Inside the container, all samples and tools are available under `/opt/intel/dlstreamer/samples`.

### Option B — Native install (Ubuntu)

**Step 1 — Install GPU/NPU drivers** (one-time, detects your hardware automatically):

```bash
wget https://raw.githubusercontent.com/open-edge-platform/dlstreamer/main/scripts/DLS_install_prerequisites.sh
chmod +x DLS_install_prerequisites.sh
./DLS_install_prerequisites.sh
```

> This script detects your Intel GPU/NPU, installs the correct drivers for Ubuntu 22.04 or 24.04, and adds your user to the required groups. Use `--reinstall-npu-driver=yes` to force-reinstall the NPU driver. Run `./DLS_install_prerequisites.sh --help` for all options.

**Step 2 — Install DL Streamer**:

```bash
wget -qO - https://apt.repos.intel.com/intel-gpg-keys/GPG-PUB-KEY-INTEL-SW-PRODUCTS.PUB | sudo apt-key add -
echo "deb https://apt.repos.intel.com/openvino/2025 ubuntu24 main" | sudo tee /etc/apt/sources.list.d/intel-openvino-2025.list
sudo apt update && sudo apt install -y intel-dlstreamer
```

Full installation guide: [Install Guide for Ubuntu](./docs/user-guide/get_started/install/install_guide_ubuntu.md) | [Windows](./docs/user-guide/get_started/install/install_guide_index.md)

---

## Your First Pipeline

Detect objects in a video file in a single command:

```bash
# Download a model
cd /opt/intel/dlstreamer/samples
./download_public_models.sh yolo11n coco128

# Run the pipeline
gst-launch-1.0 \
  filesrc location=my_video.mp4 ! \
  decodebin3 ! \
  gvadetect model=models/yolo11n/FP32/yolo11n.xml device=GPU ! \
  queue ! \
  gvawatermark ! \
  autovideosink
```

That's it. Change `device=GPU` to `device=CPU` or `device=NPU` — no other code changes needed.

### Python API

```python
import gi
gi.require_version("Gst", "1.0")
from gi.repository import Gst

Gst.init(None)

pipeline = Gst.parse_launch("""
    filesrc location=my_video.mp4 !
    decodebin3 !
    gvadetect model=models/yolo11n/FP32/yolo11n.xml device=GPU !
    queue !
    gvawatermark !
    autovideosink
""")

pipeline.set_state(Gst.State.PLAYING)
bus = pipeline.get_bus()
bus.timed_pop_filtered(Gst.CLOCK_TIME_NONE, Gst.MessageType.EOS | Gst.MessageType.ERROR)
pipeline.set_state(Gst.State.NULL)
```

---

## GStreamer Elements

DL Streamer provides purpose-built GStreamer elements for every stage of a media analytics pipeline:

### Inference

| Element | Purpose |
|---|---|
| [`gvadetect`](./docs/user-guide/elements/gvadetect.md) | Object detection (YOLO, SSD, EfficientDet, …) |
| [`gvaclassify`](./docs/user-guide/elements/gvaclassify.md) | Object classification, segmentation, pose estimation |
| [`gvainference`](./docs/user-guide/elements/gvainference.md) | Raw inference output (any model) |
| [`gvagenai`](./docs/user-guide/elements/gvagenai.md) | Vision-Language / GenAI models |
| [`gvaaudiotranscribe`](./docs/user-guide/elements/gvaaudiotranscribe.md) | Audio transcription (Whisper) |

### Analytics & Routing

| Element | Purpose |
|---|---|
| [`gvatrack`](./docs/user-guide/elements/gvatrack.md) | Zero-term or short-term object tracking |
| [`gvaanalytics`](./docs/user-guide/elements/gvaanalytics.md) | Tripwires, zones, trajectory analytics |
| [`gvastreammux`](./docs/user-guide/elements/gvastreammux.md) / [`gvastreamdemux`](./docs/user-guide/elements/gvastreamdemux.md) | Multi-stream mux/demux |
| [`gvamotiondetect`](./docs/user-guide/elements/gvamotiondetect.md) | Lightweight motion detection (VA-API accelerated) |

### Output & Visualization

| Element | Purpose |
|---|---|
| [`gvawatermark`](./docs/user-guide/elements/gvawatermark.md) | Overlay bounding boxes, labels, and custom drawings |
| [`gvametaconvert`](./docs/user-guide/elements/gvametaconvert.md) | Convert inference metadata to JSON |
| [`gvametapublish`](./docs/user-guide/elements/gvametapublish.md) | Publish JSON to MQTT, Kafka, or file |
| [`gvafpscounter`](./docs/user-guide/elements/gvafpscounter.md) | Per-stream and aggregate FPS measurement |

### 3D / Sensor Fusion

| Element | Purpose |
|---|---|
| [`g3dlidarparse`](./docs/user-guide/elements/g3dlidarparse.md) | LiDAR point cloud parsing (BIN/PCD) |
| [`g3dinference`](./docs/user-guide/elements/g3dinference.md) | PointPillars 3D object detection |
| [`g3dradarprocess`](./docs/user-guide/elements/g3dradarprocess.md) | mmWave radar signal processing |

[View all elements →](./docs/user-guide/elements/elements.md)

---

## Samples

30+ samples across Python, C++, and `gst-launch` command lines:

| Category | Samples |
|---|---|
| **Detection** | [YOLO detection](./samples/gstreamer/gst_launch/detection_with_yolo/README.md), [Face detection + classification](./samples/gstreamer/gst_launch/face_detection_and_classification/README.md), [Depth estimation](./samples/gstreamer/gst_launch/depth_estimation/README.md) |
| **Segmentation & Pose** | [Instance segmentation](./samples/gstreamer/gst_launch/instance_segmentation/README.md), [Human pose estimation](./samples/gstreamer/gst_launch/human_pose_estimation/README.md) |
| **Tracking** | [Vehicle & pedestrian tracking](./samples/gstreamer/gst_launch/vehicle_pedestrian_tracking/README.md), [Vehicle counter with tripwires](./samples/gstreamer/python/gvaanalytics_tripwire/README.md) |
| **VLM / GenAI** | [VLM video summarization](./samples/gstreamer/gst_launch/gvagenai/README.md), [VLM alerts](./samples/gstreamer/python/vlm_alerts/README.md), [VLM self-checkout](./samples/gstreamer/python/vlm_self_checkout/README.md) |
| **Multi-stream** | [Multi-camera deployment](./samples/gstreamer/gst_launch/multi_stream/README.md), [Stream mux/demux](./samples/gstreamer/gst_launch/stream_mux_and_demux/README.md) |
| **3D Sensors** | [LiDAR parsing](./samples/gstreamer/gst_launch/g3dlidarparse/README.md), [PointPillars 3D detection](./samples/gstreamer/gst_launch/g3dinference/README.md), [Radar processing](./samples/gstreamer/gst_launch/g3dradarprocess/README.md) |
| **Integration** | [ONVIF camera discovery](./samples/gstreamer/python/onvif_cameras_discovery/README.md), [Geti™ model deployment](./samples/gstreamer/gst_launch/geti_deployment/README.md), [Metadata to MQTT/Kafka](./samples/gstreamer/gst_launch/metapublish/README.md) |
| **Python extensibility** | [Custom Python GStreamer elements](./samples/gstreamer/gst_launch/python-elements/face_detection_and_classification/README.md), [Smart NVR with recording](./samples/gstreamer/python/smart_nvr/README.md) |

[Browse all samples →](./samples/gstreamer/README.md)

---

## Supported Platforms

| Hardware | CPU | GPU | NPU |
|---|:---:|:---:|:---:|
| Intel Core Ultra (Panther Lake) | ✅ | ✅ | ✅ |
| Intel Core Ultra (Arrow Lake) | ✅ | ✅ | ✅ |
| Intel Core Ultra (Lunar Lake) | ✅ | ✅ | ✅ |
| Intel Core Ultra (Meteor Lake) | ✅ | ✅ | ✅ |
| Intel Arc discrete GPU (Alchemist, Battlemage) | ✅ | ✅ | — |
| 11th–13th Gen Intel Core | ✅ | ✅ | — |

Operating systems: **Ubuntu 22.04 / 24.04**, **Windows 11** (Arrow Lake+).

[Full system requirements →](./docs/user-guide/get_started/system_requirements.md)

---

## Supported Models

DL Streamer runs models in **OpenVINO™ IR** and **ONNX** formats:

- **Detection:** YOLO (v5–v11, YOLO26, YOLOX, YOLOE), SSD, EfficientDet, FasterRCNN
- **Classification:** MobileNet, ResNet, EfficientNet
- **Segmentation:** Instance segmentation, semantic segmentation
- **Pose estimation:** Human pose (OpenPose, HigherHRNet)
- **VLMs:** MiniCPM-V, CLIP, Whisper
- **3D:** PointPillars (LiDAR), mmWave radar models
- **Geti™ models:** Anomaly detection, object detection, classification

[Full list of supported models →](./docs/user-guide/supported_models.md)

---

## Documentation

| Resource | Link |
|---|---|
| Get Started (tutorial + install) | [docs/user-guide/get_started](./docs/user-guide/get_started/get_started_index.md) |
| Developer Guide | [docs/user-guide/dev_guide](./docs/user-guide/dev_guide/dev_guide_index.md) |
| Elements Reference | [docs/user-guide/elements](./docs/user-guide/elements/elements.md) |
| API Reference | [docs/user-guide/api_ref](./docs/user-guide/api_ref/api_reference.rst) |
| Metadata Guide | [docs/user-guide/dev_guide/metadata](./docs/user-guide/dev_guide/metadata.md) |

---

## Contributing

We welcome contributions! Please read [CONTRIBUTING.md](./CONTRIBUTING.md) and follow the [Code Style Guide](./CODESTYLE.md).

For security issues, see [SECURITY.md](./SECURITY.md).

---

## License

DL Streamer is licensed under the [MIT License](./LICENSE).

---

<div align="center">

*Intel, the Intel logo, OpenVINO, Intel Core, Intel Arc, and Intel Iris are trademarks of Intel Corporation or its subsidiaries.*
*GStreamer is a trademark of the GStreamer project.*

</div>
