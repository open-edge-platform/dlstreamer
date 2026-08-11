# Windows Samples

This folder contains Windows-specific samples and scripts for Deep Learning Streamer (DL Streamer).

## Requirements

- Python 3.12 or Python 3.11 (Python 3.13 is not yet supported by openvino-dev)
- pip install openvino-dev[onnx]

## Download Models

Before running samples, prepare required models using scripts from `scripts/download_models`.

```batch
set MODELS_PATH=C:\path\to\models
cd ..\..\scripts\download_models
REM See README.md here for per-script venv setup and model-specific commands
```

## Notes

- These samples are specifically designed for Windows systems
- Linux equivalents of these samples can be found in the main `samples/gstreamer` folder
