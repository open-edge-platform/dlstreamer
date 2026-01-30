# gvametaaggregate

Aggregates inference results from multiple pipeline branches. Data that
is transferred further along the pipeline is taken from the first sink
pad of the `gvametaaggregate` element.

```bash
Pad Templates:
  SRC template: 'src'
    Availability: Always
    Capabilities:
      ANY
    Type: GstGvaMetaAggregatePad
    Pad Properties:
      emit-signals        : Send signals to signal data consumption
                            flags: readable, writable
                            Boolean. Default: false

  SINK template: 'sink_%u'
    Availability: On request
    Capabilities:
      ANY
    Type: GstGvaMetaAggregatePad
    Pad Properties:
      emit-signals        : Send signals to signal data consumption
                            flags: readable, writable
                            Boolean. Default: false

Element has no clocking capabilities.
Element has no URI handling capabilities.

Pads:
  SRC: 'src'
    Pad Template: 'src'

Element Properties:
  emit-signals        : Send signals
                        flags: readable, writable
                        Boolean. Default: false
  latency             : Additional latency in live mode to allow upstream to take longer to produce buffers for the current position (in nanoseconds)
                        flags: readable, writable
                        Unsigned Integer64. Range: 0 - 18446744073709551615 Default: 0
  min-upstream-latency: When sources with a higher latency are expected to be plugged in dynamically after the aggregator has started playing, this allows overriding the minimum latency reported by the initial source(s). This is only taken into account when larger than the actually reported minimum latency. (nanoseconds)
                        flags: readable, writable
                        Unsigned Integer64. Range: 0 - 18446744073709551615 Default: 0
  name                : The name of the object
                        flags: readable, writable
                        String. Default: "gvametaaggregate0"
  parent              : The parent of the object
                        flags: readable, writable
                        Object of type "GstObject"
  start-time          : Start time to use if start-time-selection=set
                        flags: readable, writable
                        Unsigned Integer64. Range: 0 - 18446744073709551615 Default: 18446744073709551615
  start-time-selection: Decides which start time is output
                        flags: readable, writable
                        Enum "GstAggregatorStartTimeSelection" Default: 0, "zero"
                          (0): zero             - Start at 0 running time (default)
                          (1): first            - Start at first observed input running time
                          (2): set              - Set start time with start-time property

Element Signals:
  "samples-selected" :  void user_function (GstElement* object,
                                            GstSegment* arg0,
                                            guint64 arg1,
                                            guint64 arg2,
                                            guint64 arg3,
                                            GstStructure* arg4,
                                            gpointer user_data);
```
