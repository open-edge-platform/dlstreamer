# GVA Audio Transcribe Element

## Table of Contents

1. [Overview](#overview)
   - [Model Type Support](#model-type-support)
2. [Install DL Streamer](#install-dl-streamer)
3. [Model Preparation](#model-preparation)
4. [Demo Script Execution](#demo-script-execution)
   - [WAV File Transcription](#launch-on-a-test-wav-file)
   - [Live Microphone Transcription](#launch-using-the-microphone)
   - [Video Audio Transcription](#launch-using-video-demux)
5. [Audio Transcription Pipeline Using GVA Elements](#audio-transcription-pipeline-using-gvametaconvert-and-gvametapublish)
   - [Properties](#properties)
---

## Overview

This element provides audio transcription capabilities with an extensible handler interface. Currently supports:

- **Whisper models** (primary support) - OpenVINO GenAI backend

### Model Type Support

- `whisper` - Fully supported (OpenVINO GenAI)

## Install DL Streamer

Please follow official DL Streamer documentation for [installation steps](https://docs.openedgeplatform.intel.com/2026.1/edge-ai-libraries/dl-streamer/dev_guide/advanced_install/advanced_install_guide_compilation.html).

> **Note:** This element requires OpenVINO GenAI.

## Model Preparation

These steps are adapted from the original `whisper_speech_recognition` sample in OpenVINO GenAI [README](https://github.com/openvinotoolkit/openvino.genai/blob/releases/2025/3/samples/cpp/whisper_speech_recognition/README.md).

```bash
cd ~/edge-ai-libraries/libraries/dl-streamer/samples/gstreamer/gst_launch/audio_transcribe
./model_preparation.sh
```

## Demo Script Execution:

### Go to samples

```bash
cd ~/edge-ai-libraries/libraries/dl-streamer/samples/gstreamer/gst_launch/audio_transcribe
```

### Launch on a test WAV file:

```bash
 ./audio_transcribe.sh --input-source=${HOME}/<filename.wav> --models-path=${HOME}/path/to/model-directory/ --device=CPU --mode=video
```

### Launch using the microphone:

```bash
./audio_transcribe.sh --models-path=${HOME}/path/to/model-directory/ --device=CPU --mode=live

```

### Launch using video demux:

```bash
./audio_transcribe.sh --input-source=${HOME}/<filename.mp4> --models-path=${HOME}/path/to/model-directory/ --device=CPU --mode=video
```

## Audio Transcription Pipeline Using `gvametaconvert` and `gvametapublish`

```bash
#(Optional) if you want info logs export GST_DEBUG
export GST_DEBUG=gvaaudiotranscribe:4

gst-launch-1.0  filesrc location=${HOME}/<filename>.mp4 ! qtdemux name=demux demux.audio_0 ! decodebin ! audioconvert ! audioresample ! audio/x-raw,channels=1,format=S16LE,rate=16000 ! audiomixer output-buffer-duration=100000000 ! gvaaudiotranscribe model=${HOME}/path/to/whisper-model-dir device=CPU model_type=whisper  ! gvametaconvert format=json ! gvametapublish method=file file-path=transcriptions.json ! fakesink
```

### Properties

- `model` - Path to model (directory for Whisper, custom path for other models)
- `model_type` - Model type: `whisper` (supported), or your custom type
- `device` - Inference device: `CPU`, `GPU`
