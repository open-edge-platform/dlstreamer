# Optimizer
DLS Optimizer is a tool for helping users discover more optimal versions of the pipelines they run on DL Streamer. It will explore different modifications to your pipeline that are known to increase performance and measure them. As a result, you can expect to receive a pipeline that is better suited to your setup.

Optimizations involve modifying inference elements that are part of DL Streamer, as well as searching for pre- and post-processing elements better suited for the pipeline.

Modification of inference elements currently covers:
- Discovering more suitable devices
- Adjusting the batching of frames in an element
- Adjusting the number of inference requests done simultaneously (nireqs)

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
>**Note**\
>This example assumes your working directory is the optimizer directory `/opt/intel/dlstreamer/scripts/optimizer`
```
python3 . MODE [OPT] -- PIPELINE

Arguments:
  MODE      The type of optimization that will be performed on the pipeline.
            Possible values are \"fps\" and \"streams\".

            fps - the optimizer will explore possible alternatives
                  for the pipeline, trying to locate versions that
                  have increased performance measured by fps.

            streams - the optimizer will explore possible alternatives
                      for the pipeline, trying to locate a version which
                      can support the most streams at once without
                      crossing a minimum fps threshold.
                      (check \"multistream-fps-limit for more info)

  PIPELINE  A string representing a pipeline in the GStreamer notation
            which the tool will attempt to optimize.

Options:
    --search-duration SEARCH_DURATION   How long should the optimizer search for better pipelines.
    --sample-duration SAMPLE_DURATION   How long should every pipeline be sampled for performance.
    --detection-threshold THRESHOLD     Minimum threshold of detections that tested pipelines are
                                        not allowed to cross in order to count as valid alternatives.
    --multistream-fps-limit LIMIT       Minimum fps limit which streams are not allowed to cross
                                        when optimizing for a multi-stream scenario.
    --enable-cross-stream-batching      Enable cross stream batching for inference elements in fps mode.
    --allowed-devices ALLOWED_DEVICES   List of allowed devices (CPU, GPU, NPU) to be used by the optimizer.
                                        If not specified, all available, detected devices will be used.
                                        Tool does not support discrete GPU selection.
                                        eg.--allowed-devices CPU NPU,--allowed-devices GPU
    --log-level LEVEL                   Configure the logging detail level.
    -v, --verbose                       Print information about every candidate pipeline investigated during
                                        optimization process.
```
**`search-duration`** default: `300` seconds \
Increasing the **search duration** will increase the chances of discovering more performant pipelines.

**`sample-duration`** default: `10` seconds \
Increasing the **sample duration** will improve the stability of the search.

**`multistream-fps-limit`** default: `30` fps \
Increasing the **multi-stream fps limit** will improve the performance of each individual stream,
but the final result is liable to support less streams overall.

**`enable-cross-stream-batching`** \
Levy the inference instance feature of DL Streamer to batch work across multiple streams in fps mode.

**`allowed-devices`** \
Allows you to limit the set of devices that will be considered during the optimization process.

**`log-level`** default: `INFO` \
Available **log levels** are: CRITICAL, FATAL, ERROR, WARN, INFO, DEBUG.

**`verbose`** \
Prints extra information about the candidate pipelines which were examined during the optimization process.

>**Note**\
>Search duration and sample duration both affect the amount of pipelines that will be explored during the search. \
>The total amount should be approximately `search_duration / sample_duration` pipelines.
### Example
```
 python3 . fps -- urisourcebin buffer-size=4096 uri=https://videos.pexels.com/video-files/1192116/1192116-sd_640_360_30fps.mp4 ! decodebin ! gvadetect model=/home/optimizer/models/public/yolo11s/INT8/yolo11s.xml ! queue ! gvawatermark ! fakesink
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
[__main__] [    INFO] - Best found pipeline: urisourcebin buffer-size=4096 uri=https://videos.pexels.com/video-files/1192116/1192116-sd_640_360_30fps.mp4 !decodebin3!gvadetect model=/home/optimizer/models/public/yolo11s/INT8/yolo11s.xml device=GPU pre-process-backend=va-surface-sharing batch-size=2 nireq=2 ! queue ! gvawatermark ! fakesink with fps: 81.987923.2
```
In this case the optimizer started with a pipeline that ran at ~45fps, and found a pipeline that ran at ~82fps instead. The specific improvements were:
 - replacing the `decodebin` with the `decodebin3` element.
 - configuring the `gvadetect` element to use GPU for processing
 - setting the `batch-size` parameter to 2
 - setting the `nireq` parameter to 2

## Using the optimizer as a library

The easiest way of importing the optimizer into your scripts is to include it in your `PYTHONPATH` environment variable: \
```export PYTHONPATH=/opt/intel/dlstreamer/scripts/optimizer```

Targets which are exported in order to facilitate usage inside of scripts:

### `preprocess_pipeline(pipeline) -> processed_pipeline`
- `pipeline: string` - A string containing a valid DL Streamer pipeline.
- `processed_pipeline: string` - A string containing the pipeline with all relevant substitutions.

Perform quick search and replace for known combinations of elements with more performant alternatives.

---
### `DLSOptimizer class`
Initialized without any arguments
```
optimizer = DLSOptimizer()
```
#### Methods
**`get_baseline_pipeline() -> pipeline, fps, streams`**
- `pipeline: string` - The baseline pipeline from which optimization started.
- `pipeline: float` - Fps measured for the baseline pipeline.
- `streams: int` - Number of streams in the baseline pipeline.

Returns information about the original pipeline used in the optimization process. Returned values are meaningless until at least one optimization operation is performed.
```
optimizer = DLSOptimizer()
for (_, _) in optimizer.iter_optimize_for_fps(pipeline):
    pass
pipeline, fps, streams = optimizer.get_baseline_pipeline()
```
---
**`get_optimal_pipeline() -> pipeline, fps, streams`**
- `pipeline: string` - The best pipeline found during optimization.
- `pipeline: float` - Fps measured for the optimal pipeline.
- `streams: int` - Number of streams in the optimal pipeline.

Returns information about the best pipeline found during the optimization process. Returned values are meaningless until at least one optimization operation is performed.
```
optimizer = DLSOptimizer()
for (_, _) in optimizer.iter_optimize_for_streams(pipeline):
    pass
best_pipeline, best_fps, best_streams = optimizer.get_optimal_pipeline()
```
---
**`set_sample_duration(duration)`**
- `duration: int` - The duration of sampling each candidate pipeline in seconds, default `10`.

Configures the sample duration used in optimization sessions.
```
optimizer = DLSOptimizer()
optimizer.set_sample_duration(15)
```
---
**`set_detections_error_threshold(threshold)`**
- `threshold: float` - The throshold of counted detections, between `0.0` and `1.0`, default `0.95`.

Minimum threshold of detections that tested pipelines are not allowed to cross in order to count as valid alternatives.
```
optimizer = DLSOptimizer()
optimizer.set_detections_error_threshold(0.8)
```
---
**`enable_cross_stream_batching(enable)`**
- `enable: bool` - Enable the cross stream batching feature, default `False`.

Levy the inference instance feature of DL Streamer to batch work across multiple streams when optimizing for fps.
```
optimizer = DLSOptimizer()
optimizer.enable_cross_stream_batching(True)
```
---
**`set_mutlistream_fps_limit(limit)`**
- `limit: int` - The minimum fps limit allowed for individual streams when optimizing for amount of streams, default `30`.

Configures the minimum fps limit that streams are not allowed to fall below when optimizing for a multi-stream scenario.
```
optimizer = DLSOptimizer()
optimizer.set_multistream_fps_limit(45)
```
---
**`set_allowed_devices(devices)`**
- `devices: list[string]` - A list of device identifiers.

Limits the set of devices which will be considered during the optimization process.
```
optimizer = DLSOptimizer()
optimizer.set_allowed_devices(["CPU", "GPU"])
```
---
**`optimize_for_fps(pipeline, search_duration) -> optimized_pipeline, fps`**
- `pipeline: string` - A string containing a valid DL Streamer pipeline.
- `search_duration: int` - The duration of serching for better pipelines, default `300`.
- `optimized_pipeline: string` - A string containing the best performing pipeline that has been found during the search.
- `fps: float` - The measured fps of the best perfmorming pipeline.

Runs a series of optimization steps on the pipeline searching for a version with better performance measured by fps.
```
pipeline = "urisourcebin buffer-size=4096 uri=https://videos.pexels.com/video-files/1192116/1192116-sd_640_360_30fps.mp4 ! decodebin ! gvadetect model=/home/optimizer/models/public/yolo11s/INT8/yolo11s.xml ! queue ! gvawatermark ! fakesink"
optimizer = DLSOptimizer()
optimizer.optimize_for_fps(pipeline)
```
---
**`iter_optimize_for_fps(pipeline) -> optimized_pipeline, fps`**
- `pipeline: string` - A string containing a valid DL Streamer pipeline.
- `optimized_pipeline: string` - A string containing a candidate pipeline that has been tested.
- `fps: float` - The measured fps of the candidate pipeline.

Runs a series of optimization steps on the pipeline searching for version with better performance measured by fps. Returns each and every candidate pipeline that has been considered.
```
pipeline = "urisourcebin buffer-size=4096 uri=https://videos.pexels.com/video-files/1192116/1192116-sd_640_360_30fps.mp4 ! decodebin ! gvadetect model=/home/optimizer/models/public/yolo11s/INT8/yolo11s.xml ! queue ! gvawatermark ! fakesink"
optimizer = DLSOptimizer()
for (_, _) in optimizer.iter_optimize_for_fps(pipeline):
    pass
pipeline, fps, streams = optimizer.get_optimal_pipeline()
```
---
**`optimize_for_streams(pipeline, search_duration) -> optimized_pipeline, fps, streams`**
- `pipeline: string` - A string containing a valid DL Streamer pipeline.
- `search_duration: int` - The duration of serching for better pipelines, default `300`.
- `optimized_pipeline: string` - A string containing the best performing pipeline that has been found during the search.
- `fps: float` - The measured fps of the best perfmorming pipeline.
- `streams: int` - The number of streams capable of running above the fps limit with the optimized pipeline.

Searching for a version of the input pipeline which can support the highest number of concurrent streams.
```
pipeline = "urisourcebin buffer-size=4096 uri=https://videos.pexels.com/video-files/1192116/1192116-sd_640_360_30fps.mp4 ! decodebin ! gvadetect model=/home/optimizer/models/public/yolo11s/INT8/yolo11s.xml ! queue ! gvawatermark ! fakesink"
optimizer = DLSOptimizer()
optimizer.optimize_for_streams(pipeline)
```
---
**`iter_optimize_for_streams(pipeline) -> candidate_pipeline, fps, streams`**
- `pipeline: string` - A string containing a valid DL Streamer pipeline.
- `optimized_pipeline: string` - A string containing a candidate pipeline that has been tested.
- `fps: float` - The measured fps of the candidate pipeline.
- `streams: int` - The number of streams capable of running above the fps limit with the candidate pipeline.

Searching for a version of the input pipeline which can support the highest number of concurrent streams. Returns each and every candidate pipeline that has been considered.
```
pipeline = "urisourcebin buffer-size=4096 uri=https://videos.pexels.com/video-files/1192116/1192116-sd_640_360_30fps.mp4 ! decodebin ! gvadetect model=/home/optimizer/models/public/yolo11s/INT8/yolo11s.xml ! queue ! gvawatermark ! fakesink"
optimizer = DLSOptimizer()
for (_, _, _) in optimizer.iter_optimize_for_streams(pipeline):
    pass
pipeline, fps, streams = optimizer.get_optimal_pipeline()
```
---
### Example
```python
from optimizer import get_optimized_pipeline

pipeline = "urisourcebin buffer-size=4096 uri=https://videos.pexels.com/video-files/1192116/1192116-sd_640_360_30fps.mp4 ! decodebin ! gvadetect model=/home/optimizer/models/public/yolo11s/INT8/yolo11s.xml ! queue ! gvawatermark ! fakesink"

optimizer = DLSOptimizer()
optimizer.set_sample_duration(15)
optimized_pipeline, fps = optimizer.optimize_for_fps(pipeline, search_duration = 600)
print("Best discovered pipeline: " + optimized_pipeline)
print("Measured fps: " + fps)
```

## Controling the measurement
The point at which performance is being measured can be controlled by pre-emptively inserting a `gvafpscounter` element into your pipeline definition. For pipelines which lack such an element, the measurement is done after the last inference element supported by the optimizer tool.
