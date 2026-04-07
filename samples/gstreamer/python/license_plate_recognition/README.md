# License Plate Recognition (Python)

This sample demonstrates a License Plate Recognition (LPR) pipeline built in Python using DLStreamer. It detects license plates with a YOLOv8 detector and reads plate text with PaddleOCR — all running inside a single GStreamer pipeline.

## How It Works

The script performs three stages, each marked with comments in the source code:

**Model download** — The `yolov8_license_plate_detector` (YOLOv8-based) and `ch_PP-OCRv4_rec_infer` (PaddleOCR) models are downloaded from [edge-ai-resources](https://github.com/open-edge-platform/edge-ai-resources) and cached under `models/`.

**Pipeline construction** — A GStreamer pipeline is built using `Gst.parse_launch()`:

```mermaid
graph LR
    A[source] --> B[decodebin3]
    B --> C[gvadetect<br/>license plate detector]
    C --> D[gvaclassify<br/>OCR]
    D --> E[gvawatermark]
    E --> F[output sink]
```

**Pad probe** — An optional pad probe on `gvawatermark` prints recognized plate text to stdout in real time.

## Models

| Purpose | Model | Source |
|---------|-------|--------|
| License plate detection | `yolov8_license_plate_detector` | [edge-ai-resources](https://github.com/open-edge-platform/edge-ai-resources/raw/main/models/license-plate-reader.zip) |
| Optical character recognition | `ch_PP-OCRv4_rec_infer` | [edge-ai-resources](https://github.com/open-edge-platform/edge-ai-resources/raw/main/models/license-plate-reader.zip) |

Both models are in OpenVINO™ IR format and are downloaded automatically on first run.

## Prerequisites

Source the DLStreamer environment before running the sample:

```bash
source /opt/intel/dlstreamer/scripts/setup_dls_env.sh
```

## Reproducible Setup

### Install

1. Source the DLStreamer environment:
```bash
source /opt/intel/dlstreamer/scripts/setup_dls_env.sh
```

2. Create and activate a virtual environment:
```bash
python3 -m venv .lpr_venv
source .lpr_venv/bin/activate
```

3. Install dependencies:
```bash
pip install -r requirements.txt
```

## Running

```bash
# Run with the default video (downloaded automatically)
python3 license_plate_recognition.py

# Provide a local video
python3 license_plate_recognition.py --video-path /path/to/video.mp4

# Change inference device
python3 license_plate_recognition.py --device CPU

# Output modes: display, fps, json, display-and-json, file
python3 license_plate_recognition.py --output json
python3 license_plate_recognition.py --output file
```

### Command-Line Options

| Option | Default | Description |
|--------|---------|-------------|
| `--video-path` | *(none)* | Path to a local video file |
| `--video-url` | Pexels parking video | URL to download if `--video-path` is not set |
| `--device` | `GPU` | Inference device: `CPU`, `GPU`, or `AUTO` |
| `--output` | `display-and-json` | Output mode: `display`, `fps`, `json`, `display-and-json`, `file` |

### Output Modes

| Mode | Description |
|------|-------------|
| `display` | Render annotated video to screen |
| `fps` | Print FPS to stdout (no display) |
| `json` | Write JSONL metadata to `results/output.jsonl` |
| `display-and-json` | Display + write JSONL |
| `file` | Encode annotated video to `results/output_lpr.mp4` |

## Sample Output

When running with `--output display-and-json`, the console shows detected plates:

```
[  2.50s] Plates: AB1234CD
[  3.00s] Plates: EF5678GH, IJ9012KL
Pipeline complete.
```

And `results/output.jsonl` contains per-frame inference metadata in JSON-lines format.

## See Also

* [Command-line LPR sample](../../gst_launch/license_plate_recognition/)
* [Samples overview](../../README.md)
