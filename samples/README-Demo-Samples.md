# DL Streamer Demo Samples

## Install GPU/NPU drivers
Run Step 1 of Quick Start Installation https://github.com/open-edge-platform/dlstreamer#step-1--install-gpunpu-drivers-required-for-docker-and-native-install

## Install DL Streamer (Ubuntu 24.04, via apt)

```bash
# Add Intel package repositories
sudo -E wget -O- https://apt.repos.intel.com/intel-gpg-keys/GPG-PUB-KEY-INTEL-SW-PRODUCTS.PUB | gpg --dearmor | sudo tee /usr/share/keyrings/intel-gpg-archive-keyring.gpg > /dev/null
sudo -E wget -O- https://apt.repos.intel.com/edgeai/dlstreamer/GPG-PUB-KEY-INTEL-DLS.gpg | sudo tee /usr/share/keyrings/dls-archive-keyring.gpg > /dev/null
echo "deb [signed-by=/usr/share/keyrings/dls-archive-keyring.gpg] https://apt.repos.intel.com/edgeai/dlstreamer/ubuntu24 ubuntu24 main" | sudo tee /etc/apt/sources.list.d/intel-dlstreamer.list
sudo bash -c 'echo "deb [signed-by=/usr/share/keyrings/intel-gpg-archive-keyring.gpg] https://apt.repos.intel.com/openvino ubuntu24 main" | sudo tee /etc/apt/sources.list.d/intel-openvino.list'

# Install
sudo apt update && sudo apt-get install -y intel-dlstreamer
```

## Download a YOLO Model

```bash
python3 -m venv .dls-venv && source .dls-venv/bin/activate
pip install openvino==2026.1.0 nncf==3.0.0 ultralytics==8.4.3
python3 /opt/intel/dlstreamer/scripts/download_models/download_ultralytics_models.py \
  --model yolo11s.pt \
  --outdir ~/models/yolo11s \
  --int8
```

## Set Up Environment

```bash
source /opt/intel/dlstreamer/scripts/setup_dls_env.sh
export MODELS_PATH=~/models
```

## Switch to User folder
```bash
cd ~
```

## Create videos folder
```bash
mkdir ~/videos
```

## Run YOLO Detection Sample
Demonstrates object detection pipelines creation via `gst-launch-1.0` using the latest YOLO models.
Check [Readme](gstreamer/gst_launch/detection_with_yolo/README.md) for more details.

**Download video file to ~/videos folder**
```bash
curl -L https://videos.pexels.com/video-files/1192116/1192116-sd_640_360_30fps.mp4 --output ~/videos/video1.mp4
```

**Examples:**

*Run yolo11s on GPU with display output*
```bash
/opt/intel/dlstreamer/samples/gstreamer/gst_launch/detection_with_yolo/yolo_detect.sh yolo11s GPU ~/videos/video1.mp4 display
```

*Run yolo11s on NPU with display output*
```bash
/opt/intel/dlstreamer/samples/gstreamer/gst_launch/detection_with_yolo/yolo_detect.sh yolo11s NPU ~/videos/video1.mp4 display
```
