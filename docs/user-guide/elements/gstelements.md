# GStreamer Elements

This page contains examples of certain GStreamer elements/plugins used in
combination with Intel® Deep Learning Streamer elements.

| Element | Description |
|---|---|
| [`vacompositor`](./vacompositor.md) | The `vacompositor` element allows merging multiple displays into one. |
| `GST_PLUGIN_FEATURE_RANK` | The **`GST_PLUGIN_FEATURE_RANK`** environment variable changes the *rank* (priority) of GStreamer elements in the registry at runtime. Any autoplugging mechanism that picks an element by rank is affected — **`decodebin3`, `decodebin`, `uridecodebin`, `playbin`, `parsebin`, `encodebin`, `autovideosink`/`autoaudiosink`**, etc. For example, when `decodebin3` auto-plugs a pipeline and several decoders can handle the same stream, it selects the one with the highest rank, so this variable lets you force which decoder is used without editing the pipeline. It has no effect on elements you name explicitly in the pipeline. <br><br> **Syntax** <br>The variable takes a comma-separated list of `feature:rank` pairs, where `rank` is a `GstRank` value (`NONE`, `MARGINAL`, `SECONDARY`, `PRIMARY`, `MAX`) or a number. Export it before starting the pipeline: <br> **```export GST_PLUGIN_FEATURE_RANK="<element-name>:<rank>[,element-name>:<rank>...]"```** <br><br> **Examples** <br>The following snippets force the H.264 decoder appropriate for a given platform with **iGPU** and **dGPU** by raising the rank of the preferred element and disabling the others: <br><br> For Intel **iGPU**:<br> **```export GST_PLUGIN_FEATURE_RANK="vah264dec:MAX"```** <br><br>For Intel **dGPU** (/dev/dri/renderD129): <br>**``` export GST_PLUGIN_FEATURE_RANK="varenderD129h264dec:MAX,varenderD129postproc:MAX"```**|
| `timecodestamper` | The `timecodestamper` element allows attaching<br>a timecode to every incoming video frame.<br><br>**Example:**<br> gst-launch-1.0 rtspsrc location=”rtsp://root:admin_pwd@IP/axis-media/media.amp” ! rtph264depay ! h264parse ! avdec_h264 !<br>timecodestamper set=always source=rtc ! videoconvert  ! gvadetect model=$mDetect device=CPU ! queue ! gvametaconvert timestamp-utc=true json-indent=-1 !<br>gvametapublish method=mqtt file-format=json  mqtt-config=mqtt_config.json ! fakesink sync=false <br> |

<!--hide_directive
:::{toctree}
:maxdepth: 1
:hidden:

vacompositor
:::
hide_directive-->