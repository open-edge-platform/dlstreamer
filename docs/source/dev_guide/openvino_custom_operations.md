# OpenVINO Custom Operations Support

## Overview

DL Streamer provides support for OpenVINO™ custom operations through the `ov-extension-lib` parameter. This feature enables the use of models with custom operations that are not natively supported by OpenVINO™ Runtime, by loading extension libraries that define these custom operations.

Custom operations may be required in two scenarios:

1. **New or rarely used operations** - Operations from frameworks (TensorFlow, PyTorch, ONNX, etc.) that are not yet supported in OpenVINO™
2. **User-defined operations** - Custom operations created specifically for a model using framework extension capabilities

The `ov-extension-lib` parameter is available in the following DL Streamer elements:

- `gvadetect` - Object detection
- `gvaclassify` - Object classification
- `gvainference` - Generic inference

## Prerequisites

Before using custom operations, you need:

1. **OpenVINO™ Extension Library** - A compiled `.so` file (on Linux) containing the implementation of custom operations
2. **Model with Custom Operations** - An OpenVINO™ IR model that uses the custom operations defined in the extension library

For information on creating OpenVINO™ extension libraries, refer to the [OpenVINO™ Extensibility documentation](https://docs.openvino.ai/2025/documentation/openvino-extensibility.html).

## Usage

### Basic Usage

To use a model with custom operations, specify the path to the extension library using the `ov-extension-lib` parameter:

```bash
gst-launch-1.0 filesrc location=input.mp4 ! decodebin3 ! \
    gvadetect model=model_with_custom_ops.xml \
              ov-extension-lib=/path/to/custom_operations.so \
              device=CPU ! \
    queue ! gvawatermark ! videoconvert ! autovideosink sync=false
```

### Parameter Details

**Property Name:** `ov-extension-lib`

**Type:** String (file path)

**Default:** `null` (no extension library loaded)

**Description:** Absolute or relative path to the `.so` file defining custom OpenVINO operations.

**Flags:** readable, writable

### Usage Examples

#### Example 1: Object Detection with Custom Operations

```bash
gst-launch-1.0 filesrc location=video.mp4 ! decodebin3 ! \
    gvadetect model=custom_detector.xml \
              ov-extension-lib=/opt/intel/openvino/custom_extensions/my_ops.so \
              device=CPU \
              batch-size=1 ! \
    queue ! gvawatermark ! videoconvert ! autovideosink sync=false
```

#### Example 2: Classification with Custom Operations

```bash
gst-launch-1.0 filesrc location=video.mp4 ! decodebin3 ! \
    gvadetect model=person-detection.xml device=CPU ! \
    gvaclassify model=custom_attributes.xml \
                ov-extension-lib=/home/user/extensions/attribute_ops.so \
                device=CPU \
                object-class=person ! \
    queue ! gvawatermark ! videoconvert ! autovideosink sync=false
```

#### Example 3: Generic Inference with Custom Operations

```bash
gst-launch-1.0 filesrc location=video.mp4 ! decodebin3 ! \
    gvainference model=custom_model.xml \
                ov-extension-lib=./extensions/libcustom.so \
                device=GPU ! \
    fakesink
```

## Related Documentation

- [Model Preparation Guide](model_preparation.md)
- [GStreamer Elements Reference](../elements/elements.md)
- [OpenVINO™ Documentation](https://docs.openvino.ai/)

## See Also

- [gvadetect element documentation](../elements/gvadetect.md)
- [gvaclassify element documentation](../elements/gvaclassify.md)
- [gvainference element documentation](../elements/gvainference.md)
- [Custom Processing Guide](custom_processing.md)
