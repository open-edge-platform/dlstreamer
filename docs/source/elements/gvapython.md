# gvapython

Provides a callback to execute user-defined Python functions on every
frame. Can be used for metadata conversion, inference post-processing,
and other tasks.

```bash
Pad Templates:
  SRC template: 'src'
    Availability: Always
    Capabilities:
      ANY

  SINK template: 'sink'
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
  arg                 : Argument for Python class initialization.Argument is interpreted as a JSON value or JSON array.If passed multiple times arguments are combined into a single JSON array.
                        flags: readable, writable
                        String. Default: "[]"
  class               : Python class name
                        flags: readable, writable
                        String. Default: null
  function            : Python function name
                        flags: readable, writable
                        String. Default: "process_frame"
  kwarg               : Keyword argument for Python class initialization.Keyword argument is interpreted as a JSON object.If passed multiple times keyword arguments are combined into a single JSON object.
                        flags: readable, writable
                        String. Default: "{}"
  module              : Python module name
                        flags: readable, writable
                        String. Default: null
  name                : The name of the object
                        flags: readable, writable
                        String. Default: "gvapython0"
  parent              : The parent of the object
                        flags: readable, writable
                        Object of type "GstObject"
  qos                 : Handle Quality-of-Service events
                        flags: readable, writable
                        Boolean. Default: false
```
