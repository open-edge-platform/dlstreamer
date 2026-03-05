# Deep Learning Streamer (DL Streamer) Python Samples

Python samples are simple applications that demonstrate how to integrate DLStreamer pipelines with user-specific code written in Python. 
Specifically, these samples show how to directly use PyTorch models (from Hugging Face or other hubs) in DLStreamer inference elements. 

The following samples are available: 

| Sample | Image | Description |
|--------|-------|-------------|
| [Hello DLStreamer](./hello_dlstreamer/README.md) | <img src="./hello_dlstreamer/hello_dlstreamer.jpg" width="200" height="70"> | Demonstrates how to build a simple object detection pipeline and add a custom callback to process detection results. |
| [Face Detection And Classification](./face_detection_and_classification/README.md) | <img src="./face_detection_and_classification/face_detection_sample.jpg" width="200" height="70"> | Demonstrates how to integrate models from HuggingFace and use them with DLStreamer detect and classify pipelines. |
| [ONVIF Cameras Discovery](./ace_detection_and_classification/README.md) | <img src="./onvif_cameras_discovery/dls_onvif_sample.jpg" width="200" height="70">  | Demonstrates how to discover ONVIF cameras on a network and connect them to a DLStreamer video analytics pipeline. |
| [Open/Close Valve](./open_close_valve/README.md) | <img src="./open_close_valve/dls_valve_sample.jpg" width="200" height="70">  | Demonstrates how to build dynamic pipelines with behavior that depends on objects detected in the input video stream. |
| [Prompted Detect ion](./prompted_detection/README.md) | <img src="./prompted_detection/prompted_detection_sample.jpg" width="200" height="70"> | Illustrates how to use an open-vocabulary classification model to run user-defined queries on input video streams. |
| [VLM Alerts](./vlm_alerts/README.md) | <img src="./vlm_alerts/vlm_sample.jpg" width="200" height="70"> | Illustrates how to use a GenAI VLM model from the HuggingFace hub to identify and trigger user-defined alerts on input video streams. |
| [Smart NVR](./smart_nvr/README.md) | <img src="./smart_nvr/smart_nvr_output.jpg" width="200" height="70"> | Illustrates how to add custom analytics logic (detection of line hogging events) and custom storage (saving video files as chunks with custom metadata). |

