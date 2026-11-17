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

The example pipeline uses `yolov8_license_plate_detector` for plate detection and `ch_PP-OCRv4_rec_infer` for OCR. These models are examples; a replacement must be compatible with the pipeline's model paths and preprocessing/postprocessing configuration. Prepare supported models with the general [`download_other_models.py`](../../../../scripts/download_models/README.md) helper.

The accuracy of license plate detection depends on whether the applied model has been trained using data representing license plates from a specific region. Choose a compatible detector trained for the relevant region; replacing the example model may require updating its pipeline configuration.

## Running

Prepare compatible models before running the sample. See the [download_models README](../../../../scripts/download_models/README.md) for helper setup and supported models.

```sh
    cd /opt/intel/dlstreamer/samples/gstreamer/gst_launch/license_plate_recognition/
    ./license_plate_recognition.sh [INPUT] [DEVICE] [OUTPUT]
```

The sample requires an input source; device and output mode are optional:

1. [INPUT] to specify the input source.  
The input could be:
    * local video file
    * web camera device (e.g., `/dev/video0`)
    * RTSP camera (URL starting with `rtsp://`) or other streaming source (e.g., URL starting with `http://`)  
Use any local video, supported stream URL, or camera source. A clip containing visible license plates is useful for checking the result.

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

* [Samples overview](../../../README.md)

