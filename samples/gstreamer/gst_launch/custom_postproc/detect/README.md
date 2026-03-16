# Custom Post-Processing Library Sample - Detection

This sample demonstrates how to create a custom post-processing library for DLStreamer that converts YOLOv11 tensor outputs to detection metadata.

## How It Works

The sample utilizes GStreamer command-line tool `gst-launch-1.0` which can build and run GStreamer pipeline described in a string format. The string contains a list of GStreamer elements separated by exclamation mark `!`, each element may have properties specified in the format `property`=`value`.

This sample builds GStreamer pipeline of the following elements:

* `filesrc` or `urisourcebin` or `v4l2src` for input from file/URL/web-camera
* `decodebin3` for video decoding
* `vapostproc` (when using GPU) for video format conversion and VA-API memory handling
* [gvadetect](../../../../../docs/user-guide/elements/gvadetect.md) for object detection using YOLOv11 model with custom post-processing library
* [gvawatermark](../../../../../docs/user-guide/elements/gvawatermark.md) for bounding boxes and labels visualization
* Various sink elements depending on output format (`autovideosink` for display, `filesink` for file output, `fakesink` for performance testing)

> **NOTE**: `sync=false` property in `autovideosink` element disables real-time synchronization so pipeline runs as fast as possible

The sample consists of:

* **Custom post-processing library** (`custom_postproc_detect.cpp`) that implements the `Convert` function to process YOLOv11 model outputs
* **Build script** (`build_and_run.sh`) that compiles the library and runs the GStreamer pipeline
* **CMake configuration** (`CMakeLists.txt`) for building the shared library

The custom post-processing library:

1. Receives tensor outputs from YOLOv11 object detection model
2. Parses the tensor data to extract bounding box coordinates, confidence scores, and class predictions
3. Applies confidence threshold filtering
4. Converts the results to GStreamer Analytics metadata
5. Attaches the metadata to the GstAnalyticsRelationMeta from GStreamer Analytics library

The pipeline uses the `gvadetect` element with the `custom-postproc-lib` parameter to load and use the custom library.

## Model

The sample uses the **YOLOv11s** model from Ultralytics, which should be available in the `$MODELS_PATH/public/yolo11s/FP32/` directory. These instructions assume that the DLStreamer framework is installed on your local system, along with the Intel® OpenVINO™ model downloader and converter tools, as described in this [tutorial](../../../../../docs/user-guide/get_started/tutorial.md#setup).

For the YOLOv11s model, it is also necessary to install the Ultralytics Python package:

```sh
pip install ultralytics
```

Use the `download_public_models.sh` script located in the top-level `samples` directory. This script allows you to download the full suite of YOLO models or select an individual model. To select the YOLOv11s model, execute the following command:

```sh
./download_public_models.sh yolo11s
```

> **NOTE**: Remember to set the `MODELS_PATH` environment variable, which is required by both the model download script and the script that runs the sample.

## Running

```sh
./build_and_run.sh [DEVICE] [OUTPUT] [INPUT]
```

The script takes three optional parameters:

1. **[DEVICE]** - Inference device (default: `GPU`)
   * Supported values: `CPU`, `GPU`, `NPU`

2. **[OUTPUT]** - Output format (default: `file`)
   * `file` - Save output to MP4 file
   * `display` - Show output on screen
   * `fps` - Print FPS only
   * `json` - Save metadata to JSON file
   * `display-and-json` - Show on screen and save to JSON

3. **[INPUT]** - Input source (default: sample video from URL)
   * Local video file path
   * Web camera device (e.g., `/dev/video0`)
   * RTSP stream URL
   * HTTP video stream URL

## Examples

Run with default parameters (GPU device, file output, sample video):

```sh
./build_and_run.sh
```

Run with CPU device and display output:

```sh
./build_and_run.sh CPU display
```

Run with GPU device, JSON output, and local video file:

```sh
./build_and_run.sh GPU json /path/to/your/video.mp4
```

Run with web camera input:

```sh
./build_and_run.sh CPU display /dev/video0
```

## Requirements

Before building and running the sample, ensure you have the following dependencies installed:

```sh
sudo apt install cmake make build-essential pkg-config
```

The sample also requires:

* DLStreamer framework
* GStreamer development packages
* GStreamer Analytics library
* OpenVINO™ toolkit

## Sample Output

The sample:

* Compiles the custom post-processing library into `libcustom_postproc_detect.so`
* Prints the complete GStreamer pipeline command
* Runs object detection using YOLOv11 with custom post-processing
* Depending on the output parameter:
  * **file**: Saves detected objects in MP4 video file
  * **display**: Shows real-time detection with bounding boxes
  * **fps**: Prints frame rate performance
  * **json**: Exports detection metadata to JSON file
  * **display-and-json**: Combines display and JSON output

## Implementation Details

### Custom Post-Processing Library Architecture

This sample demonstrates the **custom post-processing library** approach for extending DLStreamer functionality. This approach provides flexibility and modularity while maintaining clean separation between the core framework and custom processing logic, without requiring modifications to the DLStreamer source code.

### GStreamer Analytics Framework Integration

The custom library uses the **GStreamer Analytics Library** which provides standardized metadata structures for AI/ML results. The library implements structures
such as ``GstTensorMeta``, ``GstAnalyticsRelationMeta``, ``GstAnalyticsODMtd``,
``GstAnalyticsClsMtd``, and others.

For more information about the Analytics metadata library, please refer to:
`GStreamer Analytics Documentation <https://gstreamer.freedesktop.org/documentation/analytics/index.html?gi-language=c>`__

Key components used in this sample include:

* `GstTensorMeta` - contains output tensor data from model inference
* `GstAnalyticsRelationMeta` - output structure for attaching results
* `GstAnalyticsODMtd` - object detection metadata format
* `GstStructure` - flexible key-value container for model and parameter information

### Convert Function Implementation

The library exports a `Convert` function with the required signature:

```c
extern "C" void Convert(GstTensorMeta *outputTensors,
                        const GstStructure *network_info,
                        const GstStructure *params_info,
                        GstAnalyticsRelationMeta *relationMeta);
```

**Parameters:**

* `outputTensors` - model output tensors (always in unbatched format regardless of batch-size setting)
* `network_info` - model metadata including labels, input dimensions (image_width, image_height)
* `params_info` - processing parameters like confidence_threshold
* `relationMeta` - output structure for attaching detection results

### YOLOv11 Tensor Format Processing

The custom post-processing function handles YOLOv11 output tensor format:

* **Offset 0**: X center coordinate
* **Offset 1**: Y center coordinate
* **Offset 2**: Width
* **Offset 3**: Height
* **Offset 4+**: Class confidence scores

### Processing Pipeline

The library:

1. **Tensor Access**: Maps tensor data using `gst_buffer_map()` for raw model output access
2. **Metadata Extraction**: Retrieves network info (image dimensions, labels) and parameters (confidence threshold)
3. **Data Processing**: Performs tensor manipulation and confidence score processing
4. **Filtering**: Applies confidence threshold filtering
5. **Format Conversion**: Converts center-point format to top-left corner format for detection metadata
6. **Metadata Attachment**: Integrates with GStreamer Analytics framework using `gst_analytics_relation_meta_add_od_mtd()`

### Technical Notes

* Each model output layer has a separate `GstTensor` contained within one `GstTensorMeta`
* Tensors are always passed in **unbatched** format (batch dimension = 1) regardless of pipeline batch-size setting
* The library supports **object detection** tasks with `GstAnalyticsODMtd` metadata and **classification** tasks with `GstAnalyticsClsMtd` metadata

## See also

* [Samples overview](../../../README.md)
* [DLStreamer documentation](../../../../../docs/user-guide/index.md)
* [Custom post-processing guide](../../../../../docs/user-guide/dev_guide/custom_processing.md#6-create-custom-post-processing-library)
* [GStreamer Analytics Documentation](https://gstreamer.freedesktop.org/documentation/analytics/index.html?gi-language=c)
* [Custom Post-Processing to Tensor Sample](../classify/README.md)
