# Deep Learning Streamer Pipeline Framework Release 2025.1.2

Deep Learning Streamer Pipeline Framework is a streaming media analytics framework, based on GStreamer* multimedia framework, for creating complex media analytics pipelines. It ensures pipeline interoperability and provides optimized media, and inference operations using Intel® Distribution of OpenVINO™ Toolkit Inference Engine backend, across Intel® architecture, CPU, discrete GPU, integrated GPU and NPU.
The complete solution leverages:

- Open source GStreamer\* framework for pipeline management
- GStreamer* plugins for input and output such as media files and real-time streaming from camera or network
- Video decode and encode plugins, either CPU optimized plugins or GPU-accelerated plugins based on VAAPI
- Deep Learning models converted from training frameworks TensorFlow\*, Caffe\* etc.
- The following elements in the Pipeline Framework repository:

  | Element | Description |
  |---|---|
  | [gvadetect](./elements/gvadetect.md) | Performs object detection on a full-frame or region of interest (ROI)   using object detection models such as YOLOv4-v11, MobileNet SSD, Faster-RCNN etc. Outputs the ROI for detected   objects. |
  | [gvaclassify](./elements/gvaclassify.md) | Performs object classification. Accepts the ROI as an input and   outputs classification results with the ROI metadata. |
  | [gvainference](./elements/gvainference.md) | Runs deep learning inference on a full-frame or ROI using any model   with an RGB or BGR input. |
  | [gvatrack](./elements/gvatrack.md) | Performs object tracking using zero-term, or imageless tracking algorithms.   Assigns unique object IDs to the tracked objects. |
  | [gvaaudiodetect](./elements/gvaaudiodetect.md) | Performs audio event detection using AclNet model. |
  | [gvagenai](./elements/gvagenai.md) | Performs inference with Vision Language Models using OpenVINO™ GenAI, accepts video and text prompt as an input, and outputs text description. It can be used to generate text summarization from video. |
  | [gvaattachroi](./elements/gvaattachroi.md) | Adds user-defined regions of interest to perform inference on,   instead of full frame. |
  | [gvafpscounter](./elements/gvafpscounter.md) | Measures frames per second across multiple streams in a single   process. |
  | [gvametaaggregate](./elements/gvametaaggregate.md) | Aggregates inference results from multiple pipeline   branches |
  | [gvametaconvert](./elements/gvametaconvert.md) | Converts the metadata structure to the JSON format. |
  | [gvametapublish](./elements/gvametapublish.md) | Publishes the JSON metadata to MQTT or Kafka message brokers or   files. |
  | [gvapython](./elements/gvapython.md) | Provides a callback to execute user-defined Python functions on every   frame. Can be used for metadata conversion, inference post-processing, and other tasks. |
  | [gvarealsense](./elements/gvarealsense.md) | Provides integration with Intel RealSense cameras, enabling video and depth stream capture for use in GStreamer pipelines. |
  | [gvawatermark](./elements/gvawatermark.md) | Overlays the metadata on the video frame to visualize the inference   results. |

For the details on supported platforms, please refer to [System Requirements](./get_started/system_requirements.md).
For installing Pipeline Framework with the prebuilt binaries or Docker\* or to build the binaries from the open source, refer to [Deep Learning Streamer Pipeline Framework installation guide](./get_started/install/install_guide_index.md).

## New in this Release

| Title | High-level description |
|---|---|
| Custom model post-processing | End user can now create a custom post-processing library (.so); [sample](https://github.com/open-edge-platform/dlstreamer/tree/master/samples/gstreamer/gst_launch/custom_postproc) added as reference.  |
| Latency mode support | Default scheduling policy for DL Streamer is throughput. With this change user can add scheduling-policy=latency for scenarios that prioritize latency requirements over throughput. |
|  |  |
| Visual Embeddings enabled | New models enabled to convert input video into feature embeddings, validated with Clip-ViT-Base-B16/Clip-ViT-Base-B32 models; [sample](https://github.com/open-edge-platform/dlstreamer/tree/master/samples/gstreamer/gst_launch/lvm) added as reference. |
| VLM models support | new gstgenai element added to convert video into text (with VLM models), validated with miniCPM2.6, available in advanced installation option when building from sources; [sample](https://github.com/open-edge-platform/dlstreamer/tree/master/samples/gstreamer/gst_launch/gvagenai) added as reference. |
| INT8 automatic quantization support for Yolo models | Performance improvement, automatic INT8 quantization for Yolo models |
| MS Windows 11 support  | Native support for Windows 11 |
| New Linux distribution (Azure Linux derivative) | New distribution added, DL Streamer can be now installed on Edge Microvisor Toolkit. |
| License plate recognition use case support | Added support for models that allow to recognize license plates; [sample](https://github.com/open-edge-platform/dlstreamer/tree/master/samples/gstreamer/gst_launch/license_plate_recognition) added as reference.  |
| Deep Scenario model support | Commercial 3D model support |
| Anomaly model support | Added support for anomaly model, [sample](https://github.com/open-edge-platform/dlstreamer/tree/master/samples/gstreamer/gst_launch/geti_deployment) added as reference, sample added as reference. |
| RealSense element support | New [gvarealsense](./elements/gvarealsense.md) element implementation providing basic integration with Intel RealSense cameras, enabling video and depth stream capture for use in GStreamer pipelines. |
| OpenVINO 2025.3 version support | Support of recent OpenVINO version added. |
| GStreamer 1.26.6 version support | Support of recent GStreamer version added. |
| NPU 1.19 version driver support | Support of recent NPU driver version added. |
| Docker image size reduction | Reduction for all images, e.g., Ubuntu 24 Release image size reduced to 1.6GB from 2.6GB |

## Known Issues

| Issue | Issue Description |
|---|---|
| VAAPI memory with `decodebin` | If you are using `decodebin` in conjunction with `vaapi-surface-sharing` preprocessing backend you should set caps filter using `""video/x-raw(memory:VASurface)""` after `decodebin` to avoid issues with pipeline initialization |
| Artifacts on `sycl_meta_overlay` | Running inference results visualization on GPU via `sycl_meta_overlay` may produce some partially drawn bounding boxes and labels |
| Preview Architecture 2.0 Samples | Preview Arch 2.0 samples have known issues with inference results. |
| Sporadic hang on `vehicle_pedestrian_tracking_20_cpu` sample | Using Tiger Lake CPU to run this sample may lead to sporadic hang at 99.9% of video processing. Rerun the sample as W/A or use GPU instead. |
| Simplified installation process for option 2 via script | In certain configurations, users may encounter visible errors |
| Error when using legacy YoloV5 models: Dynamic resize: Model width dimension shall be static | To avoid the issue, modify `samples/download_public_models.sh` by inserting the following snippet at lines 273 and 280: |
| | python3 - <<EOF ""${MODEL_NAME}""<br>import sys, os<br>from openvino.runtime import Core<br>from openvino.runtime import save_model<br>model_name = sys.argv[1]<br>core = Core()<br>os.rename(f""{model_name}_openvino_model"", f""{model_name}_openvino_modelD"")<br>model = core.read_model(f""{model_name}_openvino_modelD/{model_name}.xml"")<br>model.reshape([-1, 3, 640, 640]) |
