# Custom Post-Processing Library Sample - Classification

This sample demonstrates how to create a custom post-processing library for DLStreamer that converts classification model outputs to classification metadata using the GStreamer Analytics framework.

## How It Works

The sample utilizes GStreamer command-line tool `gst-launch-1.0` which can build and run GStreamer pipeline described in a string format. The string contains a list of GStreamer elements separated by exclamation mark `!`, each element may have properties specified in the format `property`=`value`.

This sample builds GStreamer pipeline of the following elements:

* `filesrc` or `urisourcebin` or `v4l2src` for input from file/URL/web-camera
* `decodebin3` for video decoding
* `vapostproc` (when using GPU) for video format conversion and VA-API memory handling
* [gvadetect](../../../../../docs/user-guide/elements/gvadetect.md) for face detection
* [gvaclassify](../../../../../docs/user-guide/elements/gvaclassify.md) for emotion classification using custom post-processing library
* [gvawatermark](../../../../../docs/user-guide/elements/gvawatermark.md) for bounding boxes and labels visualization
* Various sink elements depending on output format (`autovideosink` for display, `filesink` for file output, `fakesink` for performance testing)

> **NOTE**: `sync=false` property in `autovideosink` element disables real-time synchronization so pipeline runs as fast as possible

The sample consists of:

* **Custom post-processing library** (`custom_postproc_classify.cpp`) that implements the `Convert` function to process emotion classification model outputs
* **Build script** (`build_and_run.sh`) that compiles the library and runs the GStreamer pipeline
* **CMake configuration** (`CMakeLists.txt`) for building the shared library

The custom post-processing library:

1. Receives tensor outputs from the emotion classification model
2. Finds the maximum probability class from the tensor data
3. Converts the result to a classification label using the model's label list
4. Creates GStreamer Analytics classification metadata (`GstAnalyticsClsMtd`)
5. Attaches the metadata to the `GstAnalyticsRelationMeta` from GStreamer Analytics library

The pipeline uses the `gvaclassify` element with the `custom-postproc-lib` parameter to load and use the custom library.

## Models

The sample uses the following pre-trained models:

* **centerface** - primary detection network for finding faces
* **hsemotion** - emotion classification on detected faces with custom post-processing

Use the `download_public_models.sh` script found in the top-level `samples` directory to download the required models. You can download both models by executing:

```sh
./download_public_models.sh centerface
./download_public_models.sh hsemotion
```

Or download all available models:

```sh
./download_public_models.sh all
```

> **NOTE**: Remember to set the `MODELS_PATH` environment variable, which is needed by both the script that downloads the models and the script that runs the sample.

These instructions assume that the DLStreamer framework is installed on your local system, along with the Intel® OpenVINO™ model downloader and converter tools, as described in this [tutorial](../../../../../docs/user-guide/get_started/tutorial.md#setup).

## Running

```sh
./build_and_run.sh [DEVICE] [OUTPUT] [INPUT_VIDEO]
```

The sample takes three command-line *optional* parameters:

1. **[DEVICE]** to specify device for inference (default: `GPU`)
   * Supported values: `CPU`, `GPU`, `NPU`

2. **[OUTPUT]** to specify output format (default: `file`)
   * `file` - saves processed video to MP4 file
   * `display` - renders to screen
   * `fps` - prints FPS without visualization
   * `json` - writes metadata to JSON file
   * `display-and-json` - renders to screen and writes to JSON file

3. **[INPUT_VIDEO]** to specify input video source (default: sample video from web)
   * Local video file path
   * Web camera device (e.g., `/dev/video0`)
   * RTSP camera URL (starting with `rtsp://`)
   * HTTP streaming URL (starting with `http://`)

Examples:

```sh
# Run with default parameters (GPU device, file output)
./build_and_run.sh

# Run on CPU with display output
./build_and_run.sh CPU display

# Run with webcam input
./build_and_run.sh GPU display /dev/video0

# Run with local video file
./build_and_run.sh GPU file /path/to/video.mp4
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

* Compiles the custom post-processing library into `libcustom_postproc_classify.so`
* Prints the complete GStreamer pipeline command
* Starts the pipeline and processes video with face detection and emotion classification
* Visualizes results with bounding boxes around detected faces and emotion labels
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
* `GstAnalyticsClsMtd` - classification metadata format
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
* `params_info` - processing parameters (not used in this classification example)
* `relationMeta` - output structure for attaching classification results

### Emotion Classification Tensor Format Processing

The custom post-processing function handles emotion classification model output tensor format:

* **1D tensor**: Contains confidence scores for each emotion class
* **Index-based mapping**: Each tensor element corresponds to a specific emotion label
* **Maximum selection**: Uses `std::max_element()` to find the class with highest confidence

### Processing Pipeline

The library:

1. **Tensor Access**: Maps tensor data using `gst_buffer_map()` for raw model output access
2. **Metadata Extraction**: Retrieves network info (image dimensions, labels) from model configuration
3. **Data Processing**: Finds maximum confidence class using `std::max_element()`
4. **Label Mapping**: Maps tensor index to emotion label string using model's label list
5. **Metadata Creation**: Creates `GstAnalyticsClsMtd` with confidence score and class label
6. **Metadata Attachment**: Integrates with GStreamer Analytics framework using `gst_analytics_relation_meta_add_one_cls_mtd()`

### Technical Notes

* Each model output layer has a separate `GstTensor` contained within one `GstTensorMeta`
* Tensors are always passed in **unbatched** format (batch dimension = 1) regardless of pipeline batch-size setting
* The library supports **classification** tasks with `GstAnalyticsClsMtd` metadata for emotion recognition
* Classification results are attached to detected face regions from the detection stage

## See also

* [Samples overview](../../../README.md)
* [DLStreamer documentation](../../../../../docs/user-guide/index.md)
* [Custom post-processing guide](../../../../../docs/user-guide/dev_guide/custom_processing.md#6-create-custom-post-processing-library)
* [GStreamer Analytics Documentation](https://gstreamer.freedesktop.org/documentation/analytics/index.html?gi-language=c)
* [Custom Post-Processing to ROI Sample](../detect/README.md)
