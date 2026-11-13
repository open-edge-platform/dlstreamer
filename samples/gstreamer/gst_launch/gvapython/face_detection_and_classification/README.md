# gvapython Sample

This sample demonstrates [gvapython](../../../../../docs/user-guide/elements/gvapython.md) element and ability to customize pipeline with application provided Python script for pre- or post-processing of inference operations. It typically used for interpretation of inference results and various application logic, especially if required in the middle of GStreamer pipeline.

## How It Works
In this sample the `gvapython` element is used three times.

 First time it's inserted after `gvainference` element running object detection. This demonstrates custom conversion of model output into list of bounding boxes. See file `ssd_object_detection.py` with conversion function coded in Python.

 Second time it's inserted after `gvainference` element running object classification. This demonstrates custom conversion of model output into object attributes (age and gender in this example). See file `age_gender_classification.py` with conversion function coded in Python.

 Third time it's inserted after previous `gvapython` to log ages into file. This demonstrates resource management in Python script. It opens log file in class constructor and closes it in destructor. Constructor will be called during pipeline initialization and destructor will be called on pipeline stop. See file `age_logger.py`. This is just an example of how to work with resources in Python script, for better logging please refer to [Metadata Publishing Sample](../../metapublish/README.md)

## Models

The sample is configured with these example models from different sources. `centerface` is converted from the upstream CenterFace project; the `dima806/*` models are from Hugging Face. The general model-preparation helpers convert them to OpenVINO IR; see [`scripts/download_models/README.md`](../../../../scripts/download_models/README.md).
*   __centerface__ is primary detection network for finding faces
*   __dima806/facial_age_image_detection__ age estimation on detected faces
*   __dima806/fairface_gender_image_detection__ gender estimation on detected faces

The listed models are examples for the current pipeline configuration. Prepare compatible models before running; use the general helpers in [`scripts/download_models`](../../../../scripts/download_models/README.md) if conversion is needed. If you substitute models, update the paths and ensure their outputs match the Python post-processing callbacks.

## Running

If Python requirements are not installed yet:

```sh
uv pip install -r ../../../../requirements.txt
cd -
```
Run sample:

```sh
./face_detection_and_classification.sh [INPUT_VIDEO] [DEVICE] [SINK_ELEMENT]
```
The sample requires an input source; device and sink are optional:
1. [INPUT_VIDEO] to specify input video file.
The input could be
* local video file
* web camera device (ex. `/dev/video0`)
* RTSP camera (URL starting with `rtsp://`) or other streaming source (ex URL starting with `http://`)
Use any local video, supported stream URL, or camera source. A clip containing faces is useful for this example.
2. [DEVICE] to specify device for detection and classification.
        Please refer to OpenVINO™ toolkit documentation for supported devices.
        https://docs.openvinotoolkit.org/latest/openvino_docs_IE_DG_supported_plugins_Supported_Devices.html
        You can find what devices are supported on your system by running following OpenVINO™ toolkit sample:
        https://docs.openvinotoolkit.org/latest/openvino_inference_engine_ie_bridges_python_sample_hello_query_device_README.html
3. [SINK_ELEMENT] to choose between render mode and fps throughput mode:
    * display - render (default)
    * fps - FPS only

## Sample Output

The sample
* prints gst-launch-1.0 full command line into console
* starts the command and either visualizes video with bounding boxes around detected faces, facial landmarks points and text with classification results (age/gender, emotion) for each detected face or
prints out fps if you set SINK_ELEMENT = fps

## See also
* [Samples overview](../../../../README.md)
