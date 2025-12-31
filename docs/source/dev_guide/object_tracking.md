# Object Tracking

## Object tracking types

[gvatrack](../elements/gvatrack.md) is an object tracking element, typically inserted into a
video analytics pipeline right after the object
detection element [gvadetect](../elements/gvadetect.md)
and can work in the following modes as specified by the *tracking-type* property:

| tracking-type        | Max detection interval (inference-interval property in gvadetect) | Algorithm uses detected coordinates and trajectory extrapolation | Algorithm uses image data | Notes                                                                                                                                                                                                                     |
|----------------------|-------------------------------------------------------------------|------------------------------------------------------------------|---------------------------|---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| short-term-imageless | &lt;= 5                                                           | Yes                                                              | No                        | Assigns a unique id to objects and generates object position for frames on which object detection was skipped.<br>Fast algorithm that extrapolates object trajectory from previous frame(s) without access to image data. |
| zero-term            | 1 (every frame)                                                   | Yes                                                              | Yes                       | Assigns a unique id to objects, and requires object detection run on every frame.<br>Takes into account object trajectory as well as a color histogram of object image data.                                              |
| zero-term-imageless  | 1 (every frame)                                                   | Yes                                                              | No                        | Assigns a unique id to objects, and requires object detection run on every frame.<br>Fastest algorithm as based on comparing object coordinates on current frame with objects trajectory on previous frames.              |
| deep-sort            | 1 (every frame)                                                   | Yes                                                              | Yes                       | Assigns a unique id to objects using Kalman filter for motion prediction and deep learning features for re-identification.<br>Robust algorithm that minimizes ID switches by combining appearance and motion cues. Requires feature extraction model (e.g., mars-small128). |

## Additional configuration

Additional configuration parameters for object tracker can be passed via the
`config` property of [gvatrack](../elements/gvatrack.md).
The `config` property accepts a comma separated list of
`KEY=VALUE` parameters. The supported parameters are described below:

### Tracking per class

Configurable via `tracking_per_class` parameter. It specifies whether
the class label is considered for updating the `object_id` of an object
or not. When set to `true`, a new tracking ID will be assigned to an
object when the class label changes. When set to `false`, the tracking
ID will be retained, based on the position of the bounding box, even if
the class label of the object changes due to model inaccuracy. The
default value is `true`.

Example:

```bash
... gvatrack config=tracking_per_class=false ...
```

### Maximum number of objects

Configurable via `max_num_objects` parameter. It specifies the maximum
number of objects that the object tracker will track. On devices with
less computing power, tracking a smaller number of objects can reduce
compute and increase throughput.

Example:

```bash
... gvatrack config=max_num_objects=20 ...
```

## Sample

Refer to the
[vehicle_pedestrian_tracking](https://github.com/open-edge-platform/dlstreamer/tree/master/samples/gstreamer/gst_launch/vehicle_pedestrian_tracking) sample
for a pipeline with `gvadetect`, `gvatrack`, and `gvaclassify` elements.

## Deep SORT Tracking

Deep SORT (Simple Online and Realtime Tracking with a Deep Association Metric) is an advanced tracking algorithm that combines:

- **Motion prediction**: Kalman filter for predicting object position based on velocity and position history
- **Appearance features**: Deep learning re-identification model that extracts 128-dimensional feature vectors to distinguish objects
- **Robust association**: Combines IoU (Intersection over Union) and cosine distance metrics to match detections with existing tracks

### Feature Extraction Model

Deep SORT requires a feature extraction model that generates 128-dimensional feature vectors for person re-identification. The recommended model is **mars-small128**, which can be downloaded using:

```bash
./samples/download_public_models.sh --model-name mars-small128
```

This downloads both FP32 and INT8 quantized versions of the model.

#### Usage with External Feature Extractor

Use `gvainference` before `gvatrack` to perform feature extraction. This allows running inference on GPU or other devices for better performance.

```bash
gvainference model=/path/to/mars-small128/FP32/mars-small128.xml device=GPU inference-region=roi-list ! \
queue ! \
gvatrack tracking-type=deep-sort
```

**Advantages**: Device flexibility (CPU/GPU/NPU), potentially higher throughput

### Configuration Parameters

Deep SORT behavior can be fine-tuned using the `deepsort-trck-cfg` property with comma-separated `KEY=VALUE` parameters:

#### Available Parameters

| Parameter | Default | Description | Tuning Guidelines |
|-----------|---------|-------------|-------------------|
| `max_iou_distance` | 0.7 | Maximum IoU(Intersection over Union) distance threshold for matching detections to existing tracks based on bounding box overlap | Lower values (0.5-0.6) = stricter spatial matching, less tolerance for movement. Higher values (0.7-0.8) = more lenient matching, better track continuity but higher risk of identity switches |
| `max_age` | 30 | Maximum number of frames a track survives without detection before deletion | **Increase to 60-90** to reduce object loss during occlusions or missed detections. Lower values delete tracks faster, good for crowded scenes |
| `n_init` | 3 | Number of consecutive detections required to confirm a new track | Lower values (1-2) = faster initialization but more false positives. Higher values = more reliable but slower confirmation |
| `max_cosine_distance` | 0.2 | Maximum cosine distance threshold for appearance feature matching between detections and tracks | **Increase to 0.3-0.4** to handle lighting changes, better for similar-looking objects, viewing angles, or appearance variations. Lower values = stricter appearance matching |
| `nn_budget` | 100 | Maximum number of appearance features stored per track | Higher values = better re-identification, good for extended tracking scenarios but more memory. Typical range: 50-150 |

#### Examples for Common Tuning Scenarios

**Reducing object loss (tracks disappearing too quickly):**
```bash
gvatrack tracking-type=deep-sort deepsort-trck-cfg="max_age=60,max_cosine_distance=0.3"
```

**Handling fast-moving objects:**
```bash
gvatrack tracking-type=deep-sort deepsort-trck-cfg="max_iou_distance=0.6,max_age=45"
```

**Crowded scenes with occlusions:**
```bash
gvatrack tracking-type=deep-sort deepsort-trck-cfg="max_age=90,max_cosine_distance=0.35,nn_budget=150"
```

**Conservative tracking (minimize false positives):**
```bash
gvatrack tracking-type=deep-sort deepsort-trck-cfg="n_init=5,max_cosine_distance=0.15"
```

### Example Pipeline

```bash
gst-launch-1.0 filesrc location=video.mp4 ! decodebin ! \
  gvadetect model=person-detection.xml ! \
  gvainference model=mars-small128.xml device=GPU inference-region=roi-list ! \
  gvatrack tracking-type=deep-sort \
    deepsort-trck-cfg="max_age=60,max_cosine_distance=0.3" ! \
  gvawatermark ! videoconvert ! autovideosink
```



## How to read object unique id

The following code example iterates all objects detected or tracked on
the current frame and prints object unique id and bounding box
coordinates.

```cpp
#include "video_frame.h"

void PrintObjects(GstBuffer *buffer) {
    GVA::VideoFrame video_frame(buffer);
    std::vector<GVA::RegionOfInterest> regions = video_frame.regions();
    for (GVA::RegionOfInterest &roi : regions) { // iterate objects
        int object_id = roi.object_id(); // get unique object id
        auto bbox = roi.rect(); // get bounding box information
        std::cout << "Object id=" << object_id << ", bounding box: " << bbox.x << "," << bbox.y << "," << bbox.w << "," << bbox.h << "," << std::endl;
    }
}
```

## Performance considerations

Object tracking can help improve performance of both object detection
`(`gvadetect`) and object classification (`gvaclassify`) elements

- Object detection: *short-term-imageless* tracking types enable
  reducing object detection frequency by setting the
  `inference-interval` property in the `gvadetect` element.
- Object classification: if an object was classified by `gvaclassify`
  on frame N, you can skip classification of the same object for
  several next frames N+1,N+2,... and reuse last classification result
  from frame N. Reclassification interval is controlled by the
  `reclassify-interval` property in the `gvaclassify` element.

See the sample pipeline below:

```bash
gst-launch-1.0 \
... ! \
decodebin3 ! \
gvadetect model=$DETECTION_MODEL inference-interval=10 ! \
gvatrack tracking-type=short-term-imageless ! \
gvaclassify model=$AGE_GENDER_MODEL reclassify-interval=30 ! \
gvaclassify model=$EMOTION_MODEL reclassify-interval=15 ! \
gvaclassify model=$LANDMARKS_MODEL ! \
...
```

It detects faces every 10th frame and tracks faces position for the next 9 frames.
The age and gender classification is updated every second, the emotion
classification is updated twice a second, and the landmark points are updated every
frame.
