# Prompt-based Object Detection

This sample searches a video file for user-defined objects using an open-vocabulary detection model.
It also demonstrates how to integrate a third-party model with a DLStreamer pipeline.

The pipeline stages implement the following functions:

* __filesrc__ — reads video stream from a local file
* __decodebin3__ — decodes video stream into individual frames
* __gvadetect__ — runs an open-vocabulary AI detection model for each frame
* __appsink / gvametapublish__ — delivers results to a user-defined callback or writes JSON metadata

## Prerequisites

### Install DLStreamer

#### Option A: Docker image (recommended)

Pull the latest DLStreamer image and start an interactive container with GPU access:

```sh
docker pull intel/dlstreamer:latest
docker run --init -it --rm \
    --device /dev/dri \
    --group-add $(stat -c "%g" /dev/dri/render*) \
    intel/dlstreamer:latest
cd /opt/intel/dlstreamer/samples/gstreamer/python/prompted_detection
```

> Note: install Docker Engine if not already available (see [Docker installation guide](https://docs.docker.com/engine/install/)).
> All subsequent commands run inside this container shell.

#### Option B: Native installation

Install DLStreamer on the host (see [DLStreamer Installation Guide](../../../../docs/user-guide/install/install_guide_index.md)).

```sh
cd samples/gstreamer/python/prompted_detection
```

### Download Video

Download example video file:

```sh
curl -L -o 1192116-sd_640_360_30fps.mp4 \
    "https://videos.pexels.com/video-files/1192116/1192116-sd_640_360_30fps.mp4"
```

### Prepare Model

This sample expects `yoloe-26s-seg` in FP16 precision from Ultralytics for open-vocabulary object detection.

Use the [Ultralytics model conversion](../../../../scripts/download_models/README.md#2-ultralytics-conversion) to prepare the model.

## Run Sample Application

```sh
python3 prompted_detection.py \
    --input 1192116-sd_640_360_30fps.mp4 \
    --prompt "white car" \
    --model "${MODELS_PATH}/public/yoloe-26s-seg/FP16/yoloe-26s-seg.xml"
```

If `--model` is omitted, it defaults to `$MODELS_PATH/public/yoloe-26s-seg/FP16/yoloe-26s-seg.xml`.

> **Note:** Replace the `--model` path with the actual location where you downloaded the model in the [Prepare Model](#prepare-model) step.

Run `python3 prompted_detection.py --help` to see all available options.

### Output Modes

Control the output with `--output` (default: `appsink`):

| Mode | Description |
|---|---|
| `appsink` | Detection results processed in a user-defined callback and printed to the terminal |
| `json` | Writes detection metadata as JSON Lines to `output.json` via `gvametapublish` |
| `file` | Annotates detected objects with `gvawatermark` and encodes result to `<input_stem>_output.mp4` (requires VA-API) |

**Appsink (default):**
```sh
python3 prompted_detection.py --input video.mp4 --prompt "white car"
```

**JSON metadata:**
```sh
python3 prompted_detection.py --input video.mp4 --prompt "white car" --output json
```
Outputs `output.json` in JSON Lines format, one record per frame.

**File output:**
```sh
python3 prompted_detection.py --input video.mp4 --prompt "white car" --output file
```

## How It Works

### DLStreamer Pipeline Construction

The application creates a GStreamer `pipeline` object configured with the pre-exported detection model and input video file.

```code
pipeline = Gst.parse_launch(
        f"filesrc location={video_file} ! decodebin3 ! "
        f"gvadetect model={model_file} device={device} batch-size=4 ! queue ! "
        f"{sink}"  # sink depends on --output mode
    )
```

A user-defined callback processes detection results in `appsink` mode:

```code
appsink = pipeline.get_by_name("appsink0")
appsink.connect("new-sample", on_new_sample, None)
```

The `on_new_sample` callback prints frame timestamps when the requested object is detected.

## See also
* [Samples overview](../../../README.md)
