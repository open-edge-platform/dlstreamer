# GST_PLUGIN_FEATURE_RANK

## Using the **GST_PLUGIN_FEATURE_RANK** environment variable to control element selection

The **`GST_PLUGIN_FEATURE_RANK`** environment variable changes the *rank* (priority) of GStreamer elements in the registry at runtime. Any autoplugging mechanism that picks an element by rank is affected — **`decodebin3`, `decodebin`, `uridecodebin`, `playbin`, `parsebin`, `encodebin`, `autovideosink`/`autoaudiosink`**, etc. For example, when `decodebin3` auto-plugs a pipeline and several decoders can handle the same stream, it selects the one with the highest rank, so this variable lets you force which decoder is used without editing the pipeline. It has no effect on elements you name explicitly in the pipeline.

GStreamer applies these ranks when it loads the plugin registry (during `gst_init()`), using the same mechanism as the [GstPluginFeature](https://gstreamer.freedesktop.org/documentation/gstreamer/gstpluginfeature.html?gi-language=python#function-macros) API (`gst_plugin_feature_set_rank()`).

### Syntax

The variable takes a comma-separated list of `feature:rank` pairs, where `rank` is a `GstRank` value (`NONE`, `MARGINAL`, `SECONDARY`, `PRIMARY`, `MAX`) or a number. Export it *before* starting the pipeline:

```bash
export GST_PLUGIN_FEATURE_RANK="<element-name>:<rank>[,<element-name>:<rank>...]"
```

### Detecting available GPUs

To find out which GPUs are present on the platform (and their DRI render nodes), run:

```bash
echo "=== PCI GPUs ==="; lspci -nn | grep -Ei 'VGA|3D|Display'
echo; echo "=== DRI render nodes ==="
for d in /dev/dri/render*; do
  n=$(basename "$d"); v=$(cat /sys/class/drm/$n/device/vendor 2>/dev/null)
  p=$(basename "$(readlink /sys/class/drm/$n/device 2>/dev/null)")
  printf '%-14s %s %s -> %s\n' "$d" "$v" "$p" "$(lspci -s "$p" 2>/dev/null | sed -E 's/^[^:]+: //')"
done
```

### Examples

The following snippets force the H.264 decoder appropriate for a given platform by raising the rank of the preferred element and disabling the others:

```bash
# For Intel iGPU:
export GST_PLUGIN_FEATURE_RANK="vah264dec:MAX,nvh264dec:NONE,varenderD129h264dec:NONE"

# For Intel dGPU:
export GST_PLUGIN_FEATURE_RANK="varenderD129h264dec:MAX,varenderD129postproc:MAX,nvh264dec:NONE"
```