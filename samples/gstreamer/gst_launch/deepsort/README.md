# Deep SORT Object Tracking Sample

This sample demonstrates multi-object tracking using the Deep SORT algorithm with DL Streamer's `gvatrack` element.

## Pipeline

```
filesrc ! decodebin3 ! gvadetect (YOLO) ! gvainference (mars-small128) ! gvatrack (deep-sort) ! sink
```

1. [gvadetect](../../../../docs/user-guide/elements/gvadetect.md) — Detects persons using YOLO 
2. [gvainference](../../../../docs/user-guide/elements/gvainference.md) — Extracts 128-dimensional re-ID appearance features per detected person using mars-small128
3. [gvatrack](../../../../docs/user-guide/elements/gvatrack.md) — Deep SORT tracker assigns persistent IDs using appearance matching, Mahalanobis-gated Kalman filtering, and IoU fallback

## Prerequisites

Download both models using the provided download script:

```bash
export MODELS_PATH=/path/to/models

# Download YOLOv8s detection model and mars-small128 re-ID model
cd samples
./download_public_models.sh yolov8s,mars-small128
```

This downloads YOLOv8s and converts the mars-small128 re-ID model to OpenVINO IR format. The models will be placed under `$MODELS_PATH/public/`:
- `$MODELS_PATH/public/yolov8s/FP32/yolov8s.xml`
- `$MODELS_PATH/public/mars-small128/mars_small128_fp32.xml`

## Usage

```bash
./gvatrack_deepsort.sh --det-model /path/to/yolov8s.xml --reid-model /path/to/mars_small128_fp32.xml
```

### Command-Line Arguments

| Argument | Default | Description |
|----------|---------|-------------|
| `--det-model DET_MODEL` | (required) | Detection model path, YOLO |
| `--reid-model REID_MODEL` | (required) | Re-ID model path, mars-small128 |
| `--device DEVICE` | `CPU` | Device to use for gvadetect and gvainference elements. Allowed: CPU, GPU, NPU |
| `--input INPUT` | Pexels video URL | Input video file or URL |
| `--output OUTPUT` | `file` | Output type. Allowed: display, file, fps, json |
| `--output-file FILE` | `deepsort_output.mp4` | Output file path/name when `--output file` is used |
| `--deepsort-cfg CFG` | See table below | Deep SORT tracker config (comma-separated key=value pairs) |

### Deep SORT Tracker Parameters (deepsort-trck-cfg)

| Parameter | Default | Description |
|-----------|---------|-------------|
| `max_age` | 60 | Max frames a track survives without a matched detection |
| `max_cosine_distance` | 0.2 | Appearance similarity threshold for matching |
| `nn_budget` | 0 | Max features stored per track (0 = unlimited) |
| `object_class` | person | Only track detections with this label |
| `reid_max_age` | 30 | Frames to keep re-ID gallery features after track deletion |

### Examples

```bash
# Run on GPU with local input video
./gvatrack_deepsort.sh --det-model /path/to/yolov8s.xml --reid-model /path/to/mars_small128_fp32.xml \
  --device GPU --input video.mp4

# Save output to a specific file, local input video 
./gvatrack_deepsort.sh --det-model /path/to/yolov8s.xml --reid-model /path/to/mars_small128_fp32.xml \
  --output file --output-file ./my_tracking_result.mp4 --input video.mp4

# Custom Deep SORT parameters
./gvatrack_deepsort.sh --det-model /path/to/yolov8s.xml --reid-model /path/to/mars_small128_fp32.xml \
  --deepsort-cfg 'max_age=120,max_cosine_distance=0.5,object_class=person'

# Direct gst-launch-1.0 command with local input video
gst-launch-1.0 \
  filesrc location=video.mp4 ! decodebin3 ! \
  gvadetect model=/path/to/yolov8s.xml device=CPU pre-proc-backend=opencv ! queue ! \
  gvainference model=/path/to/mars_small128_fp32.xml device=CPU \
    inference-region=roi-list object-class=person ! \
  gvatrack tracking-type=deep-sort \
    deepsort-trck-cfg="max_age=60,max_cosine_distance=0.2,nn_budget=0,object_class=person,reid_max_age=30" ! queue ! \
  gvawatermark displ-cfg=show-roi=person,font-scale=0.8 ! \
  gvafpscounter ! fakesink sync=false
```

## See also

* [Samples overview](../../README.md)
* [Object Tracking Developer Guide](../../../../docs/user-guide/dev_guide/object_tracking.md)
