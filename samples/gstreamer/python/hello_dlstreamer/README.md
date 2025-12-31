# Hello DL Streamer

This sample demonstrates how to build a Python application that constructs and executes a DL Streamer pipeline.

> filesrc -> decodebin3 -> gvadetect -> gvawatermark -> autovideosink

The individual pipeline stages implement the following functions:

* __filesrc__ element reads video stream from a local file
* __decodebin3__ element decodes video stream into individual frames
* __gvadetect__ element runs AI inference object detection for each frame
* __gvawatermark__ element draws (overlays) object bounding boxes on top of analyzed frames
* __autovideosink__ element renders video stream on local display

In addition, the sample uses 'queue' and 'videoconvert' elements to adapt interface between functional stages. The resulting behavior is similar to [hello_dlstreamer.sh](../../scripts/hello_dlstreamer.sh) using command line.

## How It Works

### STEP 1 - Pipeline Construction

First, the application creates a GStreamer `pipeline` object.
The sample code demonstrates two methods for pipeline creation:

* OPTION A: Use `gst_parse_launch` method to construct the pipeline from a string representation. This is the default method. It uses a single API call to create a set of elements and links them together into a pipeline.
    ```code
    pipeline = Gst.parse_launch(...)
    ```

* OPTION B: Use a sequence of GStreamer API calls to create individual elements, configure their properties and link together to form a pipeline. This method allows fine-grained control over pipeline elements.
    ```code
    element = Gst.ElementFactory.make(...)
    element.set_property(...)
    pipeline.add(element)
    element.link(next_element)
    ```
Both methods are equivalent and produce same output pipeline.

### STEP 2 - Adding custom probe

The application registers a custom callback (GStreamer `probe`) on the sink pad of `gvawatermark` element. The GStreamer pipeline will invoke the callback function on each buffer pushed to the sink pad.

```code
watermarksinkpad = watermark.get_static_pad("sink")
watermarksinkpad.add_probe(watermark_sink_pad_buffer_probe, ...)
```
In this example, the callback function inspects `GstAnalytics` metadata produced by the `gvadetect` element. The callback counts the number of detected objects in each category, and attaches a custom classification string to the processed frame.

### STEP 3 - Pipeline execution

The last step is to run the pipeline. The application sets the pipeline state to `PLAYING` and implements the message processing loop. Once the input video file is fully replayed, the `filesrc` element will send end-of-stream message.
```code
pipeline.set_state(Gst.State.PLAYING)
terminate = False
while not terminate:
    msg = bus.timed_pop_filtered(...)
    ... set terminate=TRUE on end-of-stream message
pipeline.set_state(Gst.State.NULL)
```

## Running

The sample application requires two local files with input video and an object detection model. Here is an example command line to download sample assets.
Please note the model download step may take up to several minutes as it includes model quantization to INT8.

```sh
cd <python/hello_dlstreamer directory>
export MODELS_PATH=${PWD}
wget https://videos.pexels.com/video-files/1192116/1192116-sd_640_360_30fps.mp4
../../../download_public_models.sh yolo11n coco128
```
Once assets are downloaded to the local disk, the sample application can be started as any other regular python application.

```sh
python3 ./hello_dlstreamer.py 1192116-sd_640_360_30fps.mp4 public/yolo11n/INT8/yolo11n.xml
```
The sample opens a window and renders a video stream along with object detection annotations - bounding boxes and object classes.

## See also
* [Samples overview](../../README.md)
