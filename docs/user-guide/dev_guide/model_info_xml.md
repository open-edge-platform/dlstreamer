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
| `scale` | float | 255.0 | Divide input image values by ‘scale’ before mapping to model input tensor<br>(typically used when model was trained with input data normalized in &lt;0,1&gt; range). | range: [0.0, 1.0] |

You can also refer to
[OpenVINO™ Model API](https://github.com/open-edge-platform/model_api/blob/master/docs/source/guides/model-configuration.md)
for more information on the "model_info" section.
