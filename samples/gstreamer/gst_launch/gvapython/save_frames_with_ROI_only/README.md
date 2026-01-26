# gvapython Sample - Save Frames with ROI Only

This sample demonstrates [gvapython](../../../../../docs/source/elements/gvapython.md) element with custom Python script to save video frames containing detected objects. It showcases practical post-processing use case where frames with regions of interest (ROI) are automatically saved to disk.

## How It Works
In this sample the `gvapython` element is inserted after `gvainference` element running object detection. The Python script (`simple_frame_saver.py`) processes each frame and saves it to disk when:
* At least one object is detected with confidence above threshold (default: 0.5)
* Sufficient time has elapsed since the last save (default: 2.0 seconds)

The saved frames include:
* Bounding boxes drawn around detected objects
* Labels with confidence scores
* Sequential numbering for easy organization

The script demonstrates:
* Accessing frame data and metadata through VideoFrame API
* Working with different video formats (NV12, I420, BGR, BGRA, BGRX)
* Drawing annotations on frames using OpenCV
* Saving processed frames to disk
* Rate limiting to avoid excessive disk I/O

Configuration options in the Python script:
* `OUTPUT_DIR` - Directory for saved frames (default: "saved_frames")
* `SAVE_INTERVAL` - Minimum seconds between saves (default: 2.0)
* `MIN_CONFIDENCE` - Minimum detection confidence threshold (default: 0.5)

## Models

The sample uses by default the following pre-trained model from OpenVINO™ Toolkit [Open Model Zoo](https://github.com/openvinotoolkit/open_model_zoo)
*   __face-detection-adas-0001__ is primary detection network for finding faces

> **NOTE**: Before running samples (including this one), run script `download_omz_models.sh` once (the script located in `samples` top folder) to download all models required for this and other samples.

You can modify the script to use other detection models (e.g., person detection, vehicle detection) by changing the model path in the shell script.

## Running

If Python requirements are not installed yet:

```sh
python3 -m pip install --upgrade pip
python3 -m pip install -r ../../../../requirements.txt
```

Run sample:

```sh
./save_frames_with_roi.sh [INPUT_VIDEO] [DEVICE] [SINK_ELEMENT]
```

The sample takes three command-line *optional* parameters:
1. [INPUT_VIDEO] to specify input video file.
   The input could be
   * local video file
   * web camera device (ex. `/dev/video0`)
   * RTSP camera (URL starting with `rtsp://`) or other streaming source (ex URL starting with `http://`)
   
   If parameter is not specified, the sample by default streams video example from HTTPS link (utilizing `urisourcebin` element) so requires internet connection.

2. [DEVICE] to specify device for detection.
   Please refer to OpenVINO™ toolkit documentation for supported devices.
   https://docs.openvinotoolkit.org/latest/openvino_docs_IE_DG_supported_plugins_Supported_Devices.html
   
   You can find what devices are supported on your system by running following OpenVINO™ toolkit sample:
   https://docs.openvinotoolkit.org/latest/openvino_inference_engine_ie_bridges_python_sample_hello_query_device_README.html

3. [SINK_ELEMENT] to choose between render mode and fps throughput mode:
   * display - render (default)
   * fps - FPS only

## Sample Output

The sample:
* Prints gst-launch-1.0 full command line into console
* Starts the command and visualizes video with bounding boxes around detected faces or prints out fps if you set SINK_ELEMENT = fps
* Saves frames with detections to the `saved_frames` directory (created automatically)
* Prints status message for each saved frame: `Saved: saved_frames/frame_00000.jpg (format: NV12, shape: ...)`

Example output in console:
```
Saved: saved_frames/frame_00000.jpg (format: NV12, shape: (1080, 1920))
Saved: saved_frames/frame_00001.jpg (format: NV12, shape: (1080, 1920))
Saved: saved_frames/frame_00002.jpg (format: NV12, shape: (1080, 1920))
...
```

The saved frames are numbered sequentially and include all detected objects with their bounding boxes and labels.

## See also
* [Samples overview](../../../README.md)
* [gvapython element documentation](../../../../../docs/source/elements/gvapython.md)
