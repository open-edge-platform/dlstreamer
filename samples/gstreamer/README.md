# Deep Learning Streamer (DL Streamer) Samples

Samples are simple applications that demonstrate how to use the Intel® DL Streamer. The samples are available in the `/opt/intel/dlstreamer/samples` directory.

Samples separated into several categories
1. gst_launch command-line samples (samples construct GStreamer pipeline via [gst-launch-1.0](https://gstreamer.freedesktop.org/documentation/tools/gst-launch.html) command-line utility)
    * [Face Detection And Classification Sample](./gst_launch/face_detection_and_classification/README.md) - constructs object detection and classification pipeline example with [gvadetect](../../../dl-streamer/docs/source/elements/gvadetect.md) and [gvaclassify](../../../dl-streamer/docs/source/elements/gvaclassify.md) elements to detect faces and estimate age, gender, emotions and landmark points
    * [Audio Event Detection Sample ](./gst_launch/audio_detect/README.md) - constructs audio event detection pipeline example with [gvaaudiodetect](../../../dl-streamer/docs/source/elements/gvaaudiodetect.md) element and uses  [gvametaconvert](../../../dl-streamer/docs/source/elements/gvametaconvert.md), [gvametapublish](../../../dl-streamer/docs/source/elements/gvametapublish.md) elements to convert audio event metadata with inference results into JSON format and to print on standard out
    * [Audio Transcription Sample](./gst_launch/audio_transcribe/README.md) - performs audio transcription using OpenVino GenAI model (whisper) with [gvaaudiotranscribe](../../../dl-streamer/docs/source/elements/gvaaudiotranscribe.md)
    * [Vehicle and Pedestrian Tracking Sample](./gst_launch/vehicle_pedestrian_tracking/README.md) - demonstrates object tracking via [gvatrack](../../../dl-streamer/docs/source/elements/gvatrack.md) element
    * [Human Pose Estimation Sample](./gst_launch/human_pose_estimation/README.md) - demonstrates human pose estimation with full-frame inference via [gvaclassify](../../../dl-streamer/docs/source/elements/gvaclassify.md) element
    * [Metadata Publishing Sample](./gst_launch/metapublish/README.md) - demonstrates how [gvametaconvert](../../../dl-streamer/docs/source/elements/gvametaconvert.md) and [gvametapublish](../../../dl-streamer/docs/source/elements/gvametapublish.md) elements are used for converting metadata with inference results into JSON format and publishing to file or Kafka/MQTT message bus
    * [gvapython face_detection_and_classification Sample](./gst_launch/gvapython/face_detection_and_classification/README.md) - demonstrates pipeline customization with [gvapython](../../../dl-streamer/docs/source/elements/gvapython.md) element and application provided Python script for inference post-processing
    * [gvapython save frames with ROI Sample](./gst_launch/gvapython/save_frames_with_ROI_only/README.md) - demonstrates [gvapython](../../../dl-streamer/docs/source/elements/gvapython.md) element for saving video frames with detected objects to disk
    * [Action Recognition Sample](./gst_launch/action_recognition/README.md) - demonstrates action recognition via video_inference bin element
    * [Instance Segmentation Sample](./gst_launch/instance_segmentation/README.md) - demonstrates Instance Segmentation via object_detect and object_classify bin elements
    * [Detection with Yolo](./gst_launch/detection_with_yolo/README.md) - demonstrates how to use publicly available Yolo models for object detection and classification
    * [Deployment of Geti™ models](./gst_launch/geti_deployment/README.md) - demonstrates how to deploy models trained with Intel® Geti™ Platform for object detection, anomaly detection and classification tasks
    * [Multi-camera deployments](./gst_launch/multi_stream/README.md) - demonstrates how to handle video streams from multiple cameras with one instance of DL Streamer application
    * [gvaattachroi](./gst_launch/gvaattachroi/README.md) - demonstrates how to use gvaattachroi to define the regions on which the inference should be performed
    * [FPS Throttle](./gst_launch/gvafpsthrottle/README.md) - demonstrates how to use [gvafpsthrottle](../../../docs/source/elements/gvafpsthrottle.md) element to throttle framerate independent of sink synchronization and without frame duplication or dropping
    * [LVM embeddings](./gst_launch/lvm/README.md) - demonstrates generation of image embeddings using the Large Vision CLIP model
    * [License Plate Recognition Sample](./gst_launch/license_plate_recognition/README.md) - demonstrates the use of the Yolo detector together with the optical character recognition model
    * [Vision Language Model Sample](./gst_launch/gvagenai/README.md) - demonstrates how to use the `gvagenai` element with MiniCPM-V for video summerization
    * [Radar Signal Process Sample](./gst_launch/g3dradarprocess/README.md) - demonstrates how to use the `g3dradarprocess` element for millimeter-wave radar signal processing with point cloud detection, clustering, and tracking
    * [LiDAR Parse Sample](./gst_launch/g3dlidarparse/README.md) - demonstrates LiDAR parsing pipeline with `g3dlidarparse` element
    * [Custom Post-Processing Library Sample - Detection](./gst_launch/custom_postproc/detect/README.md) - demonstrates how to create custom post-processing library for YOLOv11 tensor outputs conversion to detection metadata using GStreamer Analytics framework
    * [Custom Post-Processing Library Sample - Classification](./gst_launch/custom_postproc/classify/README.md) - demonstrates how to create custom post-processing library for emotion classification model outputs conversion to classification metadata using GStreamer Analytics framework
2. C++ samples
    * [Draw Face Attributes C++ Sample](./cpp/draw_face_attributes/README.md) - constructs pipeline and sets "C" callback to access frame metadata and visualize inference results
3. Python samples
    * [Hello DL Streamer Sample](./python/hello_dlstreamer/README.md) - constructs an object detection pipeline, add logic to analyze metadata and count objects and visualize results along with object count summary in a local window
    * [Draw Face Attributes Python Sample](./python/draw_face_attributes/README.md) - constructs pipeline and sets Python callback to access frame metadata and visualize inference results
    * [Open Close Valve Sample](./python/open_close_valve/README.md) - constructs pipeline with two sinks. One of them has [GStreamer valve element](https://gstreamer.freedesktop.org/documentation/coreelements/valve.html?gi-language=python), which is managed based object detection result and opened/closed by callback.
    * [ONVIF Camera Discovery Sample](./python/onvif_cameras_discovery/README.md) - demonstrates automatic discovery of ONVIF-compatible cameras on the network and launches corresponding DL Streamer pipelines for video analysis.
4. Benchmark
    * [Benchmark Sample](./benchmark/README.md) - measures overall performance of single-channel or multi-channel video analytics pipelines

## How To Build And Run

Samples with C/C++ code provide `build_and_run.sh` shell script to build application via cmake before execution.

Other samples (without C/C++ code) provide .sh script for constructing and executing gst-launch or Python command line.

## DL Models

DL Streamer samples use pre-trained models from OpenVINO™ Toolkit [Open Model Zoo](https://github.com/openvinotoolkit/open_model_zoo)

Before running samples, run script `download_omz_models.sh` once to download all models required for samples. The script located in `samples` top folder.
> **NOTE**: To install all necessary requirements for `download_omz_models.sh` script run this command:
```sh
python3 -m pip install --upgrade pip
python3 -m pip install openvino-dev[onnx]
```
> **NOTE**: To install all available frameworks run this command:
```sh
python3 -m pip install openvino-dev[caffe,onnx,tensorflow2,pytorch,mxnet]
```

## Input video

First command-line parameter in DL Streamer samples specifies input video and supports
* local video file
* web camera device (ex. `/dev/video0`)
* RTSP camera (URL starting with `rtsp://`) or other streaming source (ex URL starting with `http://`)

If command-line parameter not specified, most samples by default stream video example from predefined HTTPS link, so require internet connection.

> **NOTE**: Most samples set property `sync=false` in video sink element to disable real-time synchronization and run pipeline as fast as possible. Change to `sync=true` to run pipeline with real-time speed.

## Running on remote machine

In order to run samples on remote machine over SSH with X Forwarding you should force usage of `ximagesink` as video sink first:
```sh
source ./force_ximagesink.sh
```
