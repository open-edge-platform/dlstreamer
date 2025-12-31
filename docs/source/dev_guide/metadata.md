# Metadata

Inference plugins utilize standard GStreamer metadata
[GstVideoRegionOfInterestMeta](https://gstreamer.freedesktop.org/documentation/video/gstvideometa.html?gi-language=c#GstVideoRegionOfInterestMeta)
for object detection and classification use cases (the
[gvadetect](../elements/gvadetect.md), [gvaclassify](../elements/gvaclassify.md) elements),
and define two custom metadata types:

- [GstGVATensorMeta](https://github.com/open-edge-platform/dlstreamer/blob/master/include/dlstreamer/gst/metadata/gva_tensor_meta.h)

  For output of the [gvainference](../elements/gvainference.md) element performing generic
  inference on any model with an image-compatible input layer and any format of
  output layer(s)

- [GstGVAJSONMeta](https://github.com/open-edge-platform/dlstreamer/blob/master/include/dlstreamer/gst/metadata/gva_json_meta.h)

  For output of the [gvametaconvert](../elements/gvametaconvert.md) element performing
  conversion of `GstVideoRegionOfInterestMeta` into the JSON format

The `gvadetect` element supports only object detection models and
checks whether the model output layer has a known format convertible into a
list of bounding boxes. The `gvadetect` element creates and attaches to the
output `GstBuffer` as many instances of `GstVideoRegionOfInterestMeta` as
objects detected on the frame. The object bounding-box position and
object label are stored directly in `GstVideoRegionOfInterestMeta` fields
`x`, `y`, `w`, `h`, `roi_type`, while additional detection information
such as confidence (in range \[0,1\]), model name, and output layer name
are stored as the `GstStructure` object and added into `GList *params` list of
the same `GstVideoRegionOfInterestMeta`.

The `gvaclassify` element is typically inserted into the pipeline
after `gvadetect` and executes inference on all objects detected by
`gvadetect` (i.e., as many times as `GstVideoRegionOfInterestMeta` attached
to the input buffer) with input on the crop area specified by
`GstVideoRegionOfInterestMeta`. The inference output is converted into as
many `GstStructure` objects as the number of output layers in the model
and added into the `GList *params` list of the
`GstVideoRegionOfInterestMeta`. Each `GstStructure` contains full inference
results such as tensor data and dimensions, model and layer names, and
the label in a string format (if post-processing rules are specified).

The `gvainference` element generates and attaches to the `GstGVATensorMeta`
frame custom metadata (as many instances as output layers in the
model) containing tensor raw data and additional information such as
tensor dimensions, data precision, etc.

The following pipeline is used as an example:

```bash
MODEL1=face-detection-adas-0001
MODEL2=age-gender-recognition-retail-0013
MODEL3=emotions-recognition-retail-0003

gst-launch-1.0 --gst-plugin-path ${GST_PLUGIN_PATH} \
    filesrc location=${INPUT} ! decodebin3 ! video/x-raw ! videoconvert ! \
    gvadetect   model=$(MODEL_PATH $MODEL1) ! queue ! \
    gvaclassify model=$(MODEL_PATH $MODEL2) model-proc=$(PROC_PATH $MODEL2) ! queue ! \
    gvaclassify model=$(MODEL_PATH $MODEL3) model-proc=$(PROC_PATH $MODEL3) ! queue ! \
    gvawatermark ! videoconvert ! fpsdisplaysink sync=false
```

> **NOTE:** More examples can be found in the
> [gst_launch](https://github.com/open-edge-platform/dlstreamer/tree/master/samples/gstreamer/gst_launch)
> folder.

If the `gvadetect` element detected three faces, it will attach three
metadata objects each containing one `GstStructure` with detection
results, then `gvaclassify` will add two more `GstStructure` (model contains
two output layers, age, and gender) into each meta, and another
`gvaclassify` will add one more `GstStructure` (emotion), resulting in three
metadata objects each containing four `GstStructure` in the `GList *params`
field: detection, age, gender, emotions.

"C" application can iterate objects and inference results, using
GStreamer API, similarly to the code snippet below:

```C
#include <gst/video/video.h>

void print_meta(GstBuffer *buffer) {
    gpointer state = NULL;
    GstMeta *meta = NULL;
    while ((meta = gst_buffer_iterate_meta(buffer, &state)) != NULL) {
        if (meta->info->api != GST_VIDEO_REGION_OF_INTEREST_META_API_TYPE)
            continue;
        GstVideoRegionOfInterestMeta *roi_meta = (GstVideoRegionOfInterestMeta*)meta;
        printf("Object bounding box %d,%d,%d,%d\n", roi_meta->x, roi_meta->y, roi_meta->w, roi_meta->h);
        for (GList *l = roi_meta->params; l; l = g_list_next(l)) {
            GstStructure *structure = (GstStructure *) l->data;
            printf("  Attribute %s\n", gst_structure_get_name(structure));
            if (gst_structure_has_field(structure, "label")) {
                printf("    label=%s\n", gst_structure_get_string(structure, "label"));
            }
            if (gst_structure_has_field(structure, "confidence")) {
                double confidence;
                gst_structure_get_double(structure, "confidence", &confidence);
                printf("    confidence=%.2f\n", confidence);
            }
        }
    }
}
```

C++ application can access metadata much simpler, utilizing the C++ interface:

```C++
#include "gst/videoanalytics/video_frame.h"

void PrintMeta(GstBuffer *buffer) {
    GVA::VideoFrame video_frame(buffer);
    for (GVA::RegionOfInterest &roi : video_frame.regions()) {
        auto rect = roi.rect();
        std::cout << "Object bounding box " << rect.x << "," << rect.y << "," << rect.w << "," << rect.h << "," << std::endl;
        for (GVA::Tensor &tensor : roi.tensors()) {
            std::cout << "  Attribute " << tensor.name() << std::endl;
            std::cout << "    label=" << tensor.label() << std::endl;
            std::cout << "    model=" << tensor.model_name() << std::endl;
        }
    }
}
```

The following table summarizes the input and output of various elements:

| GStreamer element | Description | INPUT | OUTPUT |
|---|---|---|---|
| `gvainference` | Generic inference | <br>GstBuffer<br>or<br>GstBuffer + GstVideoRegionOfInterestMeta<br><br> | <br>INPUT + GvaTensorMeta<br>or<br>INPUT + extended GstVideoRegionOfInterestMeta<br><br> |
| `gvadetect` | Object detection | <br>GstBuffer<br>or<br>GstBuffer + GstVideoRegionOfInterestMeta<br><br> | INPUT + GstVideoRegionOfInterestMeta |
| `gvaclassify` | Object classification | <br>GstBuffer<br>or<br>GstBuffer + GstVideoRegionOfInterestMeta<br><br> | <br>INPUT + GvaTensorMeta<br>or<br>INPUT + extended GstVideoRegionOfInterestMeta<br><br> |
| `gvatrack` | Object tracking | <br>GstBuffer<br>[ + GstVideoRegionOfInterestMeta]<br><br> | INPUT + GstVideoRegionOfInterestMeta |
| `gvaaudiodetect` | Audio event detection | GstBuffer | INPUT + GstGVAAudioEventMeta |
| `gvametaconvert` | Metadata conversion | GstBuffer + GstVideoRegionOfInterestMeta, GvaTensorMeta | INPUT + GstGVAJSONMeta |
| `gvametapublish` | Metadata publishing to Kafka or MQTT | GstBuffer + GstGVAJSONMeta | INPUT |
| `gvametaaggregate` | Metadata aggregating | [GstBuffer + GstVideoRegionOfInterestMeta] | INPUT + extended GstVideoRegionOfInterestMeta |
| `gvawatermark` | Overlay | GstBuffer + GstVideoRegionOfInterestMeta, GvaTensorMeta | GstBuffer with modified image |
