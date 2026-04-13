# Sample Index

Before generating code, read the relevant existing samples to understand established conventions.

## Python Samples

| Sample | Key Pattern | Path |
|--------|-------------|------|
| hello_dlstreamer | Minimal pipeline + pad probe | `samples/gstreamer/python/hello_dlstreamer/` |
| face_detection_and_classification | Detect → classify chain, HuggingFace model export | `samples/gstreamer/python/face_detection_and_classification/` |
| prompted_detection | Third-party model integration (YOLOE), appsink callback | `samples/gstreamer/python/prompted_detection/` |
| open_close_valve | Dynamic pipeline control, tee + valve, OOP controller | `samples/gstreamer/python/open_close_valve/` |
| vlm_alerts | VLM inference (gvagenai), argparse config, file output | `samples/gstreamer/python/vlm_alerts/` |
| vlm_self_checkout | Computer Vision detection and VLM classification, multi-branch tee, custom frame selection for VLM | `samples/gstreamer/python/vlm_self_checkout/` |
| smart_nvr | Custom Python GStreamer elements (analytics + recorder), chunked storage | `samples/gstreamer/python/smart_nvr/` |
| onvif_cameras_discovery | Multi-camera RTSP, ONVIF discovery, subprocess orchestration | `samples/gstreamer/python/onvif_cameras_discovery/` |
| draw_face_attributes | Detect → multi-classify chain, custom tensor post-processing in pad probe callback | `samples/gstreamer/python/draw_face_attributes/` |
| coexistence | DL Streamer + DeepStream coexistence, Docker orchestration, multi-framework LPR | `samples/gstreamer/python/coexistence/` |

## Command Line Samples

| Sample | Key Pattern | Path |
|--------|-------------|------|
| face_detection_and_classification | Detection + classification chain (`gvadetect` → `gvaclassify`) | `samples/gstreamer/gst_launch/face_detection_and_classification/` |
| audio_detect | Audio event detection + metadata publish | `samples/gstreamer/gst_launch/audio_detect/` |
| audio_transcribe | Audio transcription with `gvaaudiotranscribe` | `samples/gstreamer/gst_launch/audio_transcribe/` |
| vehicle_pedestrian_tracking | Detection + tracking (`gvatrack`) | `samples/gstreamer/gst_launch/vehicle_pedestrian_tracking/` |
| human_pose_estimation | Full-frame pose estimation/classification | `samples/gstreamer/gst_launch/human_pose_estimation/` |
| metapublish | Metadata conversion and publish (`gvametaconvert`/`gvametapublish`) | `samples/gstreamer/gst_launch/metapublish/` |
| gvapython/face_detection_and_classification | Python post-processing via `gvapython` | `samples/gstreamer/gst_launch/gvapython/face_detection_and_classification/` |
| gvapython/save_frames_with_ROI_only | Save ROI frames with `gvapython` | `samples/gstreamer/gst_launch/gvapython/save_frames_with_ROI_only/` |
| action_recognition | Action recognition pipeline | `samples/gstreamer/gst_launch/action_recognition/` |
| instance_segmentation | Instance segmentation pipeline | `samples/gstreamer/gst_launch/instance_segmentation/` |
| detection_with_yolo | YOLO-based detection/classification | `samples/gstreamer/gst_launch/detection_with_yolo/` |
| geti_deployment | Intel® Geti™ model deployment | `samples/gstreamer/gst_launch/geti_deployment/` |
| multi_stream | Multi-camera / multi-stream processing | `samples/gstreamer/gst_launch/multi_stream/` |
| gvaattachroi | Attach custom ROIs before inference | `samples/gstreamer/gst_launch/gvaattachroi/` |
| gvafpsthrottle | FPS throttling with `gvafpsthrottle` | `samples/gstreamer/gst_launch/gvafpsthrottle/` |
| lvm | Image embeddings generation with ViT/CLIP | `samples/gstreamer/gst_launch/lvm/` |
| license_plate_recognition | License plate recognition (detector + OCR) | `samples/gstreamer/gst_launch/license_plate_recognition/` |
| gvagenai | VLM usage with `gvagenai` | `samples/gstreamer/gst_launch/gvagenai/` |
| g3dradarprocess | Radar signal processing | `samples/gstreamer/gst_launch/g3dradarprocess/` |
| g3dlidarparse | LiDAR parsing pipeline | `samples/gstreamer/gst_launch/g3dlidarparse/` |
| gvarealsense | RealSense camera capture | `samples/gstreamer/gst_launch/gvarealsense/` |
| custom_postproc/detect | Custom detection post-processing library | `samples/gstreamer/gst_launch/custom_postproc/detect/` |
| custom_postproc/classify | Custom classification post-processing library | `samples/gstreamer/gst_launch/custom_postproc/classify/` |
| face_detection_and_classification_bins | Detection + classification using `processbin`, GPU/CPU VA memory paths | `samples/gstreamer/gst_launch/face_detection_and_classification_bins/` |
| motion_detect | Motion region detection (`gvamotiondetect`), ROI-restricted inference | `samples/gstreamer/gst_launch/motion_detect/` |
