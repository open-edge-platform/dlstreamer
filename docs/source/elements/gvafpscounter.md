# gvafpscounter

Measures frames per second across multiple streams in a single process.

```bash
Pad Templates:
  SINK template: 'sink'
    Availability: Always
    Capabilities:
      video/x-raw(ANY)
      application/tensor(ANY)
      application/tensors(ANY)

  SRC template: 'src'
    Availability: Always
    Capabilities:
      ANY

Element has no clocking capabilities.
Element has no URI handling capabilities.

Pads:
  SINK: 'sink'
    Pad Template: 'sink'
  SRC: 'src'
    Pad Template: 'src'

Element Properties:
  avg-fps             : The average frames per second calculated from the beginning of stream start, read-only parameter.
                        flags: readable
                        Float. Range: 0 - 3.402823e+38 Default: 0.0
  interval            : The time interval in seconds for which the fps will be measured
                        flags: readable, writable
                        String. Default: "1"
  name                : The name of the object
                        flags: readable, writable
                        String. Default: "gvafpscounter0"
  parent              : The parent of the object
                        flags: readable, writable
                        Object of type "GstObject"
  qos                 : Handle Quality-of-Service events
                        flags: readable, writable
                        Boolean. Default: false
  read-pipe           : Read FPS data from a named pipe. Create and delete a named pipe.
                        flags: readable, writable
                        String. Default: null
  starting-frame      : Start collecting fps measurements after the specified number of frames have been processed to remove the influence of initialization cost
                        flags: readable, writable
                        Unsigned Integer. Range: 0 - 4294967295 Default: 0
  write-pipe          : Write FPS data to a named pipe. Blocks until read-pipe is opened.
                        flags: readable, writable
                        String. Default: null
  print-std-dev       : Write standard deviation for all streams. The metric measures time interval between two subsequent frames received for a particular video stream and computes standard deviation over time.
                        flags: readable, writable
                        Boolean. Default: false
  print-latency       : Write average frame latency for all streams. Needs timecodestamper element at the beginning of pipeline.
                        flags: readable, writable
                        Boolean. Default: false
```
