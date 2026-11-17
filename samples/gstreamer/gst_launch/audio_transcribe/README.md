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

Please follow official DL Streamer documentation for [installation steps](https://docs.openedgeplatform.intel.com/dev/edge-ai-libraries/dl-streamer/dev_guide/advanced_install/advanced_install_guide_compilation.html).

> **Note:** This element requires OpenVINO GenAI.

## Model Preparation

Before running this sample, prepare any Whisper model compatible with OpenVINO GenAI using the general [`download_hf_models.py`](../../../../scripts/download_models/download_hf_models.py) helper. For example, `openai/whisper-tiny` is supported; other compatible Whisper models may also be used. Follow the [model download README](../../../../scripts/download_models/README.md#1-hugging-face-conversion) to set up and use the helper, then pass the resulting model directory to `--models-path`.

## Demo Script Execution:

### Go to samples

```bash
cd ~/edge-ai-libraries/libraries/dl-streamer/samples/gstreamer/gst_launch/audio_transcribe
```

### Launch on a test WAV file:

```bash
 ./audio_transcribe.sh --input-source=${HOME}/<filename.wav> --models-path=/path/to/whisper-model --device=CPU --mode=wav
```

### Launch using the microphone:

```bash
./audio_transcribe.sh --models-path=/path/to/whisper-model --device=CPU --mode=live

```

### Launch using video demux:

```bash
./audio_transcribe.sh --input-source=${HOME}/<filename.mp4> --models-path=/path/to/whisper-model --device=CPU --mode=video
```

## Audio Transcription Pipeline Using `gvametaconvert` and `gvametapublish`

```bash
#(Optional) if you want info logs export GST_DEBUG
export GST_DEBUG=gvaaudiotranscribe:4

gst-launch-1.0  filesrc location=${HOME}/<filename>.mp4 ! qtdemux name=demux demux.audio_0 ! decodebin ! audioconvert ! audioresample ! audio/x-raw,channels=1,format=S16LE,rate=16000 ! audiomixer output-buffer-duration=100000000 ! gvaaudiotranscribe model=/path/to/whisper-model device=CPU model_type=whisper  ! gvametaconvert format=json ! gvametapublish method=file file-path=transcriptions.json ! fakesink
```

### Properties

- `model` - Path to model (directory for Whisper, custom path for other models)
- `model_type` - Model type: `whisper` (supported), or your custom type
- `device` - Inference device: `CPU`, `GPU`
