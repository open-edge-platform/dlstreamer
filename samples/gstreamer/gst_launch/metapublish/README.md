# Metadata Publishing Sample (gst-launch command line)

This sample demonstrates how [gvametaconvert](../../../../docs/user-guide/elements/gvametaconvert.md) and [gvametapublish](../../../../docs/user-guide/elements/gvametapublish.md) elements are used in a typical pipeline constructed with Deep Learning Streamer (DL Streamer) and GStreamer elements. By placing these elements to the end of a pipeline that performs face detection and emotion classification, you will quickly see how these elements enable publishing of pipeline metadata to an output file, in-memory fifo, or a popular message bus.

These elements are useful for cases where you need to record outcomes (e.g., emitting inferences) of your DL Streamer pipeline to applications running locally or across distributed systems.

## How It Works
The sample utilizes GStreamer command-line tool `gst-launch-1.0` which can build and run GStreamer pipeline described in a string format.
The string contains a list of GStreamer elements separated by exclamation mark `!`, each element may have properties specified in the format `property`=`value`.

Overall this sample builds GStreamer pipeline of the following elements:
* `filesrc` or `urisourcebin` or `v4l2src` for input from file/URL/web-camera
* `decodebin3` for video decoding
* [gvadetect](../../../../docs/user-guide/elements/gvadetect.md) for detecting faces using the OpenVINO™ Toolkit Inference Engine
* [gvaclassify](../../../../docs/user-guide/elements/gvaclassify.md) for recognizing the age and gender of detected faces using the the OpenVINO™ Toolkit Inference Engine.
* [gvametaconvert](../../../../docs/user-guide/elements/gvametaconvert.md) for conversion of tensor and inference metadata to JSON format.
* [gvametapublish](../../../../docs/user-guide/elements/gvametapublish.md) for publishing the JSON metadata as output to console, file, fifo, MQTT or Kafka.
* `fakesink` to terminate the pipeline output without actually rendering video frames.

> **NOTE**: The sample sets property 'json-indent=4' in [gvametaconvert](../../../../docs/user-guide/elements/gvametaconvert.md) element for generating JSON in pretty print format with 4 spaces indent. Remove this property to generate JSON without pretty print.

## Models

The sample is configured with example models from different sources. `centerface` is converted from the upstream CenterFace project; the `dima806/*` models are from Hugging Face. The general model-preparation helpers convert them to OpenVINO IR; see [`scripts/download_models/README.md`](../../../../scripts/download_models/README.md).
*   __centerface__ is primary detection network for detecting faces that appear within video frames.
*   __dima806/facial_age_image_detection__ ageestimation on detected faces
*   __dima806/fairface_gender_image_detection__ gender estimation on detected faces

The listed models are examples for the current pipeline configuration. Prepare compatible models before running; use the general helpers in [`scripts/download_models`](../../../../scripts/download_models/README.md) if conversion is needed. Substituting models may require updating their paths and model-proc configuration.

The sample contains `model_proc` subfolder with .json files for each model with description of model input/output formats and post-processing rules for classification models.

## Running

The sample requires an input source. With `METHOD=file` and no `OUTPUT`, it prints pretty JSON messages to the console.

> **NOTE**: Before using MQTT or Kafka output, see [listener.md](./listener.md) for listener setup.

```sh
./metapublish.sh [INPUT] [METHOD] [OUTPUT] [FORMAT] [TOPIC]
```

Arguments:

1. `INPUT` (required): local video path, supported stream URL, or camera device such as `/dev/video0`.
   An optional HTTPS example is [`head-pose-face-detection-female-and-male.mp4`](https://github.com/intel-iot-devkit/sample-videos/raw/master/head-pose-face-detection-female-and-male.mp4). Download it yourself and pass it explicitly; the sample does not download the video.
2. `METHOD`: `file` (default), `kafka`, or `mqtt`.
3. `OUTPUT`: file path for `file`; broker address for `kafka` or `mqtt`. Defaults: stdout, `localhost:9092`, or `localhost:1883`, respectively.
4. `FORMAT`: `json` or `json-lines`; defaults to `json` for file output and `json-lines` for Kafka/MQTT.
5. `TOPIC`: Kafka/MQTT topic (default: `dlstreamer`).

### Examples
1. Run with a local video and print pretty JSON to stdout.
   ```
    ./metapublish.sh /path/to/video.mp4
   ```

1. Override the file or absolute path of output file.
    ```
    ./metapublish.sh /path/to/video.mp4 \
    file /tmp/output_inferences.json
    ```

1. Output results to MQTT broker running at localhost:1883 with your listener subscribed to 'dlstreamer' topic.
    ```
    ./metapublish.sh /path/to/video.mp4 mqtt
    ```

## Sample Output
The sample
* prints gst-launch command line into console
* starts the command and emits inference events that include the evaluated age and gender for each face detected within video input frames.

## See also
* [Samples overview](../../../README.md)
