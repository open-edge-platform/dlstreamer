# Tutorial

In this tutorial, you will learn how to build video analytics pipelines
using Deep Learning Streamer Pipeline
Framework.

- [About GStreamer](#about-gstreamer)
- [Introduction to Deep Learning Streamer Pipeline Framework](#introduction-to-intel-deep-learning-streamer-intel-dl-streamer-pipeline-framework)
- [Non-Docker tutorial setup](#non-docker-tutorial-setup)
- [Docker tutorial setup](#docker-tutorial-setup)
- [Exercise 1 Build object detection pipeline](#exercise-1---build-object-detection-pipeline)
- [Exercise 2 - Build object classification pipeline](#exercise-2-build-object-classification-pipeline-object-classification)
- [Exercise 3 - Use object tracking to improve performance](#exercise-3-use-object-tracking-to-improve-performance-object-tracking)
- [Exercise 4 - Publish the inference results to a ".json" file](#exercise-4-publish-inference-results)

## About GStreamer

In this section we introduce basic GStreamer\* concepts that you will
use in the rest of the tutorial. If you are already familiar with
GStreamer feel free to skip ahead to the next section -
[Introduction to Deep Learning Streamer Pipeline Framework](#introduction-to-deep-learning-streamer-pipeline-framework).

[GStreamer](https://gstreamer.freedesktop.org/) is a flexible, fast,
multi-platform open-source multimedia framework. It has an easy to use
command line tool for running pipelines, as well as an API with bindings
in C\*, Python\*, JavaScript\* and more. In this tutorial we will use
the GStreamer command line tool **gst-launch-1.0**. For more information and
examples please refer to the online documentation for
[gst-launch-1.0](https://gstreamer.freedesktop.org/documentation/tools/gst-launch.html?gi-language=c).

### GStreamer Library Pipelines

The command line tool **gst-launch-1.0** enables developers to describe
a media analytics pipeline as a series of connected elements. The list
of elements, their configuration properties, and their connections are
all specified as a list of strings separated by exclamation marks (`!`).
**gst-launch-1.0** parses the string and instantiates the software
modules that perform the individual media analytics operations.
Internally, the GStreamer library constructs a pipeline object that
contains the individual elements and handles common operations such as
clocking, messaging, and state changes.

Example with test video input:

```bash
gst-launch-1.0 videotestsrc ! ximagesink
```

### GStreamer Library Elements

An
[element](https://gstreamer.freedesktop.org/documentation/application-development/basics/elements.html?gi-language=c)
is the fundamental building block of a pipeline. Elements perform
specific operations on incoming frames and then push the resulting
frames downstream for further processing. Elements are linked together
textually by exclamation marks (`!`) with the full chain of elements
representing the entire pipeline. Each element takes data from its
upstream element, process it and then outputs the data for processing by
the next element.

Elements designated as source elements provide input into the pipeline
from external sources. In this tutorial we use the
[filesrc](https://gstreamer.freedesktop.org/documentation/coreelements/filesrc.html?gi-language=c#filesrc)
element that reads input from a local file.

Elements designated as sink elements represent the final stage of a
pipeline. For example, a sink element could write transcoded frames to
a file on the local disk or open a window to render the video content to
the screen or even restream the content via RTSP. We will use the
standard
[autovideosink](https://gstreamer.freedesktop.org/documentation/autodetect/autovideosink.html?gi-language=c)
element to render the video frames on a local display.

We will also use the
[decodebin3](https://gstreamer.freedesktop.org/documentation/playback/decodebin3.html)
utility element. The **decodebin3** element constructs a concrete set of
decode operations, based on the given input format as well as decoder and
demuxer elements available in the system. At a high level, the
**decodebin3** abstracts the individual operations required to take
encoded frames and produce raw video frames suitable for image
transformation and inferencing.

### Properties

Elements are configured using key-value pairs called properties. For
example, the filesrc element has a property named `location`, which
specifies the file path for input.

Example of filesrc element with its filesrc property:

```bash
filesrc location=cars_1900.mp4
```

The documentation for each element describes its properties as well
as the valid range of values for each property. It can be viewed using the
command line tool **gst-inspect-1.0**.

## Introduction to Deep Learning Streamer Pipeline Framework

Deep Learning Streamer Pipeline Framework is an easy way to construct media
analytics pipelines using OpenVINO™ toolkit. It
leverages the GStreamer open source media framework to provide optimized
media operations and
[Deep Learning Inference Engine](https://docs.openvino.ai/2025/index.html)
from OpenVINO™ Toolkit to provide optimized inference.

The elements packaged in the Deep Learning Streamer Pipeline Framework
binary release can be divided into three categories:

-   Elements for optimized streaming media operations (USB and IP camera
    support, file handling, decoding, color-space-conversion, scaling,
    encoding, rendering, etc.). These elements are developed by the
    larger GStreamer community.
-   Elements that use the Deep Learning Inference Engine from OpenVINO™
    Toolkit or OpenCV for optimized video analytics (detection,
    classification, tracking). These elements are provided as part of
    the Pipeline Framework's GVA plugin.
-   Elements that convert and publish inference results to the screen as
    overlaid bounding boxes, to a file (as a list of JSON Objects), or to
    popular message brokers (Kafka or MQTT) as JSON messages. These
    elements are provided as part of the DL Streamer's GVA plugin.

The elements in the last two categories above are part of Pipeline
Framework's GVA plugin and start with the prefix `gva`. We will describe
the `gva` elements used in this tutorial with some important properties
here. Refer to
[Deep Learning Streamer elements](../elements/elements.md) page for more details.

- [gvadetect](../elements/gvadetect.md)

  \- Runs detection with the Inference Engine from OpenVINO™ Toolkit.
  We will use it to detect vehicles in a frame, and output their
  bounding boxes (aka Regions of Interest - ROI). The `queue` element must
  be put directly after the `gvadetect` element in the pipeline.

  - `model` - path to the inference model network file
  - `device` - device to run inferencing on
  - `inference-interval` - interval between inference requests, the
    bigger the value, the better the throughput. i.e. setting this
    property to 1 will run detection on every frame while
    setting it to 5 will run detection on every fifth frame.

- [gvaclassify](../elements/gvaclassify.md)
  \- Runs classification with the Inference Engine from OpenVINO™
  Toolkit. We will use it to label the bounding boxes output by `gvadetect`
  with the type and color of the vehicle. The `queue` element
  must be put directly after the `gvaclassify` element in pipeline.

  - `model` - path to the inference model network file.
  - `model-proc` - path to the model-proc file. A model-proc file
    describes the model input and output layer format. The
    model-proc file in this tutorial describes the output layer name
    and labels (person and vehicle) of objects it detects. See
    [model-proc](../dev_guide/model_proc_file.md)> for more information.
  - `device` - device to run inferencing on.

- [`gvatrack](../elements/gvatrack.md)

  \- Identifies the objects in frames where detection is skipped and
  assigns unique IDs to objects. This increases overall throughput
  by allowing us to run object detection on fewer frames, while still
  tracking the position and type of objects in every frame.

- [gvawatermark](../elements/gvawatermark.md)

  \- Overlays detection and classification results on top of video
  data. This element parses the detected vehicle results
  metadata and creates a video frame rendered with the bounding box
  aligned to the vehicle position, as well as parses the classified vehicle result
  and labels it on the bounding box.

In addition to `gvadetect` and `gvaclassify`, you can use `gvainference`
for running inference with any CNN model not supported by `gvadetect` or
`gvaclassify`. `queue` element must be put directly after `gvainference`
element in pipeline. Also, instead of visualizing the inference results,
as shown in this tutorial, you can publish them to MQTT, Kafka or a file
using `gvametaconvert` and `gvametapublish` of Deep Learning Streamer.

## Non-Docker tutorial setup

This section prepares the environment to run examples described below.
Follow these steps if you chose Option #1 (APT repository) in Install Guide
Ubuntu.

1.  Export `MODELS_PATH` to define where to download models. For
    example:

    ```bash
    export MODELS_PATH=/home/${USER}/intel/models
    ```

2.  Download the models from
    [Open Model Zoo](https://github.com/openvinotoolkit/open_model_zoo) to the
    `MODELS_PATH` directory:

    ```bash
    python3 -m pip install --upgrade pip
    python3 -m pip install openvino-dev[onnx,tensorflow,pytorch]
    mkdir -p $MODELS_PATH
    omz_downloader --name person-vehicle-bike-detection-2004,vehicle-attributes-recognition-barrier-0039 -o $MODELS_PATH
    ```

    > **NOTE:** Make sure your environment variable `$PATH` includes
    > `$HOME/.local/bin` - use `echo $PATH`.

3.  Export variables to set paths for `model` and `model_proc` files. It
    will make pipeline definition easier in later examples:

    ```bash
    export DETECTION_MODEL=${MODELS_PATH}/intel/person-vehicle-bike-detection-2004/FP16/person-vehicle-bike-detection-2004.xml
    export DETECTION_MODEL_PROC=/opt/intel/dlstreamer/samples/gstreamer/model_proc/intel/person-vehicle-bike-detection-2004.json
    export VEHICLE_CLASSIFICATION_MODEL=${MODELS_PATH}/intel/vehicle-attributes-recognition-barrier-0039/FP16/vehicle-attributes-recognition-barrier-0039.xml
    export VEHICLE_CLASSIFICATION_MODEL_PROC=/opt/intel/dlstreamer/samples/gstreamer/model_proc/intel/vehicle-attributes-recognition-barrier-0039.json
    ```

    If you want to use your own models, you first need to convert them
    to the IR (Intermediate Representation) format. For detailed
    instructions on how to convert models, look
    [here](https://docs.openvino.ai/2025/openvino-workflow/model-preparation/convert-model-to-ir.html)

4.  Export the example video file path:

    You may download a sample video from
    [here](https://github.com/intel-iot-devkit/sample-videos/raw/master/person-bicycle-car-detection.mp4).
    If you provide your own video file as input, please make sure
    that it is in h264 or mp4 format. You can also download and use
    freely licensed content from websites such as Pexels\*. Any
    video with cars, or pedestrians can be used for this exercise.

    ```bash
    # This tutorial uses ~/path/to/video as the video path
    # and FILENAME as the placeholder for a video file name.
    # Change this information to fit your setup.
    export VIDEO_EXAMPLE=~/path/to/video/FILENAME
    ```

## Docker tutorial setup

This section prepares the environment to run examples described below.
Follow these steps if you chose Option #2 (Docker) in Install Guide Ubuntu.

1.  Make sure you are on your local host, and *not* in a Docker container.

2.  Export `MODELS_PATH` to define where to download the models. For
    example:

    ```bash
    export MODELS_PATH=/home/${USER}/intel/models
    ```

3.  Download the models from
    [Open Model Zoo](https://github.com/openvinotoolkit/open_model_zoo)
    to the `MODELS_PATH` directory:

    ```bash
    python3 -m pip install --upgrade pip
    python3 -m pip install openvino-dev[onnx,tensorflow,pytorch]
    mkdir -p $MODELS_PATH
    omz_downloader --name person-vehicle-bike-detection-2004,vehicle-attributes-recognition-barrier-0039 -o $MODELS_PATH
    ```

    > **NOTE:** Make sure your environment variable `$PATH` includes
    > `$HOME/.local/bin` - use `echo $PATH`.

4.  Run the Deep Learning Streamer container.

    Run the Docker container with the models directory mounted into the
    container using `-v` or `--volume` parameter in the `docker run`
    command. Make sure your mounting parameter is specified as
    `-v <path_on_host>:<path_in_the_container>`:

    **Ubuntu 22**

    ```bash
    docker run -it --rm -v ${MODELS_PATH}:/home/dlstreamer/models --env MODELS_PATH=/home/dlstreamer/models intel/dlstreamer:2025.2.0-ubuntu22
    ```

    **Ubuntu 24**

    ```bash
    docker run -it --rm -v ${MODELS_PATH}:/home/dlstreamer/models --env MODELS_PATH=/home/dlstreamer/models intel/dlstreamer:latest
    ```

    Running Deep Learning Streamer in the Docker container with inference
    on GPU or NPU devices requires non-root user access to
    these devices in the container. Deep Learning Streamer Pipeline
    Framework Docker images do not contain a `render` group for
    `dlstreamer` non-root user because the `render` group does not have
    a strict group ID, unlike the `video` group. To run container as a
    non-root user with access to a GPU and/or NPU device, you have to specify
    the `render` group ID from your host. The full running command
    example:

    ```bash
    docker run -it --rm -v ${MODELS_PATH}:/home/dlstreamer/models \
    --device /dev/dri \
    --group-add $(stat -c "%g" /dev/dri/render*) \
    --device /dev/accel \
    --group-add $(stat -c "%g" /dev/accel/accel*) \
    --env ZE_ENABLE_ALT_DRIVERS=libze_intel_npu.so \
    --env MODELS_PATH=/home/dlstreamer/models \
    intel/dlstreamer:latest
    ```

    where the newly added parameters are:

    1.  `--device /dev/dri` - access to GPU device, required when you
        want to use GPU as an inference device (`device=GPU`) or use VA-API
        graphics hardware acceleration capabilities like `vapostproc`,
        `vah264dec`, `vah264enc`, `vah265dec`, `vah265enc` etc.
    2.  `--group-add $(stat -c "%g" /dev/dri/render*)` - non-root access
        to GPU devices, required in the same scenarios as
        `--device /dev/dri` above.
    3.  `--device /dev/accel` - access to NPU device, required when you
        want to use NPU as an inference device (`device=NPU`).
    4.  `--group-add $(stat -c "%g" /dev/accel/accel*)` - non-root
        access to NPU devices, required in the same scenarios as
        `--device /dev/accel` above.
    5.  `--env ZE_ENABLE_ALT_DRIVERS=libze_intel_npu.so` - exporting
        environmental variable needed to run inference successfully on
        NPU devices.

5.  In the container, export variables to set the paths for `model` and
    `model_proc` files. It will make pipeline definition easier in later examples:

    ```bash
    export DETECTION_MODEL=/home/dlstreamer/models/intel/person-vehicle-bike-detection-2004/FP16/person-vehicle-bike-detection-2004.xml
    export DETECTION_MODEL_PROC=/opt/intel/dlstreamer/samples/gstreamer/model_proc/intel/person-vehicle-bike-detection-2004.json
    export VEHICLE_CLASSIFICATION_MODEL=/home/dlstreamer/models/intel/vehicle-attributes-recognition-barrier-0039/FP16/vehicle-attributes-recognition-barrier-0039.xml
    export VEHICLE_CLASSIFICATION_MODEL_PROC=/opt/intel/dlstreamer/samples/gstreamer/model_proc/intel/vehicle-attributes-recognition-barrier-0039.json
    ```

    If you want to use your own models, first you need to convert them
    in the IR (Intermediate Representation) format. For detailed
    instructions on how to convert models, look [here](https://docs.openvino.ai/2025/openvino-workflow/model-preparation/convert-model-to-ir.html).

6.  In the container, export the example video file path:

    You can download a sample video from
    [here](https://github.com/intel-iot-devkit/sample-videos/raw/master/person-bicycle-car-detection.mp4).
    If you provide your own video file as input, please make sure
    that it is in h264 or mp4 format. You can also download and use
    freely licensed content from websites such as Pexels\*. Any
    video with cars, or pedestrians can be used for this exercise.

    ```bash
    # This tutorial uses ~/path/to/video as the video path
    # and FILENAME as the placeholder for a video file name.
    # Change this information to fit your setup.
    export VIDEO_EXAMPLE=~/path/to/video/FILENAME
    ```

## Exercise 1 - Build object detection pipeline

This exercise will help you create a GStreamer pipeline that will perform
object detection using the `gvadetect` element and Intermediate
Representation (IR) formatted object detection model. It provides two
optional add-ons to show you how to use video from a web camera stream
and an RTSP URI.

This exercise introduces you to using the following Pipeline Framework
elements:

- `gvadetect`
- `gvawatermark`

### Exercise 1.1 Create a Pipeline

We will create a pipeline to detect people and vehicles in a video. The
pipeline will accept input from a video file, decode it and run vehicle
detection. It will overlay the bounding boxes for detected vehicles on
the video frame and render the video to a local device.

Run the below pipeline at the command prompt and review the output:

```bash
gst-launch-1.0 \
filesrc location=${VIDEO_EXAMPLE} ! decodebin3 ! \
gvadetect model=${DETECTION_MODEL} model_proc=${DETECTION_MODEL_PROC} device=CPU ! queue ! \
gvawatermark ! videoconvert ! autovideosink sync=false
```

> **Note**: On EMT OS the `X11/wayland` display server is by default disabled. To see the
> the video for the above pipeline, replace the last gstreamer element `autovideosink sync=false`
> with `kmssink sync=false`. The system on which the pipeline is running must be working on the KVM setup.

**Expected output**: You will see your video with overlaid bounding boxes
around persons, vehicles, and bikes.

You're done building and running this pipeline. To expand on this
exercise, use one or both add-ons for this exercise to select different
video sources. If the add-ons don't suit you, jump ahead to start
[Exercise 2](#object-classification).

#### Pipeline with a Web Camera Video Stream Input (First optional add-on to Exercise 1)

GStreamer supports connected video devices, like web cameras, which
means you use a web camera to perform real-time inference.

In order to use a web camera as input, we will replace the `filesrc`
element in the object detection pipeline with the
[v4l2src](https://gstreamer.freedesktop.org/documentation/video4linux2/v4l2src.html?gi-language=c)
element, which is used for capturing video from webcams. Before running
the below updated pipeline, check the web camera path and update it in
the pipeline. The web camera stream is usually in the `/dev/` directory.

Object detection pipeline using web camera:

```bash
# Change <path-to-device> below to your web camera device path
gst-launch-1.0 \
v4l2src device=<path-to-device> ! decodebin3 ! \
gvadetect model=${DETECTION_MODEL} model_proc=${DETECTION_MODEL_PROC} device=CPU ! queue ! \
gvawatermark ! videoconvert ! autovideosink sync=false
```

#### Pipeline with an RTSP Input (Second optional add-on to Exercise 1)

In order to use an RTSP source as input, we will replace the `filesrc`
element in the object detection pipeline with
[urisourcebin](https://gstreamer.freedesktop.org/documentation/playback/urisourcebin.html?gi-language=c)
to access URIs. Before running the below updated pipeline, replace
'\<RTSP_uri\>' with your RTSP URI and verify it before running the
command.

Object detection pipeline using sample RTSP URI from Pexels:

```bash
gst-launch-1.0 \
urisourcebin uri=https://videos.pexels.com/video-files/1192116/1192116-sd_640_360_30fps.mp4 ! decodebin3 ! \
gvadetect model=${DETECTION_MODEL} model_proc=${DETECTION_MODEL_PROC} device=CPU ! queue ! \
gvawatermark ! videoconvert ! autovideosink sync=false
```

## Exercise 2: Build object classification pipeline {#object-classification}

This exercise will help you create a GStreamer pipeline that will perform
object classification on the Regions of Interest (ROIs) detected by
`gvadetect` using the `gvaclassify` element and Intermediate Representation
(IR) formatted object classification model.

This exercise uses the following Pipeline Framework elements:

- `gvadetect`
- `gvaclassify`
- `gvawatermark`

### Exercise 2.1: Create a Pipeline

We will create a pipeline to detect people and vehicles in a video and
classify the detected people and vehicles to provide additional
attributes.

Run the below pipeline at the command prompt and review the output:

```bash
gst-launch-1.0 \
filesrc location=${VIDEO_EXAMPLE} ! decodebin3 ! \
gvadetect model=${DETECTION_MODEL} model_proc=${DETECTION_MODEL_PROC} device=CPU ! queue ! \
gvaclassify model=${VEHICLE_CLASSIFICATION_MODEL} model-proc=${VEHICLE_CLASSIFICATION_MODEL_PROC} device=CPU object-class=vehicle ! queue ! \
gvawatermark ! videoconvert ! autovideosink sync=false
```

> **Note**: On EMT OS the `X11/wayland` display server is by default disabled. To see the
> the video for the above pipeline, replace the last gstreamer element `autovideosink sync=false`
> with `kmssink sync=false`. The system on which the pipeline is running must be working on the KVM setup.

**Expected output**: Persons, vehicles, and bikes are bound by colored
boxes, and detection results as well as classification attributes such
as vehicle type and color are displayed as video overlays.

In the above pipeline:

1. `gvadetect` detects the ROIs in the video and outputs ROIs with appropriate
   attributes (person, vehicle, bike) according to its `model-proc` file.
2. `gvadetect` ROIs are used as inputs for the `gvaclassify` model.
3. `gvaclassify` classifies the ROIs and outputs additional attributes
   according to the `model-proc` file:

   - `object-class` tells `gvalcassify` which ROIs to classify.
   - `object-class=vehicle` classifies ROIs with `vehicle` attribute only.

4. `gvawatermark` displays the ROIs and their attributes.

See
[model-proc](https://github.com/open-edge-platform/dlstreamer/tree/master/samples/gstreamer/model_proc)
for `model-proc` file examples as well as its input and output specifications.

## Exercise 3: Use object tracking to improve performance {#object-tracking}

This exercise helps you create a GStreamer pipeline that will use object
tracking with `gvatrack` to reduce the frequency of object detection and
classification, increasing the throughput.

This exercise uses the following Pipeline Framework elements:

- `gvadetect`
- `gvaclassify`
- `gvatrack`
- `gvawatermark`

### Exercise 3.1: Create a Pipeline

We will use the same pipeline as in exercise 2, for detecting and
classifying vehicle and people. We will add the `gvatrack` element after
`gvadetect` and before `gvaclassify` to track objects. `gvatrack` will
assign object IDs and provide updated ROIs between detections. We
will also specify the parameters of `gvadetect` and `gvaclassify` elements
to reduce the frequency of detection and classification.

Run the below pipeline at the command prompt and review the output:

```bash
gst-launch-1.0 \
filesrc location=${VIDEO_EXAMPLE} ! decodebin3 ! \
gvadetect model=${DETECTION_MODEL} model_proc=${DETECTION_MODEL_PROC} device=CPU inference-interval=10 ! queue ! \
gvatrack tracking-type=short-term-imageless ! queue ! \
gvaclassify model=${VEHICLE_CLASSIFICATION_MODEL} model-proc=${VEHICLE_CLASSIFICATION_MODEL_PROC} device=CPU object-class=vehicle reclassify-interval=10 ! queue ! \
gvawatermark ! videoconvert ! autovideosink sync=false
```

> **Note**: On EMT OS the `X11/wayland` display server is by default disabled. To see the
> the video for the above pipeline, replace the last gstreamer element `autovideosink sync=false`
> with `kmssink sync=false`. The system on which the pipeline is running must be working on the KVM setup.

**Expected output**: Persons, vehicles, and bikes are bound by colored
boxes, and detection results as well as classification attributes such
as vehicle type and color are displayed as video overlays, same as
exercise 2. However, notice the increase in the FPS of the pipeline.

In the above pipeline:

1. `gvadetect` detects the ROIs in the video and outputs ROIs with
   appropriate attributes (person, vehicle, bike), according to its
   `model-proc` file, on every 10th frame (due to `inference-interval=10`).
2. `gvatrack` tracks each object detected by `gvadetect`.
3. `gvadetect` ROIs are used as inputs for the `gvaclassify` model.
4. `gvaclassify` classifies the ROIs and outputs additional attributes
   according to model-proc, but skips classification for already
   classified objects for 10 frames, using tracking information from
   `gvatrack` to determine whether to classify an object:
   - `object-class` tells `gvaclassify` which ROIs to classify.
   - `object-class=vehicle` classifies ROIs that have the `vehicle`
     attribute.
   - `reclassify-interval` determines how often to reclassify tracked
     objects. Only valid when used in conjunction with `gvatrack`.
5. `gvawatermark` displays the ROIs and their attributes.

You're done building and running this pipeline. The next exercise shows
you how to publish your results to a `.json`.

## Exercise 4: Publish Inference Results

This exercise extends the pipeline to publish your detection and
classification results to a `.json` file from a GStreamer pipeline.

This exercise uses the following Pipeline Framework elements:

- `gvadetect`
- `gvaclassify`
- `gvametaconvert`
- `gvametapublish`

### Setup

One additional setup step is required for this exercise, to export the
output file path:

```bash
# Adjust the command below according to your needs
export OUTFILE=~/pipeline_output.json
```

### Exercise 4.1: Create a Pipeline

We will use the same pipeline as in exercise 2 for detecting and
classifying vehicle and people. However, instead of overlaying the
results and rendering them to a screen, we will send them to a file in
JSON format.

Run the below pipeline at the command prompt and review the output:

```bash
gst-launch-1.0 \
filesrc location=${VIDEO_EXAMPLE} ! decodebin3 ! \
gvadetect model=${DETECTION_MODEL} model_proc=${DETECTION_MODEL_PROC} device=CPU ! queue ! \
gvaclassify model=${VEHICLE_CLASSIFICATION_MODEL} model-proc=${VEHICLE_CLASSIFICATION_MODEL_PROC} device=CPU object-class=vehicle ! queue ! \
gvametaconvert format=json ! \
gvametapublish method=file file-path=${OUTFILE} ! \
fakesink
```

**Expected output**: After the pipeline completes, a JSON file of the
inference results is available. Review the JSON file.

In the above pipeline:

- `gvametaconvert` uses the optional parameter `format=json` to
  convert inferenced data to `GstGVAJSONMeta`.
- `gvametapublish` uses the optional parameter `method=file` to
  publish inference results to a file.
- `filepath=${OUTFILE}` is a JSON file to which the inference results
  are published.

For publishing the results to MQTT or Kafka, please refer to
[metapublish samples](https://github.com/open-edge-platform/dlstreamer/tree/master/samples/gstreamer/gst_launch/metapublish).

You have completed this tutorial. Now, start creating your video
analytics pipelines with Deep Learning Streamer Pipeline Framework!

## Additional Resources

- [Samples overview](https://github.com/open-edge-platform/dlstreamer/blob/master/samples/gstreamer/README.md)
- [Elements](../elements/elements.md)
- [How to create model-proc file](../dev_guide/how_to_create_model_proc_file.md)

------------------------------------------------------------------------

> **\*** *Other names and brands may be claimed as the property of
> others.*
