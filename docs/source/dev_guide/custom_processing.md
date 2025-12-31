# Custom Processing

The DL model inference in general works with tensor data on input and
output. The tensor is an abstract N-dimension array, which in GStreamer inference
plugins is stored under frame [Metadata](./metadata.md) using a flexible
[GstStructure](https://gstreamer.freedesktop.org/documentation/gstreamer/gststructure.html)
key-value container.
The **GVA::Tensor** C++ class with
[header-only implementation](https://github.com/open-edge-platform/dlstreamer/tree/master/include/dlstreamer/gst/videoanalytics/tensor.h#L38)
helps C++ applications access the tensor data.

The integration of DL model inference into real application typically
involves model specific pre-processing and post-processing logic.
GStreamer inference plugins support pre-/post-processing for many
popular model topologies and use-cases, configurable via JSON file
format, as described in the [model preparation](./model_preparation.md)
guide.

If a specific model pre-/post-processing is not supported, there are several
options to customize GStreamer pipeline or offload processing logic to an
application:

1. Consume tensor data and parse/convert/process it on application side.
2. Set a C/Python callback in the middle of the GStreamer pipeline.
3. Insert the [gvapython](../elements/gvapython.md) element and provide a Python
   callback function.
4. Insert a new GStreamer element implemented in C/C++;
5. Modify source code of
   [gvadetect](../elements/gvadetect.md)/[gvaclassify](../elements/gvaclassify.md)
   elements.
6. Create a custom post-processing library.

For models with custom operations not natively supported by OpenVINO™, see
[OpenVINO Custom Operations](./openvino_custom_operations.md).

See the sections below for more details on these options.

## 1. Consume tensor data and parse/convert it on application side

This option works in most use cases except

- Converted/parsed data required by a downstream element (For example,
  object detection → classification chain.)
- Pipeline constructed and executed by `gst-launch` command-line
  utility, not by a C/C++/Python application

The C/C++ application can either:

- Set a pad probe callback on one of elements at the end of the pipeline
  (after all metadata is attached to the frame). Refer to the callback
  mechanism documented
  [by GStreamer framework](https://gstreamer.freedesktop.org/documentation/application-development/advanced/pipeline-manipulation.html#data-probes)
- Insert the `appsink` element at the end of the pipeline and utilize
  the appsink functions and signals for GstBuffer and metadata
  consumption

The pad probe callback is demonstrated in the
[draw_face_attributes](https://github.com/open-edge-platform/dlstreamer/blob/master/samples/gstreamer/cpp/draw_face_attributes/main.cpp) C++ sample.

## 2. Set C/Python callback in the middle of GStreamer pipeline

Similarly to cases in
[section 1](#1-consume-tensor-data-and-parseconvert-it-on-application-side),
the pad probe callback could be set in the middle of pipeline.

Note that GstBuffer on source pad (output pad) of all inference elements
is guaranteed to be writable (`gst_buffer_is_writable` returns `true`), so
an application specific C++/Python callback or a custom element can
attach/modify `GstVideoRegionOfInterestMeta` or other metadata to the
GstBuffer.

This enables
[gvainference](../elements/gvainference.md) → [gvaclassify](../elements/gvaclassify.md)
pipelines, where `gvainference` runs an object detection model with a custom output layer
format. A C/Python callback inserted by the app between `gvainference` and `gvaclassify` can
parse tensor data in `GvaTensorMeta` into a list of objects (a bounding box
with attributes), and attach a new `GstVideoRegionOfInterestMeta` to video
frame for further object classification by the `gvaclassify` downstream element.

## 3. Insert gvapython element and provide Python callback function

The advantage of this option is applicability for any application type,
including the `gst-launch` utility.

The [gvapython element](../elements/gvapython.md) takes reference to user provided Python
script with a function to be called on every frame processing.

The callback function can attach/modify metadata as demonstrated in the
[sample](https://github.com/dlstreamer/dlstreamer/tree/master/samples/gstreamer/gst_launch/gvapython/face_detection_and_classification).

## 4. Insert new GStreamer element implemented on C/C++

Refer to the
[GStreamer documentation](https://gstreamer.freedesktop.org/documentation/application-development/basics/elements.html?gi-language=c) and samples to learn how to implement a new GStreamer element and register
a GStreamer plugin.

If the frame processing function is implemented in C++, it can utilize the
[GVA::Tensor](https://github.com/open-edge-platform/dlstreamer/blob/master/include/dlstreamer/gst/videoanalytics/tensor.h#L38)
helper class.

## 5. Modify source code of post-processors for gvadetect/gvaclassify elements

You can add new or modify any suitable existing
[post-processor](https://github.com/open-edge-platform/dlstreamer/blob/master/src/monolithic/gst/inference_elements/common/post_processor/blob_to_meta_converter.cpp)
for `gvadetect`/`gvaclassify` elements.

## 6. Create custom post-processing library

For advanced custom post-processing scenarios, you can create a separate
dynamic library that implements the post-processing logic without
modifying the DL Streamer source code. This approach provides
flexibility and modularity while maintaining clean separation between
the core framework and custom processing logic.

Practical examples of implementations are demonstrated in the
[sample](https://github.com/open-edge-platform/dlstreamer/tree/master/samples/gstreamer/gst_launch/custom_postproc).

**Important Requirements**

Your custom library must use the **GStreamer Analytics Library**, which
provides standardized metadata structures for AI/ML results. The library
implements structures such as `GstTensorMeta`,
`GstAnalyticsRelationMeta`, `GstAnalyticsODMtd`, `GstAnalyticsClsMtd`,
and others.

For more information about the Analytics metadata library, refer
to the
[GStreamer Analytics Documentation](https://gstreamer.freedesktop.org/documentation/analytics/index.html?gi-language=c).

**Current Support Limitations**


At this time, only **detection** and **classification** tasks are supported:

- **Object Detection** (`GstAnalyticsODMtd`) - works only with the
   `gvadetect` element (see [*Detection* sample][detection_sample]).

   [detection_sample]: https://github.com/open-edge-platform/dlstreamer/tree/master/samples/gstreamer/gst_launch/custom_postproc/detect/

- **Classification** (`GstAnalyticsClsMtd`) - works with both the
  [gvadetect](../elements/gvadetect.md) and
  [gvaclassify](../elements/gvaclassify.md) elements (see [*Classification* sample][classify_sample]).

  [classify_sample]: https://github.com/open-edge-platform/dlstreamer/tree/master/samples/gstreamer/gst_launch/custom_postproc/classify/

**Implementation Requirements**

Your custom library must export a `Convert` function with the following
signature:

```c
void Convert(GstTensorMeta *outputTensors,
             const GstStructure *network,
             const GstStructure *params,
             GstAnalyticsRelationMeta *relationMeta);
```

Where:

- `outputTensors` - contains output tensor data from the model inference.
- `network` - is model metadata including labels and input dimensions.
- `params` - are processing parameters, like confidence thresholds.
- `relationMeta` - is an output structure used for attaching results.

**Important Notes:**

- Each model output layer has a separate `GstTensor` contained within
  one `GstTensorMeta`. Tensors from individual layers can be
  identified by their `GstTensor` IDs.
- Regardless of the `batch-size` setting in the `gvadetect` or
  `gvaclassify` elements, the output tensors from the model are always
  passed to the `Convert` function in an **unbatched** format (i.e.,
  with the batch dimension equal to 1).

**Usage in GStreamer Pipeline**

Use the `custom-postproc-lib` parameter directly in DLS elements
(`gvadetect` or `gvaclassify`):

```bash
gst-launch-1.0 videotestsrc ! gvadetect \
  model=/path/to/model.xml \
  custom-postproc-lib=/path/to/your/libcustom_postproc.so ! \
  ...
```

**Example Implementation**

Below are examples of custom post-processing libraries for object detection
and classification use cases.

**Example 1: Object Detection**

```c
#include <gst/gst.h>
#include <gst/analytics/analytics.h>
#include <stdexcept>
#include <vector>

extern "C" void Convert(GstTensorMeta *outputTensors,
                        const GstStructure *network,
                        const GstStructure *params,
                        GstAnalyticsRelationMeta *relationMeta) {

    // Get output tensor(s).
    const GstTensor *tensor = gst_tensor_meta_get(outputTensors, 0);
    size_t dims_size;
    size_t *dims = gst_tensor_get_dims(gst_tensor_copy(tensor), &dims_size);

    // Get network metadata.
    size_t input_width = 0, input_height = 0;
    gst_structure_get_uint64(network, "image_width", &input_width);
    gst_structure_get_uint64(network, "image_height", &input_height);

    // Get processing parameters.
    double confidence_threshold = 0.5;
    gst_structure_get_double(params, "confidence_threshold", &confidence_threshold);

    // Get class labels.
    std::vector<std::string> labels;
    const GValue *labels_value = gst_structure_get_value(network, "labels");
    if (labels_value && G_VALUE_HOLDS(labels_value, GST_TYPE_ARRAY)) {
        int n_labels = gst_value_array_get_size(labels_value);
        for (int i = 0; i < n_labels; ++i) {
            const GValue *item = gst_value_array_get_value(labels_value, i);
            if (G_VALUE_HOLDS_STRING(item))
                labels.push_back(g_value_get_string(item));
        }
    }

    // Map tensor data to access raw model output.
    float *data = nullptr;
    GstMapInfo map;
    if (gst_buffer_map(tensor->data, &map, GST_MAP_READ)) {
        data = reinterpret_cast<float *>(map.data);
        gst_buffer_unmap(tensor->data, &map);
    } else {
        throw std::runtime_error("Failed to map tensor data.");
    }

    // Process model output according to your specific model format.
    // Parse detection results: bounding boxes, confidence scores, class IDs.
    // Apply confidence thresholding and NMS if needed.
    // ...

    // For each detected object, add object detection metadata.
    int x = 100, y = 50, w = 200, h = 150;  // Example coordinates
    float confidence = 0.85;                // Example confidence
    size_t class_id = 0;                   // Example class index

    GQuark label_quark = g_quark_from_string(labels[class_id].c_str());

    GstAnalyticsODMtd od_mtd;
    if (!gst_analytics_relation_meta_add_od_mtd(relationMeta, label_quark,
                                               x, y, w, h, confidence, &od_mtd)) {
        throw std::runtime_error("Failed to add object detection metadata.");
    }
}
```

**Example 2: Classification**

```c
#include <gst/gst.h>
#include <gst/analytics/analytics.h>
#include <algorithm>
#include <stdexcept>
#include <vector>

extern "C" void Convert(GstTensorMeta *outputTensors,
                        const GstStructure *network,
                        const GstStructure *params,
                        GstAnalyticsRelationMeta *relationMeta) {

    // Get classification output tensor.
    const GstTensor *tensor = gst_tensor_meta_get(outputTensors, 0);
    size_t dims_size;
    size_t *dims = gst_tensor_get_dims(gst_tensor_copy(tensor), &dims_size);

    size_t num_classes = dims[dims_size - 1];

    // Get network metadata.
    size_t input_width = 0, input_height = 0;
    gst_structure_get_uint64(network, "image_width", &input_width);
    gst_structure_get_uint64(network, "image_height", &input_height);

    // Specify confidence threshold.
    double confidence_threshold = 0.5;

    // Get class labels.
    std::vector<std::string> labels;
    const GValue *labels_value = gst_structure_get_value(network, "labels");
    if (labels_value && G_VALUE_HOLDS(labels_value, GST_TYPE_ARRAY)) {
        int n_labels = gst_value_array_get_size(labels_value);
        for (int i = 0; i < n_labels; ++i) {
            const GValue *item = gst_value_array_get_value(labels_value, i);
            if (G_VALUE_HOLDS_STRING(item))
                labels.push_back(g_value_get_string(item));
        }
    }

    // Map tensor data to access raw model output.
    float *data = nullptr;
    GstMapInfo map;
    if (gst_buffer_map(tensor->data, &map, GST_MAP_READ)) {
        data = reinterpret_cast<float *>(map.data);
        gst_buffer_unmap(tensor->data, &map);
    } else {
        throw std::runtime_error("Failed to map tensor data.");
    }

    // Process classification output according to your model format.
    // Apply softmax, find top-k classes, or other post-processing.
    // ...

    // Example: Find a class with the highest score.
    size_t best_class_id = 0;
    float best_confidence = 0.8;  // Example confidence score

    if (best_confidence > confidence_threshold && best_class_id < labels.size()) {
        std::string label = labels[best_class_id];
        GQuark label_quark = g_quark_from_string(label.c_str());

        // Add classification metadata.
        GstAnalyticsClsMtd cls_mtd;
        if (!gst_analytics_relation_meta_add_one_cls_mtd(relationMeta, best_confidence,
                                                        label_quark, &cls_mtd)) {
            throw std::runtime_error("Failed to add classification metadata.");
        }
    }
}
```

**Compilation**

Compile your library as a shared object with GStreamer Analytics
support:

```bash
g++ -shared -fPIC -o libcustom_postproc.so custom_postproc.cpp \
  `pkg-config --cflags --libs gstreamer-1.0 gstreamer-analytics-1.0` -ldl -Wl,--no-undefined
```
