# Video streams from multiple cameras (gst-launch command line)

This sample demonstrates how to construct multi-stream pipeline via `gst-launch-1.0` command-line utility using detection and classification models.
It combines four pipelines. By default, the first two streams run on NPU [Intel® Core™ Ultra processors] and the other two using the GPU device.

## How It Works
This sample utilizes GStreamer command-line tool `gst-launch-1.0` which can build and run GStreamer pipeline described in a string format.
The string contains a list of GStreamer elements separated by exclamation mark `!`, each element may have properties specified in the format `property`=`value`.

> **NOTE**: Before running, download the yolov8s model to `$MODELS_PATH/public/yolov8s/FP16/`.
Please follow instruction: [Detection with Yolo](./gst_launch/detection_with_yolo/README.md) how to download Yolov8s model.

This sample builds for GStreamer a pipeline of the following elements:
* `filesrc`
* `decodebin3` for video decoding
* `videoconvert` for converting video frame into different color formats
* [gvadetect](../../../../docs/source/elements/gvadetect.md) uses for full-frame object detection and marking objects with labels
* [gvawatermark](../../../../docs/source/elements/gvawatermark.md) for points and visualization of their connections
* `autovideosink` for rendering output video into screen
> **NOTE**: Each of the two pipelines can run on CPU or GPU or NPU.
> **NOTE**: `sync=false` property in `autovideosink` element disables real-time synchronization so pipeline runs as fast as possible

## Sample
An example run with NPU device for the first two video streams and GPU device for two other streams using the same input video mp4 file.

```sh
./multi_stream_sample.sh video.mp4
```

Another example uses CPU device for the first two streams and GPU for two other streams using the same input video mp4 file.

```sh
./multi_stream_sample.sh video.mp4 CPU GPU
```

Next example uses CPU device and YOLOv8 model for the first two streams and GPU and YOLOv9 model for two other streams using the same input video mp4 file.

```sh
./multi_stream_sample.sh video.mp4 CPU GPU yolov8s yolov9c
```

## Multi-stream Pipeline Templates
This section lists example command line templates to constructs multi-stream pipelines.

Please note multi-stream examples include 'queue' element after gvadetect or gvaclassify.
This is required to achieve optimal performance in multi-stream / multi-threaded deployments.

The first pipeline takes 4 input streams and maps them to AI model instances.
The first two streams use inference instance running on the NPU device ('model-instance-id=inf0') and two other streams run on GPU ('model-instance-id=inf1').
Please note this example uses exact same AI model for both NPU and GPU, yet one can construct pipelines with different models.

```sh
gst-launch-1.0 \
filesrc location=${INPUT_VIDEO_FILE_1} ! decodebin3 ! vaapipostproc ! video/x-raw(memory:VASurface) ! \
gvadetect model=${DETECTION_MODEL} device=NPU pre-process-backend=ie nireq=4 model-instance-id=inf0 ! queue ! \
gvawatermark ! gvafpscounter ! vaapih264enc ! h264parse ! mp4mux ! filesink location=${OUTPUT_VIDEO_FILE_1} \
filesrc location=${INPUT_VIDEO_FILE_2} ! decodebin3 ! vaapipostproc ! video/x-raw(memory:VASurface) ! \
gvadetect model=${DETECTION_MODEL} device=NPU pre-process-backend=ie nireq=4 model-instance-id=inf0 ! queue ! \
gvawatermark ! gvafpscounter ! vaapih264enc ! h264parse ! mp4mux ! filesink location=${OUTPUT_VIDEO_FILE_2} \
filesrc location=${INPUT_VIDEO_FILE} ! decodebin3 ! vaapipostproc ! video/x-raw(memory:VASurface) !
gvadetect model=${$DETECTION_MODEL_3} device=GPU pre-process-backend=vaapi-surface-sharing nireq=4 model-instance-id=inf1 ! queue ! \
gvawatermark ! gvafpscounter ! vaapih264enc ! h264parse ! mp4mux ! filesink location=${OUTPUT_VIDEO_FILE_3} \
filesrc location=${INPUT_VIDEO_FILE_4} ! decodebin3 vaapipostproc ! video/x-raw(memory:VASurface) !
gvadetect model=${DETECTION_MODEL} device=GPU pre-process-backend=vaapi-surface-sharing nireq=4 model-instance-id=inf1 ! queue ! \
gvawatermark ! gvafpscounter ! vaapih264enc ! h264parse ! mp4mux ! filesink location=${OUTPUT_VIDEO_FILE_4}
```

The next pipeline illustrates how to construct a pipeline with multiple AI models and a single video stream.
In addition, this pipeline runs AI inference every 3 frames ('inference-interval=3') and uses 'gvatrack' to keep analytics results for non-inferenced frames.
The example also batches inference requests ('batch-size=8') to maximize AI model throughput at the expense of single-request latency.

```sh
gst-launch-1.0 \
filesrc location=${INPUT_VIDEO_FILE} ! video/x-h265 ! h265parse ! video/x-h265 ! vaapih265dec ! video/x-raw(memory:VASurface) ! \
gvadetect inference-interval=3 model=${DETECTION_MODEL} device=GPU nireq=4 batch-size=8 ie-config=PERFORMANCE_HINT=THROUGHPUT pre-process-backend=vaapi-surface-sharing ! queue ! \
gvatrack tracking-type=short-term-imageless ! queue ! \
gvaclassify inference-interval=3 model=${CLASSIFICATION_MODEL_1} device=GPU nireq=4 batch-size=8 ie-config=PERFORMANCE_HINT=THROUGHPUT pre-process-backend=vaapi-surface-sharing ! queue ! \
gvaclassify inference-interval=3 model=${CLASSIFICATION_MODEL_2} device=GPU nireq=4 batch-size=8 ie-config=PERFORMANCE_HINT=THROUGHPUT pre-process-backend=vaapi-surface-sharing ! queue ! \
gvawatermark ! gvafpscounter ! vaapih264enc ! h264parse ! mp4mux ! filesink location=${OUTPUT_VIDEO_FILE}
```

The last pipeline combines multi-stream and multi-model execution.

```sh
gst-launch-1.0 \
filesrc location=${INPUT_VIDEO_FILE_1} ! video/x-h265 ! h265parse ! video/x-h265 ! vaapih265dec ! video/x-raw(memory:VASurface) ! \
gvadetect model=${DETECTION_MODEL} device=GPU model-instance-id=inf0 nireq=4 batch-size=8 ie-config=PERFORMANCE_HINT=THROUGHPUT pre-process-backend=vaapi-surface-sharing ! queue ! \
gvaclassify model=${CLASSIFICATION_MODEL_1} device=GPU model-instance-id=inf1 nireq=4 batch-size=8 ie-config=PERFORMANCE_HINT=THROUGHPUT pre-process-backend=vaapi-surface-sharing ! queue ! \
gvaclassify model=${CLASSIFICATION_MODEL_2} device=GPU model-instance-id=inf2 nireq=4 batch-size=8 ie-config=PERFORMANCE_HINT=THROUGHPUT pre-process-backend=vaapi-surface-sharing ! queue ! \
gvawatermark ! gvafpscounter ! vaapih264enc ! h264parse ! mp4mux ! filesink location=${OUTPUT_VIDEO_FILE_1} \
filesrc location=${INPUT_VIDEO_FILE_2} ! video/x-h265 ! h265parse ! video/x-h265 ! vaapih265dec ! video/x-raw(memory:VASurface) ! \
gvadetect model=${DETECTION_MODEL} device=GPU model-instance-id=inf0 nireq=4 batch-size=8 ie-config=PERFORMANCE_HINT=THROUGHPUT pre-process-backend=vaapi-surface-sharing ! queue ! \
gvaclassify model=${CLASSIFICATION_MODEL_1} device=GPU model-instance-id=inf1 nireq=4 batch-size=8 ie-config=PERFORMANCE_HINT=THROUGHPUT pre-process-backend=vaapi-surface-sharing ! queue ! \
gvaclassify model=${CLASSIFICATION_MODEL_2} device=GPU model-instance-id=inf2 nireq=4 batch-size=8 ie-config=PERFORMANCE_HINT=THROUGHPUT pre-process-backend=vaapi-surface-sharing ! queue ! \
gvawatermark ! gvafpscounter ! vaapih264enc ! h264parse ! mp4mux ! filesink location=${OUTPUT_VIDEO_FILE_2}
```

## See also
* [Samples overview](../../README.md)