# gvadetect

Performs object detection using SSD-like (including MobileNet-V1/V2 and
ResNet), YOLOv5 - YOLO11, YOLOX and FasterRCNN-like object detection
models.

```text
Pad Templates:
  SINK template: 'sink'
    Availability: Always
    Capabilities:
      video/x-raw
                format: { (string)BGRx, (string)BGRA, (string)BGR, (string)NV12, (string)I420 }
                  width: [ 1, 2147483647 ]
                height: [ 1, 2147483647 ]
              framerate: [ 0/1, 2147483647/1 ]
      video/x-raw(memory:DMABuf)
                format: { (string)DMA_DRM }
                  width: [ 1, 2147483647 ]
                height: [ 1, 2147483647 ]
              framerate: [ 0/1, 2147483647/1 ]
      video/x-raw(memory:VASurface)
                format: { (string)NV12 }
                  width: [ 1, 2147483647 ]
                height: [ 1, 2147483647 ]
              framerate: [ 0/1, 2147483647/1 ]
      video/x-raw(memory:VAMemory)
                format: { (string)NV12 }
                  width: [ 1, 2147483647 ]
                height: [ 1, 2147483647 ]
              framerate: [ 0/1, 2147483647/1 ]

  SRC template: 'src'
    Availability: Always
    Capabilities:
      video/x-raw
                format: { (string)BGRx, (string)BGRA, (string)BGR, (string)NV12, (string)I420 }
                  width: [ 1, 2147483647 ]
                height: [ 1, 2147483647 ]
              framerate: [ 0/1, 2147483647/1 ]
      video/x-raw(memory:DMABuf)
                format: { (string)DMA_DRM }
                  width: [ 1, 2147483647 ]
                height: [ 1, 2147483647 ]
              framerate: [ 0/1, 2147483647/1 ]
      video/x-raw(memory:VASurface)
                format: { (string)NV12 }
                  width: [ 1, 2147483647 ]
                height: [ 1, 2147483647 ]
              framerate: [ 0/1, 2147483647/1 ]
      video/x-raw(memory:VAMemory)
                format: { (string)NV12 }
                  width: [ 1, 2147483647 ]
                height: [ 1, 2147483647 ]
              framerate: [ 0/1, 2147483647/1 ]

Element has no clocking capabilities.
Element has no URI handling capabilities.

Pads:
  SINK: 'sink'
    Pad Template: 'sink'
  SRC: 'src'
    Pad Template: 'src'

Element Properties:
  batch-size          : Number of frames batched together for a single inference. If the batch-size is 0, then it will be set by default to be optimal for the device. Not all models support batching. Use model optimizer to ensure that the model has batching support.
                        flags: readable, writable
                        Unsigned Integer. Range: 0 - 1024 Default: 0
  cpu-throughput-streams: Deprecated. Use ie-config=CPU_THROUGHPUT_STREAMS=<number-streams> instead
                        flags: readable, writable, deprecated
                        Unsigned Integer. Range: 0 - 4294967295 Default: 0
  custom-postproc-lib : Path to the .so file defining custom model output converter. The library must implement the Convert function: void Convert(GstTensorMeta *outputTensors, const GstStructure *network, const GstStructure *params, GstAnalyticsRelationMeta *relationMeta);
                        flags: readable, writable
                        String. Default: null
  custom-preproc-lib  : Path to the .so file defining custom input image pre-processing
                        flags: readable, writable
                        String. Default: null
  device              : Target device for inference. Please see OpenVINO™ Toolkit documentation for list of supported devices.
                        flags: readable, writable
                        String. Default: "CPU"
  gpu-throughput-streams: Deprecated. Use ie-config=GPU_THROUGHPUT_STREAMS=<number-streams> instead
                        flags: readable, writable, deprecated
                        Unsigned Integer. Range: 0 - 4294967295 Default: 0
  ie-config           : Comma separated list of KEY=VALUE parameters for Inference Engine configuration. See OpenVINO™ Toolkit documentation for available parameters
                        flags: readable, writable
                        String. Default: ""
  inference-interval  : Interval between inference requests. An interval of 1 (Default) performs inference on every frame. An interval of 2 performs inference on every other frame. An interval of N performs inference on every Nth frame.
                        flags: readable, writable
                        Unsigned Integer. Range: 1 - 4294967295 Default: 1
  inference-region    : Identifier responsible for the region on which inference will be performed
                        flags: readable, writable
                        Enum "InferenceRegionType3" Default: 0, "full-frame"
                          (0): full-frame       - Perform inference for full frame
                          (1): roi-list         - Perform inference for roi list
  labels              : Array of object classes. It could be set as the following example: labels=<label1,label2,label3>
                        flags: readable, writable
                        String. Default: null
  labels-file         : Path to .txt file containing object classes (one per line)
                        flags: readable, writable
                        String. Default: null
  model               : Path to inference model network file
                        flags: readable, writable
                        String. Default: null
  model-instance-id   : Identifier for sharing a loaded model instance between elements of the same type. Elements with the same model-instance-id will share all model and inference engine related properties
                        flags: readable, writable
                        String. Default: null
  model-proc          : Path to JSON file with description of input/output layers pre-processing/post-processing
                        flags: readable, writable
                        String. Default: null
  name                : The name of the object
                        flags: readable, writable
                        String. Default: "gvadetect0"
  nireq               : Number of inference requests
                        flags: readable, writable
                        Unsigned Integer. Range: 0 - 1024 Default: 0
  no-block            : (Experimental) Option to help maintain frames per second of incoming stream. Skips inference on an incoming frame if all inference requests are currently processing outstanding frames
                        flags: readable, writable, deprecated
                        Boolean. Default: false
  object-class        : Filter for Region of Interest class label on this element input
                        flags: readable, writable
                        String. Default: null
  ov-extension-lib    : Path to the .so file defining custom OpenVINO operations.
                        flags: readable, writable
                        String. Default: null
  parent              : The parent of the object
                        flags: readable, writable
                        Object of type "GstObject"
  pre-process-backend : Select a pre-processing method (color conversion, resize and crop), one of 'ie', 'opencv', 'va', 'va-surface-sharing, 'vaapi', 'vaapi-surface-sharing'. If not set, it will be selected automatically: 'va' for VAMemory and DMABuf, 'ie' for SYSTEM memory.
                        flags: readable, writable
                        String. Default: ""
  pre-process-config  : Comma separated list of KEY=VALUE parameters for image processing pipeline configuration
                        flags: readable, writable
                        String. Default: ""
  qos                 : Handle Quality-of-Service events
                        flags: readable, writable
                        Boolean. Default: false
  reshape             : If true, model input layer will be reshaped to resolution of input frames (no resize operation before inference). Note: this feature has limitations, not all network supports reshaping.
                        flags: readable, writable
                        Boolean. Default: false
  reshape-height      : Height to which the network will be reshaped.
                        flags: readable, writable
                        Unsigned Integer. Range: 0 - 4294967295 Default: 0
  reshape-width       : Width to which the network will be reshaped.
                        flags: readable, writable
                        Unsigned Integer. Range: 0 - 4294967295 Default: 0
  scale-method        : Scale method to use in pre-preprocessing before inference. Only default and scale-method=fast (VAAPI based) supported in this element
                        flags: readable, writable
                        String. Default: null
  scheduling-policy   : Scheduling policy across streams sharing same model instance: throughput (select first incoming frame), latency (select frames with earliest presentation time out of the streams sharing same model-instance-id; recommended batch-size less than or equal to the number of streams)
                        flags: readable, writable
                        String. Default: null
  share-va-display-ctx: Feature allowing sharing VA Display context across inference elements
                        flags: readable, writable
                        Boolean. Default: true
  threshold           : Threshold for detection results. Only regions of interest with confidence values above the threshold will be added to the frame
                        flags: readable, writable
                        Float. Range: 0 - 1 Default: 0.5
```
