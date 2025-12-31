# Model-proc File (legacy)

> **WARNING**:
>
> The functionality described here has been **deprecated**. Avoid using it
> to prevent dealing with a legacy solution. It will be maintained for
> some time to ensure backwards compatibility, but you should not use it
> in modern applications. The new method of model preparation is described
> in [Model Info Section](../dev_guide/model_info_xml.md)

## Table of Contents

- [model-proc Overview](#model-proc-overview)
- [Pre-processing description](#pre-processing-description)
  - [Pre-processing configuration](#pre-processing-configuration)
  - [Example](#example-of-input-preprocessing)
- [Post-processing description "output_postproc"](#post-processing-description-output_postproc)
  - [Post-processing configuration](#post-processing-configuration)
  - [Example](#example-of-output-post-processing)

## model-proc Overview

A model-proc file is a regular JSON file with pre- and post-processing
configuration.

As an example implementation, see the model-proc file for the emotions
recognition model shown in the code snippet below.

`input_preproc` set to `[]` indicates that images do not need to be
pre-processed with custom means before inference. They will be resized
without aspect-ratio preservation and their color space will be changed
to BGR.

`output_postproc` indicates whether the inference result is one of the strings
listed in `"labels"`.

See the
[samples/gstreamer/model_proc](https://github.com/open-edge-platform/dlstreamer/tree/master/samples/gstreamer/model_proc)
for examples of .json files using various models from
[Open Model Zoo](https://github.com/openvinotoolkit/open_model_zoo) and some public
models.

```javascript
{
    "json_schema_version": "2.2.0",
    "input_preproc": [],
    "output_postproc": [
      {
        "attribute_name": "emotion",
        "converter": "label",
        "method": "max",
        "labels": [
          "neutral",
          "happy",
          "sad",
          "surprise",
          "anger"
        ]
      }
    ]
}
```

This file has specific fields:

- `json_schema_version`. Service information needed by Deep
  Learning Streamer Pipeline Framework. **The
  latest version should be used: 2.2.0**.
- `input_preproc`. Describes how to process an input tensor before
  inference.
- `output_postproc`. Describes how to process inference results.

## Pre-processing description

### Pre-processing configuration

Pre-processing is described by the `input_preproc` key. The value of
`input_preproc` should contain an array of JSON objects with pre-processing
configuration. Each object describes operations for one input layer.
This array can be empty for models with one input layer. In such case,
tensors will be processed with the default tensor pre-processing (e.g., in
case of image input: resize without aspect-ratio preservation and color
space conversion to BGR). Input pre-processing configuration should be
described in a key-value format. Valid keys for `input_preproc` are
presented in the table below.

| Key |  | Possible values or example | Description | Supported by backend |
|---|---|---|---|---|
| layer_name |  | string | Neural network’s input layer name. | All |
| format |  | <br>image,<br>image_info,<br>sequence_index<br><br> | Input format for the layer with the specified name. In other words: “What to pre-process?”. Can be one of the following:<br><br>image - pre-process images;<br>image_info - pre-process additional layer with image info;<br>sequence_index - additional layer which is filled with ones<br><br> | All |
| params | resize | <br>no,<br>no-aspect-ratio,<br>aspect-ratio<br><br> | Resize an image to match input model layer dimensions. | <br>opencv,<br>va,<br>va-surface-sharing<br><br> |
| params | crop | <br>central,<br>top_left,<br>top_right,<br>bottom_left,<br>bottom_right<br><br> | Crop image to fit input model layer dimensions. | <br>opencv,<br>va,<br>va-surface-sharing<br><br> |
| params | color_space | <br>RGB,<br>BGR,<br>GRAYSCALE<br><br> | Convert image to targeted color space. | <br>opencv,<br>va,<br>va-surface-sharing (BGR only)<br><br> |
| params | range | [ 0.0, 1.0 ] | Normalize input image values to be in the specified range. | <br>opencv,<br>va-surface-sharing<br>ie<br><br> |
| params | mean | [ 0.485, 0.456, 0.406 ] | JSON arrays of doubles. Size of arrays should be equal to the number of channels of the input image. | <br>opencv,<br>va<br><br> |
| params | std | [ 0.229, 0.224, 0.225 ] | JSON arrays of doubles. Size of arrays should be equal to the number of channels of the input image. | <br>opencv,<br>va<br><br> |
| params | padding | <br>{<br><br>"stride_x": 8,<br>"stride_y": 8<br><br>}<br><br> | A JSON object with stride in pixels from image boundaries (also can be set with `stride_x` and `stride_y`) | <br>opencv,<br>va,<br>va-surface-sharing<br><br> |

These operations will be performed in the order listed above, regardless
of how they are listed in the `model-proc` file. If no operation is specified,
it will not be performed.

For the best performance of "normalize" and "color convert" operations (BGR ->
RGB), it is worth excluding and adding the corresponding layers in the IR
model, using the appropriate model optimizer parameters.

### Example of Input Preprocessing

Below is an example of `input_preproc` and its parameters:

```javascript
...
"input_preproc": [
    {
        "layer_name": "seq_ind",
        "format": "sequence_index"
    },
    {
        "format": "image_info",
        "layer_name": "image_info",
        "params": {
            "scale": 1.0
        }
    },
    {
        "layer_name": "input",
        "precision": "FP32",
        "format": "image",
        "params": {
            "resize": "aspect-ratio",
            "color_space": "BGR",
            "range": [ 0.0, 1.0 ],
            "mean": [ 0.485, 0.456, 0.406 ],
            "std": [ 0.229, 0.224, 0.225 ],
            "padding": {
                "stride_x": 8,
                "stride_y": 8
            }
        }
    }
], ...
```

## Post-processing description (`output_postproc`)

### Post-processing configuration

Post-processing is described similarly to pre-processing by
the `output_postproc` key. The key should contain an array of JSON
objects with post-processing configuration. Each object describes
operations for one output layer. This array can be empty for models with
one output layer. In such case, Pipeline Framework will detect the name
of the output layer and set a default converter for a specified Pipeline
Framework inference element.

Pipeline Framework uses converters to transform the output blob into a
suitable form. Currently, several converters are supported and its usage
depends on what model was used (whether detection, classification, or
any other model).

Some converters may require the name of the output layer (usually
applicable with `gvaclassify`). This can be done with the following
fields:

- `layer_name` or `layer_names` - define for which layer
    the transformation is applicable. Should be defined for one of them.
- `attribute_name` - the name of the output tensor for post-processing result.

**Default behavior**: The default output transformation method depends on
the used element. If the behavior described by the table is suitable,
model-proc can be ignored.

| Element        | Default converter                                             |
|----------------|---------------------------------------------------------------|
| `gvaclassify`  | raw_data_copy                                                 |
| `gvadetect `   | detection_output and boxes_labels (depends on model’s output) |
| `gvainference` | raw_data_copy                                                 |

The table below contains currently supported converters. Values in the
*Converter* column contain a link to a `model-proc` file with this
converter. The last column contains a link to a model for which this
converter can be applied.

| Converter | Description | Applied to output layer like |
|---|---|---|
| **For gvainference:** |  |  |
| raw_data_copy | Attach tensor data from all output layers in raw binary format and optionally tag the data format. | Basically any inference model |
| **For gvadetect:** |  |  |
| [detection_output](https://github.com/open-edge-platform/dlstreamer/blob/master/samples/gstreamer/model_proc/intel/face-detection-retail-0004.json) | Parse the output blob produced by an object detection neural network with the DetectionOutput IR output layer’s type. Output is RegionOfInterest.<br>- labels - an array of strings representing labels or a path to a file with each label per line.<br> | [ssdlite_mobilenet_v2](https://github.com/openvinotoolkit/open_model_zoo/tree/master/models/public/ssdlite_mobilenet_v2#output)<br>[person-vehicle-bike-detection-crossroad-0078](https://github.com/openvinotoolkit/open_model_zoo/tree/master/models/intel/person-vehicle-bike-detection-crossroad-0078#outputs)<br> |
| [boxes_labels](https://github.com/open-edge-platform/dlstreamer/blob/master/samples/gstreamer/model_proc/intel/face-detection-0205.json) | Parse the output blob produced by an object detection neural network with two output layers: boxes and labels. Output is RegionOfInterest.<br>- labels - an array of strings representing labels or a path to a file with each label per line.<br> | [face-detection-0205](https://github.com/openvinotoolkit/open_model_zoo/tree/master/models/intel/face-detection-0205#outputs)<br>[person-vehicle-bike-detection-2004](https://github.com/openvinotoolkit/open_model_zoo/tree/master/models/intel/person-vehicle-bike-detection-2004#outputs)<br> |
| [yolo_v2](https://github.com/open-edge-platform/dlstreamer/blob/master/samples/gstreamer/model_proc/public/yolo-v2-tf.json) | Parse the output blob produced by an object detection neural network with the YOLO v2 architecture. Output is RegionOfInterest.<br><br>- labels - an array of strings representing labels or a path to a file with each label per line;<br>- classes - an integer number of classes;<br>- `bbox_number_on_cell` - the box count that can be predicted in each cell;<br>- anchors - box size (x, y) is multiplied by this value. len(anchors) == `bbox_number_on_cell * 2 * number_of_outputs`;<br>- `cells_number` - an image is split on cells with this number (if model’s input layer has non-square form set cells_number_x &amp; cells_number_y instead `cells_number`);<br>- `iou_threshold` - the parameter for NMS.<br><br> | [yolo-v2-tf](https://github.com/openvinotoolkit/open_model_zoo/tree/master/models/public/yolo-v2-tf#output)<br>[yolo-v2-tiny-tf](https://github.com/openvinotoolkit/open_model_zoo/tree/master/models/public/yolo-v2-tiny-tf#output)<br> |
| [yolo_v3](https://github.com/open-edge-platform/dlstreamer/blob/master/samples/gstreamer/model_proc/public/yolo-v3-tf.json) | Parse the output blob produced by an object detection neural network with the YOLO v3 architecture.<br><br>- labels - an array of strings representing labels or a path to a file with each label per line;<br>- classes - an integer number of classes;<br>- `bbox_number_on_cell` - the box count that can be predicted in each cell;<br>- anchors - the box size (x, y) is multiplied by this value. `len(anchors) == bbox_number_on_cell * 2 * number_of_outputs`;<br>- `cells_number` - an image is split on cells with this number (if model’s input layer has non-square form set `cells_number_x` &amp; `cells_number_y` instead `cells_number`);<br>- `iou_threshold` - the parameter for NMS;<br>- `masks`- determines which anchors are related to which output layer;<br>- `output_sigmoid_activation` - performs a sigmoid operation for coordinates and confidence.<br><br>See more details [there](./how_to_create_model_proc_file.md#build-model-proc-for-detection-model-with-advance-post-processing).<br> | [yolo-v3-tf](https://github.com/openvinotoolkit/open_model_zoo/tree/master/models/public/yolo-v3-tf#output)<br>[yolo-v4-tf](https://github.com/openvinotoolkit/open_model_zoo/tree/master/models/public/yolo-v4-tf#output)<br> |
| heatmap_boxes | Parse the output blob produced by a network with DBNet architecture which is in the form of a probability heatmap. Output is RegionOfInterest.<br><br>- `minimum_side` - Any detected box with its smallest side &lt; `minimum_side` will be dropped;<br>- `binarize_threshold` - a threshold value for OpenCV binary image thresholding, expected in the [0.0, 255.0] range ;<br><br> |  |
| **For gvaclassify:** |  |  |
| [text](https://github.com/open-edge-platform/dlstreamer/blob/master/samples/gstreamer/model_proc/intel/age-gender-recognition-retail-0013.json) | Transform the output tensor to text.<br><br>- `text_scale` - scales data by this number;<br>- `text_precision` - sets precision for textual representation.<br><br> | [age-gender-recognition-retail-0013](https://github.com/openvinotoolkit/open_model_zoo/tree/master/models/intel/age-gender-recognition-retail-0013#outputs) |
| [label](https://github.com/open-edge-platform/dlstreamer/blob/master/samples/gstreamer/model_proc/intel/vehicle-attributes-recognition-barrier-0039.json) | Put an appropriate label for result.<br><br>- method: one of `[max, index, compound (threshold is required. 0.5 is default)]`;<br>- `labels` - an array of strings representing labels or a path to a file with each label per line.<br><br> | [emotions-recognition-retail-0003](https://github.com/openvinotoolkit/open_model_zoo/tree/master/models/intel/age-gender-recognition-retail-0013#outputs)<br>[license-plate-recognition-barrier-0007](https://github.com/openvinotoolkit/open_model_zoo/tree/master/models/intel/license-plate-recognition-barrier-0001#outputs)<br>[person-attributes-recognition-crossroad-0230](https://github.com/openvinotoolkit/open_model_zoo/tree/master/models/intel/person-attributes-recognition-crossroad-0230#outputs)<br> |
| [keypoints_hrnet](https://github.com/open-edge-platform/dlstreamer/blob/master/samples/gstreamer/model_proc/public/single-human-pose-estimation-0001.json) | Parse the output blob produced by a network with the HRNet architecture. The output tensor will have an array of key points.<br><br>- `point_names` - an array of strings with the name of the points;<br>- `point_connections` - an array of strings with points connection. The length should be even.<br><br> | [single-human-pose-estimation-0001](https://github.com/openvinotoolkit/open_model_zoo/tree/master/models/public/single-human-pose-estimation-0001#outputs) |
| [keypoints_openpose](https://github.com/open-edge-platform/dlstreamer/blob/master/samples/gstreamer/model_proc/intel/human-pose-estimation-0001.json) | Parse the output blob produced by a network with OpenPose architecture. The output tensor will have an array of key points.<br><br>`point_names` - an array of strings with the name of the points;<br>`point_connections` - an array of strings with points connection. The length should be even.<br><br> | [human-pose-estimation-0001](https://github.com/openvinotoolkit/open_model_zoo/tree/master/models/intel/human-pose-estimation-0001#outputs) |
| keypoints_3d | Parse the output blob produced by a network with HRNet architecture. The output tensor will have an array of 3D-key points.<br><br>- `point_names` - an array of strings with the name of the points;<br>- `point_connections` - an array of strings with points connection. The length should be even.<br><br> | None |
| **For gvaaudiodetect:** |  |  |
| [audio_labels](https://github.com/open-edge-platform/dlstreamer/blob/master/samples/gstreamer/model_proc/public/aclnet.json) | Output tensor - audio detections tensor.<br><br>- layer_name - name of the layer to process;<br>- labels - an array of JSON objects with index, label, threshold fields.<br><br> | [aclnet](https://github.com/openvinotoolkit/open_model_zoo/blob/master/models/public/aclnet/README.md#output) |

### Example of Output Post-processing

Below is an example of `output_postproc` and its parameters:

> **NOTE:** This configuration cannot be used for any model.

```javascript
...
"output_postproc": [
   {
       "converter": "raw_data_copy"
   },
   {
       "converter": "detection_output",
       "labels": [
           "background", "face"
       ]
   },
   {
       "converter": "yolo_v2",
       "classes": 20,
       "do_cls_softmax": true,
       "output_sigmoid_activation": true,
       "anchors": [
           1.08, 1.19, 3.42, 4.41, 6.63, 11.38, 9.42, 5.11, 16.62, 10.52
       ],
       "iou_threshold": 0.5,
       "bbox_number_on_cell": 5,
       "cells_number": 13,
       "labels": [
           "aeroplane", "bicycle", "bird", "boat", "bottle", "bus", "car",
           ...
           "pottedplant", "sheep", "sofa", "train", "tvmonitor"
       ]
   },
   {
       "converter": "text",
       "text_scale": 100.0,
       "text_precision": 0
       "layer_name": "age_conv3",
       "attribute_name": "age",
   },
   {
       "converter": "label",
       "labels": [
           "Female", "Male"
       ],
       "method": "max"
       "layer_name": "prob",
       "attribute_name": "gender",
   },
   {
       "converter": "label",
       "labels": "/opt/data/color_labels.txt",
       "method": "max"
       "layer_name": "color",
       "attribute_name": "color",
   },
   {
       "converter": "keypoints_hrnet",
       "point_names": [
           "nose", "eye_l", "eye_r", "ear_l", "ear_r"
       ],
       "point_connections": [
           "nose", "eye_l", "nose", "eye_r", "eye_l", "ear_l", "eye_r", "ear_r"
       ]
   },
   {
       "converter": "keypoints_openpose",
       "point_names": [
           "nose", "eye_l", "eye_r", "ear_l", "ear_r"
       ],
       "point_connections": [
           "nose", "eye_l", "nose", "eye_r", "eye_l", "ear_l", "eye_r", "ear_r"
       ]
   },
   {
       "layer_name": "output",
       "converter": "audio_labels",
       "labels": [
           {
               "index": 0,
               "label": "Dog",
               "threshold": 0.0
           },
           ...
           {
               "index": 52,
               "label": "Speech",
               "threshold": 0.0
           }
       ]
   }
]
...
```

<!--hide_directive
:::{toctree}
:maxdepth: 2

how_to_create_model_proc_file
:::
hide_directive-->
