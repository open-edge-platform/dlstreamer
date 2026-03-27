# Samples 2.0

Architecture 2.0 brings new samples separated into 3 categories by
programming models (*Direct Programming, GStreamer low-level, GStreamer
high-level*):

| Category | Folder | Sample(s) | #1 used | #2 used | #3 used |
|---|---|---|---|---|---|
| Direct Programming | samples/ffmpeg_openvino/ | decode_inference | No | No | No |
| Direct Programming | samples/ffmpeg_openvino/ | decode_resize_inference | Yes | Yes | No |
| Direct Programming | samples/ffmpeg_dpcpp/ | rgb_to_grayscale | Yes | Yes | No |
| GStreamer low-level | samples/gstreamer/gst_launch/face_detection_and_classification_bins/ | face_detection_and_classification_cpu.sh<br>face_detection_and_classification_gpu.sh | No | Yes | No |
| GStreamer low-level | samples/gstreamer/gst_launch/action_recognition/ | action_recognition.sh | No | Yes | Yes |
| GStreamer low-level | samples/gstreamer/gst_launch/instance_segmentation/ | instance_segmentation.sh | No | Yes | Yes |
| GStreamer high-level | samples/gstreamer/gst_launch | All other GStreamer samples<br>if set environment variable export DLSTREAMER_GEN=2 | No | No | Yes |
| GStreamer gva* (architecture 1.0) | samples/gstreamer/gst_launch | All GStreamer samples<br>using PV-quality gva* elements | No | No | No |
