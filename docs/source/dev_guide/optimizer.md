# Optimizer
DLS Optimizer is a tool for helping users discover more optimal versions of the pipelines they run on DL Streamer. It will explore different modifications to your pipeline that are known to increase performance and measure them. As a result, you can expect to receive a pipeline that is better suited to your setup.

## Limitations
Currently the DLS Optimizer focuses mainly on DL Streamer elements, specifically the `gvadetect` and `gvaclassify`. The produced pipeline could still have potential for further optimization by transforming other elements.

Multi-stream pipelines (those utilizing the `tee` element) are also currently not supported.

## Prerequisites
Before using the DLS Optimizer, ensure you have the OpenVINO Python module installed.
You can install it by running the following command:
```bash
pip install openvino
```

## Using the optimizer as a tool
> Note: This example assumes your working directory is the optimizer directory `/opt/intel/dlstreamer/scripts/optimizer`
```
python3 . [OPT] -- PIPELINE

Options:
    --search-duration SEARCH_DURATION   How long should the optimizer search for better pipelines
    --sample-duration SAMPLE_DURATION   How long should every pipeline be sampled for performance
    --log-level LEVEL                   Configure the logging detail level
```

- Increasing the **search duration** will increase the chances of discovering more performant pipelines.
  -> Default: `300` seconds
- Increasing the **sample duration** will improve the stability of the search.
  -> Default: `10` seconds
- Available **log levels** are: CRITICAL, FATAL, ERROR, WARN, INFO, DEBUG.
  -> Default: `INFO`

>Note: Search duration and sample duration both affect the amount of pipelines that will be explored during the search.
### Example
```
 python3 . -- urisourcebin buffer-size=4096 uri=https://videos.pexels.com/video-files/1192116/1192116-sd_640_360_30fps.mp4 ! decodebin ! gvadetect model=/home/optimizer/models/public/yolo11s/INT8/yolo11s.xml ! queue ! gvawatermark ! vah264enc ! h264parse ! mp4mux ! fakesink
[__main__] [    INFO] - GStreamer initialized successfully
[__main__] [    INFO] - GStreamer version: 1.26.6
[__main__] [    INFO] - Detected GPU Device
[__main__] [    INFO] - No NPU Device detected
[__main__] [    INFO] - Sampling for 10 seconds...
FpsCounter(last 1.00sec): total=46.87 fps, number-streams=1, per-stream=46.87 fps
FpsCounter(average 1.00sec): total=46.87 fps, number-streams=1, per-stream=46.87 fps
FpsCounter(last 1.01sec): total=43.70 fps, number-streams=1, per-stream=43.70 fps
FpsCounter(average 2.01sec): total=45.28 fps, number-streams=1, per-stream=45.28 fps

...

FpsCounter(last 1.09sec): total=73.45 fps, number-streams=1, per-stream=73.45 fps
FpsCounter(average 8.70sec): total=73.65 fps, number-streams=1, per-stream=73.65 fps
[__main__] [    INFO] - Best found pipeline: urisourcebin buffer-size=4096 uri=https://videos.pexels.com/video-files/1192116/1192116-sd_640_360_30fps.mp4 !decodebin3!gvadetect model=/home/optimizer/models/public/yolo11s/INT8/yolo11s.xml device=GPU pre-process-backend=va-surface-sharing batch-size=2 nireq=2 ! queue ! gvawatermark ! vah264enc ! h264parse ! mp4mux ! fakesink with fps: 81.987923.2
```
In this case the optimizer started with a pipeline that ran at ~45fps, and found a pipeline that ran at ~82fps instead. The specific improvements were:
 - replacing the `decodebin` with the `decodebin3` element.
 - configuring the `gvadetect` element to use GPU for processing
 - setting the `batch-size` parameter to 2
 - setting the `nireq` parameter to 2

## Using the optimizer as a library

The easiest way of importing the optimizer into your scripts is to include it in your `PYTHONPATH` environment variable:
```export PYTHONPATH=/opt/intel/dlstreamer/scripts/optimizer```

Targets which are exported in order to facilitate usage inside of scripts:

### `preprocess_pipeline(pipeline) -> processed_pipeline`
>`pipeline` - A string containing a valid DL Streamer pipeline.
>`processed_pipeline` - A string containing the pipeline with all relevant substitutions.

Perform quick search and replace for known combinations of elements with more performant alternatives.

### `get_optimized_pipeline(pipeline, search_duration, sample_duration) -> (optimized_pipeline, fps)`
>`pipeline` - A string containing a valid DL Streamer pipeline.
>`search_duration` - The duration of searching for optimized pipelines in seconds, default `300`.
>`sample_duration` - The duration of sampling each candidate pipeline in seconds, default `10`.
>`optimized_pipeline` - A string containing the best performing pipeline that has been found during the search.
>`fps` - The measured fps of the best perfmorming pipeline.

Runs a series of optimization steps on the pipeline searching for a better performing versions.

### Example
```python
from optimizer import get_optimized_pipeline

pipeline = "urisourcebin buffer-size=4096 uri=https://videos.pexels.com/video-files/1192116/1192116-sd_640_360_30fps.mp4 ! decodebin ! gvadetect model=/home/optimizer/models/public/yolo11s/INT8/yolo11s.xml ! queue ! gvawatermark ! vah264enc ! h264parse ! mp4mux ! fakesink"

optimized_pipeline, fps = get_optimized_pipeline(pipeline)
print("Best discovered pipeline: " + optimized_pipeline)
print("Measured fps: " + fps)
```

## Controling the measurement
The point at which performance is being measured can be controlled by pre-emptively inserting a `gvafpscounter` element into your pipeline definition. For pipelines which lack such an element, the measurement is done after the last inference element supported by the optimizer tool.
