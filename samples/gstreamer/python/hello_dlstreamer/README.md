# Hello DL Streamer

This sample demonstrates how to build a Python application that constructs and executes a DL Streamer pipeline.

> filesrc -> decodebin3 -> gvadetect -> gvawatermark -> autovideosink

The pipeline stages implement the following functions:

* __filesrc__ - reads video from a local file
* __decodebin3__ - decodes video into individual frames
* __gvadetect__ - runs AI inference for object detection on each frame
* __gvawatermark__ - overlays object bounding boxes on frames
* __autovideosink__ - renders the video stream on the display

The sample inserts also `queue` and `videoconvert` elements to adapt interfaces between stages. The resulting behavior is similar to [hello_dlstreamer.sh](../../scripts/hello_dlstreamer.sh).

## How It Works

### STEP 1 - Pipeline Construction

The application creates a GStreamer `pipeline` object using one of two methods:

* [OPTION A](./hello_dlstreamer.py): Use `gst_parse_launch` to construct the pipeline from a string (default method).
    ```code
    pipeline = Gst.parse_launch(...)
    ```

* [OPTION B](./hello_dlstreamer.py): Use GStreamer API calls to create, configure, and link individual elements.
    ```code
    element = Gst.ElementFactory.make(...)
    element.set_property(...)
    pipeline.add(element)
    element.link(next_element)
    ```

Both methods produce equivalent pipelines.

### STEP 2 - Adding Custom Probe

The application registers a custom callback on the sink pad of the `gvawatermark` element. The callback is invoked on each buffer pushed to the pad.

```code
watermarksinkpad = watermark.get_static_pad("sink")
watermarksinkpad.add_probe(watermark_sink_pad_buffer_probe, ...)
```

The callback inspects `GstAnalytics` metadata from `gvadetect`, counts detected objects by category, and attaches a classification string to the frame.

### STEP 3 - Pipeline Execution

The application sets the pipeline to `PLAYING` state and processes messages until the input completes.

```code
pipeline.set_state(Gst.State.PLAYING)
terminate = False
while not terminate:
    msg = bus.timed_pop_filtered(...)
    ... set terminate=TRUE on end-of-stream message
pipeline.set_state(Gst.State.NULL)
```

## Running

The sample requires a video file and an object detection model. Download sample assets with:

```sh
cd <python/hello_dlstreamer directory>
export MODELS_PATH=${PWD}
wget https://videos.pexels.com/video-files/1192116/1192116-sd_640_360_30fps.mp4
../../../download_public_models.sh yolo11n coco128
```
> **Note:** This may take several seconds depending on your network speed.

Run the application:

```sh
python3 ./hello_dlstreamer.py 1192116-sd_640_360_30fps.mp4 public/yolo11n/INT8/yolo11n.xml
```

The sample displays the video stream with object detection annotations (bounding boxes and class labels).


## See also
* [Samples overview](../../README.md)
