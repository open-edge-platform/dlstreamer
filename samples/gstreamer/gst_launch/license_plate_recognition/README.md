# License Plate Recognition Sample (gst-launch command line)

This sample demonstrates the License Plate Recognition (LPR) pipeline constructed via `gst-launch-1.0` command-line utility.


## How It Works

The sample utilizes GStreamer command-line tool `gst-launch-1.0` which can build and run a GStreamer pipeline described in a string format.
The string contains a list of GStreamer elements separated by an exclamation mark `!`, each element may have properties specified in the format `property=value`.

This sample builds a GStreamer pipeline of the following elements:

* `filesrc`, `urisourcebin`, or `v4l2src` for input from file/URL/web-camera
* `decodebin3` for video decoding
* `videoconvert` for converting video frames into different color formats
* `vapostproc` for post-processing (used in GPU pipeline)
* `gvadetect` for running the license plate detector
* `gvaclassify` for running the OCR model
* `gvametaconvert` for converting metadata to JSON format
* `gvametapublish` for publishing metadata to a file
* `gvafpscounter` for measuring FPS (used in FPS mode)
* `fakesink` for discarding the output

## Models

The sample uses the [`yolov8_license_plate_detector`](https://github.com/open-edge-platform/edge-ai-resources/blob/main/models/license-plate-reader.zip) model for license plate detection and the [`ch_PP-OCRv4_rec_infer`](https://github.com/PaddlePaddle/PaddleOCR) model for optical character recognition (OCR). The required model preparation is handled by [`download_other_models.sh`](../../../../scripts/download_models/download_other_models.sh).

The accuracy of license plate detection depends on whether the applied model has been trained using data representing license plates from a specific region. In addition to the default model used in this example, the same pipeline can be used with other license plate detectors, such as the one available [`here`](https://github.com/Muhammad-Zeerak-Khan/Automatic-License-Plate-Recognition-using-YOLOv8).

## Running

```sh
    export MODELS_PATH="$HOME"/models
    cd /opt/intel/dlstreamer/scripts/download_models/
    ./download_other_models.sh yolov8_license_plate_detector,ch_PP-OCRv4_rec_infer
    cd /opt/intel/dlstreamer/samples/gstreamer/gst_launch/license_plate_recognition/
    ./license_plate_recognition.sh [INPUT] [DEVICE] [OUTPUT]
```

The sample takes three command-line *optional* parameters:

1. [INPUT] to specify the input source.  
The input could be:
    * local video file
    * web camera device (e.g., `/dev/video0`)
    * RTSP camera (URL starting with `rtsp://`) or other streaming source (e.g., URL starting with `http://`)  
If the parameter is not specified, the sample by default streams a video example from an HTTPS link (utilizing the `urisourcebin` element), so it requires an internet connection.

2. [DEVICE] to specify the device for inference.  
   You can choose either `CPU`, `GPU` or `AUTO`.
3. [OUTPUT] to choose between several output modes:
    * display - screen rendering
    * json - output to a JSON file
    * file - output to an mp4 video file
    * display-and-json
    * fps - FPS only
    

## Sample Output

The sample:
* prints gst-launch command line into console
* starts the command and shows the video with bounding boxes around the detected license plates together with the recognized text, or prints out fps if you set OUTPUT to fps.

## See also

* [Samples overview](../../README.md)

