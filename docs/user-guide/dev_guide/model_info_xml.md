# Model Info Section

OpenVINO™ Intermediate Representation (IR) includes an XML file with
the description of network topology as well as conversion and runtime
metadata.

If a `model_proc` file is not present, Deep Learning Streamer
parses the "model_info" section located at the end of the XML model file.

An example is shown in the code snippet below:

```xml
<rt_info>
    ...
    <model_info>
        <iou_threshold value="0.7" />
        <labels value="person bicycle ... " />
        <model_type value="yolo_v8" />
        <pad_value value="114" />
        <resize_type value="fit_to_window_letterbox" />
        <reverse_input_channels value="True" />
        <scale_values value="255" />
    </model_info>
</rt_info>
```

Deep Learning Streamer supports the following fields in the model
info section:

| Field | Type | Possible values or example | Description | Corresponding 'model-proc' key |
|---|---|---|---|---|
| `model_type` | string | <br>label<br>detection_output<br>yolo_v8<br><br> | The converter to parse output tensors and map to GStreamer meta data. | converter |
| `confidence_threshold` | float | [0.0, 1.0] | The confidence level to report inference results, typically depends on training accuracy. | threshold (command line param) |
| `iou_threshold` | float | [ 0.0, 1.0 ] | Threshold for non-maximum suppression (NMS) intersection over union (IOU) filtering. | iou_threshold |
| `multilabel` | boolean | <br>True<br>False<br><br> | Classification model predicts a set of labels per input image. | method=multi |
| `output_raw_scores` | boolean | <br>True<br>False<br><br> | Classification model outputs all non-normalized scores for all detected labels. | method=softmax |
| `labels` | string list | person bicycle … | List of labels for predicted object classes. | labels |
| `resize_type` | string | <br>crop<br>standard<br>fit_to_window<br>fit_to_window_letterbox<br><br> | Resize method to map input video images to model input tensor. | resize |
| `reverse_input_channels` | boolean | <br>True<br>False<br><br> | Convert input video image to RGB format (model trained with RGB images) | color_space=”RGB” |
| `intensity_mode` | string | scale_to_unit | Divide 8-bit pixel values by 255 before mean/std normalization. | range: [0.0, 1.0] |
| `mean_values` | string list | 0.485 0.456 0.406 | Three per-channel means, used together with three-channel `scale_values`. | mean |
| `scale_values` | string list | 255 or 0.229 0.224 0.225 | One scalar divisor, or three per-channel std divisors used together with `mean_values`. | scale or std |

## Input Normalization

For 8-bit image inputs, DL Streamer does not implicitly divide pixels by 255
when applying mean/std normalization. Without `intensity_mode="scale_to_unit"`,
three-channel mean and scale values apply directly in the pixel domain:
`(pixel - mean_values[channel]) / scale_values[channel]`.

For models requiring unit-domain mean/std, use:

```xml
<intensity_mode value="scale_to_unit" />
<mean_values value="0.485 0.456 0.406" />
<scale_values value="0.229 0.224 0.225" />
```

This computes `(pixel / 255 - mean_values[channel]) / scale_values[channel]`.
The final normalized values can be outside the 0-1 range. The equivalent
pixel-domain configuration, without `intensity_mode`, is:

```xml
<mean_values value="123.675 116.28 103.53" />
<scale_values value="58.395 57.12 57.375" />
```

For division by 255 alone, a single `scale_values` value of `255` is sufficient.
Do not combine it with `intensity_mode="scale_to_unit"` unless two divisions
by 255 are intended. Only the `scale_to_unit` intensity mode is supported;
this does not add support for other `intensity_*` settings.

You can also refer to
[OpenVINO™ Model API](https://github.com/open-edge-platform/model_api/blob/master/model_api/docs/source/guides/model-configuration.md)
for more information on the "model_info" section.
