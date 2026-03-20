# Custom GStreamer Plugin Installation

This article describes how to install a custom GStreamer plugin.

## 1. Install custom GStreamer plugin(s)

First, install dedicated GStreamer plugin(s) relevant for required,
missed GStreamer element(s), using the `apt` package manager.

To install the `x264enc` GStreamer plugin, use:

```bash
sudo apt update
sudo apt install gstreamer1.0-plugins-ugly
```

For a more generic case, use:

```bash
sudo apt update
sudo apt install libgstreamer1.0-dev libgstreamer-plugins-base1.0-dev libgstreamer-plugins-bad1.0-dev gstreamer1.0-plugins-base gstreamer1.0-plugins-good gstreamer1.0-plugins-bad gstreamer1.0-plugins-ugly gstreamer1.0-libav gstreamer1.0-tools gstreamer1.0-x gstreamer1.0-alsa gstreamer1.0-gl gstreamer1.0-gtk3 gstreamer1.0-qt5 gstreamer1.0-pulseaudio
```

## 2. Update GStreamer plugin(s) settings

After installation, update the `GST_PLUGIN_PATH` environment variable
with the path to the installation directory of GStreamer custom plugin(s).

For example, to update `GST_PLUGIN_PATH` with the `/usr/lib/x86_64-linux-gnu/gstreamer-1.0`
path to the custom plugin, use the following command:

```bash
export GST_PLUGIN_PATH=${GST_PLUGIN_PATH}:/usr/lib/x86_64-linux-gnu/gstreamer-1.0
```
