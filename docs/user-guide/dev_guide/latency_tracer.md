# Latency Tracer

This tutorial shows how to use `latency_tracer` to find
element and pipeline frame latency. The time displayed in the logs has
precision in the order of **milliseconds**.

- element latency (latency_tracer_element) - time that a
  buffer takes to travel from the source pad of the previous element to the source
  pad of the current element.
- pipeline latency (latency_tracer_pipeline) - time that a buffer takes
  to travel from the source pad of the source element to the source pad of the
  element before the sink element. This also provides latency and fps of full
  pipeline.

## Elements and pipeline latency

### Basic configuration

Sample pipeline with enabled default latency tracker:

```bash
GST_DEBUG="GST_TRACER:7" GST_TRACERS="latency_tracer" gst-launch-1.0 filesrc location=input.mp4 ! decodebin3 ! gvadetect model=yolo11s.xml device=CPU ! gvafpscounter ! fakesink sync=False
```

By default, `latency_tracer` shows latency for both pipeline and
elements for each frame. Below there is a sample log for `gvadetect` element and a whole pipeline.

```bash
...
0:00:03.014774818 79459 0x55736c57a060 TRACE             GST_TRACER :0:: latency_tracer_element, name=(string)gvadetect0, frame_latency=(double)136.919739, avg=(double)138.142178, min=(double)136.327195, max=(double)143.694035, frame_num=(uint)23, is_bin=(boolean)0;
...
0:00:03.014782371 79459 0x55736c57a060 TRACE             GST_TRACER :0:: latency_tracer_pipeline, frame_latency=(double)704.898524, avg=(double)238.748567, min=(double)0.013293, max=(double)704.898524, latency=(double)32.277973, fps=(double)30.980879, frame_num=(uint)27;
...
```

Key measurement for `latency_tracer_element`:
- `frame_latency` - the current frame's processing latency calculated as the time difference
  between when the frame was entered the element and the current timestamp at element's output
- `avg` - the average value of element processing latency for all frames processed so far by the element,
  average value of `frame_latency` for the element from the pipeline execution start till now
- `min` - the lowest value of element processing latency for all frames processed so far by the element,
  minimum value of `frame_latency` for the element from the pipeline execution start till now
- `max` - the maximum value of element processing latency for all frames processed so far by the element,
  maximum value of `frame_latency` for the element from the pipeline execution start till now
- `frame_num` - frame number for which latencies values are calculated

Key measurement for `latency_tracer_pipeline`:
- `frame_latency` - the current frame's processing latency calculated as the time difference
  between when the frame was entered the pipeline and the current timestamp at pipeline's output
- `avg` - the average value of the pipeline processing latency for all frames processed so far by the pipeline,
  average value of `frame_latency` for the pipeline from the pipeline execution start till now
- `min` - the lowest value of pipeline processing latency for all frames processed so far by the pipeline,
  minimum value of `frame_latency` for the pipeline from the pipeline execution start till now
- `max` - the maximum value of pipeline processing latency for all frames processed so far by the pipeline,
  maximum value of `frame_latency` for the pipeline from the pipeline execution start till now
- `latency` - the overall pipeline throughput latency calculated as the total elapsed time divided by the
  number of frames processed, shows the average time interval between pipeline's frame outputs (inversely related to FPS)
- `fps` - frames per second indicating the throughput of the pipeline, calculated as `1000 / latency` where latency is in milliseconds
- `frame_num` - frame number for which latencies values are calculated

### Configuration options

The `latency_tracer` can be configured to show latencies only for elements or a pipeline:
- show only element latencies
  ```bash
  GST_DEBUG="GST_TRACER:7" GST_TRACERS="latency_tracer(flags=element)" gst-launch-1.0 ...
  ```
- show only pipeline latencies
  ```bash
  GST_DEBUG="GST_TRACER:7" GST_TRACERS="latency_tracer(flags=pipeline)" gst-launch-1.0 ...
  ```
- show both element and pipeline latencies (default)
  ```bash
  GST_DEBUG="GST_TRACER:7" GST_TRACERS="latency_tracer(flags=element+pipeline)" gst-launch-1.0 ...
  ```


## Interval Configuration

The `latency_tracer` supports interval-based reporting that provides periodic statistics summaries.
This feature allows to monitor latency trends over time by generating aggregate reports at specified intervals.

### Basic configuration

The interval can be configured by `interval` parameter. Its default value is 1000ms (1 second).
Example of configuring 2 seconds interval:

```bash
GST_DEBUG="GST_TRACER:7" GST_TRACERS="latency_tracer(flags=element+pipeline,interval=2000)" gst-launch-1.0 filesrc location=input.mp4 ! decodebin3 ! gvadetect model=yolo11s.xml device=CPU ! gvafpscounter ! fakesink sync=False
```

Sample output:

```bash
...
0:00:02.651279827   370 0x7928f8987160 TRACE             GST_TRACER :0:: latency_tracer_element_interval, name=(string)gvadetect0, interval=(double)2001.379689, avg=(double)106.710024, min=(double)88.035645, max=(double)133.217614;
...
0:00:02.651439668   370 0x7928f8987160 TRACE             GST_TRACER :0:: latency_tracer_pipeline_interval, interval=(double)2000.249664, avg=(double)364.307407, min=(double)0.004015, max=(double)529.258106, latency=(double)21.279252, fps=(double)46.994134;
...
```

The interval feature generates two types of periodic reports:

1. `latency_tracer_element_interval` - Provides aggregated statistics for each element over the specified time interval
2. `latency_tracer_pipeline_interval` - Provides aggregated statistics for the entire pipeline over the specified time interval

Key measurements in interval reports:
- `interval` - The actual duration of the reporting interval in milliseconds
- All other parameters (`avg`, `min`, `max`, `latency`, `fps`) have the same interpretation as for ordinary latency_tracer,
  but statistics are calculated for the last interval window only
