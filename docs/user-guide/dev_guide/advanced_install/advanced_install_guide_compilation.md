# Advanced Installation - Compilation From Source

> **NOTE:** Installation of Deep Learning Streamer Pipeline Framework
> [from pre-built Debian packages using one-click script](../../install/install_guide_ubuntu.md)
> is the easiest approach.

The instructions below are intended for building Deep Learning Streamer Pipeline Framework
from the source code provided in

[Open Edge Platform repository](https://github.com/open-edge-platform/edge-ai-libraries).

> **NOTE:** The `main` branch currently targets a **nightly (pre-release) build
> of OpenVINO™ GenAI**, not an official stable release. The exact nightly
> version/date used below may become outdated over time; check
> `OPENVINO_GENAI_VERSION` in
> [dependencies/versions.env](https://github.com/open-edge-platform/dlstreamer/blob/main/dependencies/versions.env)
> for the version currently used in CI, and substitute it below if it differs.

## Step 1: Install prerequisites (only for Ubuntu)

Follow the instructions in
[the prerequisites](../../install/install_guide_ubuntu.md#prerequisites) section.

## Step 2: Install build dependencies

<!--hide_directive::::{tab-set}
:::{tab-item}hide_directive--> Ubuntu 24
<!--hide_directive:sync: tab1hide_directive-->

  ```bash
  sudo apt-get update && \
  sudo apt-get install -y wget vainfo xz-utils python3-pip python3-gi gcc-multilib libglib2.0-dev \
      flex bison autoconf automake libtool libogg-dev make g++ libva-dev yasm libglx-dev libdrm-dev \
      python-gi-dev python3-dev unzip libgflags-dev libcurl4-openssl-dev \
      libgirepository1.0-dev libx265-dev libx264-dev libde265-dev gudev-1.0 libusb-1.0 nasm python3-venv \
      libcairo2-dev libxt-dev libgirepository1.0-dev libgles2-mesa-dev wayland-protocols \
      libssh2-1-dev cmake git valgrind numactl libvpx-dev libopus-dev libsrtp2-dev libxv-dev \
      linux-libc-dev libpmix2t64 libhwloc15 libhwloc-plugins libxcb1-dev libx11-xcb-dev \
      ffmpeg libpaho-mqtt-dev libopencv-dev libpostproc-dev libavfilter-dev libavdevice-dev \
      libswscale-dev libswresample-dev libavutil-dev libavformat-dev libavcodec-dev libtbb12 libxml2-dev \
      ocl-icd-opencl-dev
  ```

<!--hide_directive:::
:::{tab-item}hide_directive--> Ubuntu 22
<!--hide_directive:sync: tab2hide_directive-->

  ```bash
  sudo apt-get update && \
  sudo apt-get install -y wget vainfo xz-utils python3-pip python3-gi gcc-multilib libglib2.0-dev \
      flex bison autoconf automake libtool libogg-dev make g++ libva-dev yasm libglx-dev libdrm-dev \
      python-gi-dev python3-dev unzip libgflags-dev \
      libgirepository1.0-dev libx265-dev libx264-dev libde265-dev gudev-1.0 libusb-1.0 nasm python3-venv \
      libcairo2-dev libxt-dev libgirepository1.0-dev libgles2-mesa-dev wayland-protocols libcurl4-openssl-dev \
      libssh2-1-dev cmake git valgrind numactl libvpx-dev libopus-dev libsrtp2-dev libxv-dev \
      linux-libc-dev libpmix2 libhwloc15 libhwloc-plugins libxcb1-dev libx11-xcb-dev \
      ffmpeg libpaho-mqtt-dev libpostproc-dev libavfilter-dev libavdevice-dev \
      libswscale-dev libswresample-dev libavutil-dev libavformat-dev libavcodec-dev libtbb12  libxml2-dev ocl-icd-opencl-dev \
      opencl-headers
  ```

<!--hide_directive:::
:::{tab-item}hide_directive--> Fedora 41
<!--hide_directive:sync: tab3hide_directive-->

  ```bash
  sudo dnf install -y \
      https://download1.rpmfusion.org/free/fedora/rpmfusion-free-release-$(rpm -E %fedora).noarch.rpm \
      https://download1.rpmfusion.org/nonfree/fedora/rpmfusion-nonfree-release-$(rpm -E %fedora).noarch.rpm
  sudo dnf install -y wget libva-utils xz python3-pip python3-gobject gcc gcc-c++ glibc-devel glib2-devel \
      flex bison autoconf automake libtool libogg-devel make libva-devel yasm mesa-libGL-devel libdrm-devel \
      python3-gobject-devel python3-devel tbb gnupg2 unzip opencv-devel gflags-devel openssl-devel openssl-devel-engine \
      gobject-introspection-devel x265-devel x264-devel libde265-devel libgudev-devel libusb1 libusb1-devel nasm python3-virtualenv \
      cairo-devel cairo-gobject-devel libXt-devel mesa-libGLES-devel wayland-protocols-devel libcurl-devel which \
      libssh2-devel cmake git valgrind numactl libvpx-devel opus-devel libsrtp-devel libXv-devel paho-c-devel \
      kernel-headers pmix pmix-devel hwloc hwloc-libs hwloc-devel libxcb-devel libX11-devel libatomic intel-media-driver
  ```

<!--hide_directive:::
:::{tab-item}hide_directive--> EMT 3.x
<!--hide_directive:sync: tab3hide_directive-->

  ```bash
  sudo dnf install -y uuid libuuid-devel openssl-devel gcc gcc-c++ make curl ca-certificates librdkafka-devel libva-devel alsa-lib-devel unzip glibc libstdc++ libgcc cmake sudo pkgconf pkgconf-pkg-config ocl-icd-devel libva-intel-media-driver python3-devel libXaw-devel ncurses-devel libva2 intel-compute-runtime intel-opencl intel-level-zero-gpu intel-ocloc-devel nasm
  ```

<!--hide_directive:::
::::hide_directive-->

## Step 3: Clone Deep Learning Streamer repository

  ```bash
  cd ~
  git clone --branch main --recursive https://github.com/open-edge-platform/dlstreamer.git
  cd dlstreamer
  ```

## Step 4: Set up a Python environment

DL Streamer's Python plugins (`gvapython` and GStreamer Python elements) embed
the **system `python3`** of your distribution, so the Python virtual environment
must be based on it - do not use a Python downloaded by `uv` or another tool.
The `scripts/setup_python_env.sh` script takes care of that on every supported
distribution. Installing `uv` is recommended (the script falls back to
`python3 -m venv` and `pip` otherwise):

  ```bash
  curl -LsSf https://astral.sh/uv/install.sh | sh
  ```

Create the virtual environment with the build tools (`meson`, `ninja`):

  ```bash
  cd ~/dlstreamer
  ./scripts/setup_python_env.sh --venv ~/python3venv --groups build --no-pth
  source ~/python3venv/bin/activate
  ```

## Step 5: Install OpenVINO™ GenAI

On Ubuntu and EMT, the OpenVINO™ GenAI archive bundles the full OpenVINO™ C++
runtime alongside the GenAI libraries, so this step alone is enough to build
DL Streamer - no separate "plain" OpenVINO™ Toolkit install is needed on those
platforms. Fedora 41 instead installs the official OpenVINO™ Toolkit 2026.3.1
package from Intel's YUM repository, as in the Fedora 41 Dockerfile. The
separate OpenVINO™ GenAI archive is required if you want to use the
[gvagenai element](https://docs.openedgeplatform.intel.com/dev/edge-ai-libraries/dlstreamer/elements/gvagenai.html),
which needs the [OpenVINO GenAI archive](https://docs.openvino.ai/2026/get-started/install-openvino/install-openvino-genai.html) package.

<!--hide_directive::::{tab-set}
:::{tab-item}hide_directive--> Ubuntu 24
<!--hide_directive:sync: tab1hide_directive-->

  ```bash
  wget -O- https://storage.openvinotoolkit.org/repositories/openvino_genai/packages/nightly/2026.5.0.0.dev20261027/openvino_genai_ubuntu24_2026.5.0.0.dev20261027_x86_64.tar.gz | tar -xz &&
  sudo mv openvino_genai_ubuntu24_2026.5.0.0.dev20261027_x86_64 /opt/intel/openvino_genai
  source /opt/intel/openvino_genai/setupvars.sh
  ```

<!--hide_directive:::
:::{tab-item}hide_directive--> Ubuntu 22
<!--hide_directive:sync: tab2hide_directive-->

  ```bash
  wget -O- https://storage.openvinotoolkit.org/repositories/openvino_genai/packages/nightly/2026.5.0.0.dev20261027/openvino_genai_ubuntu22_2026.5.0.0.dev20261027_x86_64.tar.gz | tar -xz &&
  sudo mv openvino_genai_ubuntu22_2026.5.0.0.dev20261027_x86_64 /opt/intel/openvino_genai
  source /opt/intel/openvino_genai/setupvars.sh
  ```

<!--hide_directive:::
:::{tab-item}hide_directive--> Ubuntu 26
<!--hide_directive:sync: tab5hide_directive-->

  ```bash
  wget -O- https://storage.openvinotoolkit.org/repositories/openvino_genai/packages/nightly/2026.5.0.0.dev20261027/openvino_genai_ubuntu26_2026.5.0.0.dev20261027_x86_64.tar.gz | tar -xz &&
  sudo mv openvino_genai_ubuntu26_2026.5.0.0.dev20261027_x86_64 /opt/intel/openvino_genai
  source /opt/intel/openvino_genai/setupvars.sh
  ```

<!--hide_directive:::
:::{tab-item}hide_directive--> Fedora 41
<!--hide_directive:sync: tab3hide_directive-->

  ```bash
sudo tee /etc/yum.repos.d/openvino.repo >/dev/null <<'EOF'
[OpenVINO]
name=Intel(R) Distribution of OpenVINO
baseurl=https://yum.repos.intel.com/openvino
enabled=1
gpgcheck=1
repo_gpgcheck=1
gpgkey=https://yum.repos.intel.com/intel-gpg-keys/GPG-PUB-KEY-INTEL-SW-PRODUCTS.PUB
EOF
sudo dnf install -y openvino-2026.3.1
```

> **NOTE:** No Fedora-specific nightly OpenVINO™ GenAI archive is published.
> These instructions install the official OpenVINO™ Toolkit 2026.3.1 package
> from Intel's YUM repository, matching the Fedora 41 Dockerfile.
>
> The model download scripts (`scripts/download_models`) install the nightly
> OpenVINO™ Python wheels pinned in `requirements/constraints.txt`. A model
> exported with a newer OpenVINO™ may not load in this older runtime, so on
> Fedora reinstall the matching wheel in the download environment before
> exporting: `uv pip install openvino~=2026.3.1`.

<!--hide_directive:::
:::{tab-item}hide_directive--> EMT
<!--hide_directive:sync: tab4hide_directive-->

  ```bash
  wget -O- https://storage.openvinotoolkit.org/repositories/openvino_genai/packages/nightly/2026.5.0.0.dev20261027/openvino_genai_ubuntu24_2026.5.0.0.dev20261027_x86_64.tar.gz | tar -xz &&
  sudo mv openvino_genai_ubuntu24_2026.5.0.0.dev20261027_x86_64 /opt/intel/openvino_genai
  source /opt/intel/openvino_genai/setupvars.sh
  ```

  > **NOTE:** No EMT-specific OpenVINO™ GenAI archive is published; the
  > `ubuntu24` archive is used instead, matching the "plain" OpenVINO™
  > Toolkit archive flavor EMT has always relied on for ABI compatibility.

<!--hide_directive:::
::::hide_directive-->

## Step 6: Build Deep Learning Streamer

To build DL Streamer is it recommended to use the provided makefile for ease of use:

  ```bash
  make build
  ```

Running this command will build any major missing dependencies and then compile DL Streamer itself.

## Step 7: Install Deep Learning Streamer (optional)

After building DL Streamer you can install it on your local system by running:

  ```bash
  sudo -E make install
  ```

## Step 8: Set up environment

Set up the required environment variables:

<!--hide_directive::::{tab-set}
:::{tab-item}hide_directive--> Ubuntu
<!--hide_directive:sync: tab1hide_directive-->

  ```bash
  export LIBVA_DRIVER_NAME=iHD
  export GST_PLUGIN_PATH="/opt/intel/dlstreamer/Release/lib:/opt/intel/dlstreamer/gstreamer/lib/gstreamer-1.0:$HOME/dlstreamer/build/intel64/Release/lib:$HOME/dlstreamer/build/deps/gstreamer-bin/lib/gstreamer-1.0:$GST_PLUGIN_PATH"
  export LD_LIBRARY_PATH="/opt/intel/dlstreamer/Release/lib:/opt/intel/dlstreamer/gstreamer/lib:/opt/intel/dlstreamer/opencv/lib:/opt/intel/dlstreamer/rdkafka/lib:$HOME/dlstreamer/build/intel64/Release/lib:$HOME/dlstreamer/build/deps/gstreamer-bin/lib:$HOME/dlstreamer/build/deps/opencv-bin/lib:$HOME/dlstreamer/build/deps/rdkafka-bin/lib:$LD_LIBRARY_PATH"
  export LIBVA_DRIVERS_PATH="/usr/lib/x86_64-linux-gnu/dri"
  export GST_VA_ALL_DRIVERS="1"
  export PATH="/opt/intel/dlstreamer/Release/bin:/opt/intel/dlstreamer/gstreamer/bin:/opt/intel/dlstreamer/opencv/bin:$HOME/dlstreamer/build/intel64/Release/bin:$HOME/dlstreamer/build/deps/gstreamer-bin/bin:$HOME/dlstreamer/build/deps/opencv-bin/bin:$HOME/.local/bin:$HOME/python3venv/bin:$PATH"
  export PKG_CONFIG_PATH="/opt/intel/dlstreamer/Release/lib/pkgconfig:/opt/intel/dlstreamer/gstreamer/lib/pkgconfig::$HOME/dlstreamer/build/intel64/Release/lib/pkgconfig:$HOME/dlstreamer/build/deps/gstreamer-bin/lib/pkgconfig:$PKG_CONFIG_PATH"
  export GST_PLUGIN_FEATURE_RANK=${GST_PLUGIN_FEATURE_RANK},ximagesink:MAX
  export GI_TYPELIB_PATH="/opt/intel/dlstreamer/gstreamer/lib/girepository-1.0:$HOME/dlstreamer/build/src/gst/metadata/:$HOME/dlstreamer/build/deps/gstreamer-bin/lib/girepository-1.0:/usr/lib/x86_64-linux-gnu/girepository-1.0"
  ```

<!--hide_directive:::
:::{tab-item}hide_directive--> Fedora
<!--hide_directive:sync: tab2hide_directive-->

  ```bash
  export LIBVA_DRIVER_NAME=iHD
  export GST_PLUGIN_PATH="/opt/intel/dlstreamer/Release/lib:/opt/intel/dlstreamer/gstreamer/lib/gstreamer-1.0:$HOME/dlstreamer/build/intel64/Release/lib:$HOME/dlstreamer/build/deps/gstreamer-bin/lib/gstreamer-1.0:$GST_PLUGIN_PATH"
  export LD_LIBRARY_PATH="/opt/intel/dlstreamer/Release/lib:/opt/intel/dlstreamer/gstreamer/lib:/opt/intel/dlstreamer/opencv/lib:$HOME/dlstreamer/build/intel64/Release/lib:$HOME/dlstreamer/build/deps/gstreamer-bin/lib:$HOME/dlstreamer/build/deps/opencv-bin/lib:$HOME/dlstreamer/build/deps/rdkafka-bin/lib:$LD_LIBRARY_PATH"
  export LIBVA_DRIVERS_PATH="/usr/lib64/dri-nonfree"
  export GST_VA_ALL_DRIVERS="1"
  export PATH="/opt/intel/dlstreamer/Release/bin:/opt/intel/dlstreamer/gstreamer/bin:/opt/intel/dlstreamer/opencv/bin:$HOME/dlstreamer/build/intel64/Release/bin:$HOME/dlstreamer/build/deps/gstreamer-bin/bin:$HOME/dlstreamer/build/deps/opencv-bin/bin:$HOME/.local/bin:$HOME/python3venv/bin:$PATH"
  export PKG_CONFIG_PATH="/opt/intel/dlstreamer/Release/lib/pkgconfig:/opt/intel/dlstreamer/gstreamer/lib/pkgconfig::$HOME/dlstreamer/build/intel64/Release/lib/pkgconfig:$HOME/dlstreamer/build/deps/gstreamer-bin/lib/pkgconfig:$PKG_CONFIG_PATH"
  export GST_PLUGIN_FEATURE_RANK=${GST_PLUGIN_FEATURE_RANK},ximagesink:MAX
  ```

<!--hide_directive:::
:::{tab-item}hide_directive--> EMT
<!--hide_directive:sync: tab3hide_directive-->

  Enable `i915` graphics driver in the system:

  ```bash
  sudo vim /etc/default/grub
  ### Extend the GRUB_CMDLINE_LINUX with i915.force_probe=* ###
  sudo grub2-mkconfig -o /boot/grub2/grub.cfg "$@"
  sudo reboot
  ```

  After a reboot, before trying the Deep Learning Streamer pipelines, you can `export` the
  following environment variables for the current terminal session (temporary solution):

  ```bash
  export LIBVA_DRIVER_NAME=iHD
  export GST_PLUGIN_PATH="/opt/intel/dlstreamer/Release/lib:/opt/intel/dlstreamer/gstreamer/lib/gstreamer-1.0:$HOME/dlstreamer/build/intel64/Release/lib:$HOME/dlstreamer/build/deps/gstreamer-bin/lib/gstreamer-1.0:$GST_PLUGIN_PATH"
  export LD_LIBRARY_PATH="/opt/intel/dlstreamer/Release/lib:/opt/intel/dlstreamer/gstreamer/lib:/opt/intel/dlstreamer/opencv/lib:$HOME/dlstreamer/build/intel64/Release/lib:$HOME/dlstreamer/build/deps/gstreamer-bin/lib:$HOME/dlstreamer/build/deps/opencv-bin/lib:$HOME/dlstreamer/build/deps/rdkafka-bin/lib:$LD_LIBRARY_PATH"
  export LIBVA_DRIVERS_PATH="/usr/lib/dri"
  export GST_VA_ALL_DRIVERS="1"
  export PATH="/opt/intel/dlstreamer/Release/bin:/opt/intel/dlstreamer/gstreamer/bin:/opt/intel/dlstreamer/opencv/bin:$HOME/dlstreamer/build/intel64/Release/bin:$HOME/dlstreamer/build/deps/gstreamer-bin/bin:$HOME/dlstreamer/build/deps/opencv-bin/bin:$HOME/.local/bin:$HOME/python3venv/bin:$PATH"
  export PKG_CONFIG_PATH="/opt/intel/dlstreamer/Release/lib/pkgconfig:/opt/intel/dlstreamer/gstreamer/lib/pkgconfig::$HOME/dlstreamer/build/intel64/Release/lib/pkgconfig:$HOME/dlstreamer/build/deps/gstreamer-bin/lib/pkgconfig:$PKG_CONFIG_PATH"
  export GST_PLUGIN_FEATURE_RANK=${GST_PLUGIN_FEATURE_RANK},ximagesink:MAX
  ```

<!--hide_directive:::
::::hide_directive-->

> **NOTE:**  For a permanent solution, open `\~/.bashrc` and add the variables above
> to set up Linux to use them for every terminal session.
>
> `PYTHONPATH` is not needed: DL Streamer's Python modules are linked into the
> Python environment by `scripts/setup_python_env.sh` (Step 9).

## Step 9: Install Python dependencies (optional)

If you intend to use Python elements, Python samples or the pipeline optimizer,
install the Python dependencies into the `~/python3venv` environment from
[Step 4](#step-4-set-up-a-python-environment). PyGObject is built from source,
so install its build dependencies first:

<!--hide_directive::::{tab-set}
:::{tab-item}hide_directive--> Ubuntu
<!--hide_directive:sync: tab1hide_directive-->

  ```bash
  sudo apt-get update
  sudo apt-get install -y -q --no-install-recommends gcc cmake python-gi-dev python3-dev \
      libglib2.0-dev libcairo2-dev libopencv-objdetect-dev libopencv-photo-dev libopencv-stitching-dev libopencv-video-dev \
      libopencv-calib3d-dev libopencv-core-dev libopencv-dnn-dev libgirepository1.0-dev
  ```

<!--hide_directive:::
:::{tab-item}hide_directive--> Fedora / EMT
<!--hide_directive:sync: tab2hide_directive-->

  ```bash
  sudo dnf install -y gcc cmake python3-devel python3-gobject-devel glib2-devel cairo-devel \
      cairo-gobject-devel gobject-introspection-devel
  ```

<!--hide_directive:::
::::hide_directive-->

Then install the dependencies and link DL Streamer's Python modules into the
environment. Use `--dlstreamer-dir /opt/intel/dlstreamer` if you installed
DL Streamer in [Step 7](#step-7-install-deep-learning-streamer-optional), or
`--dlstreamer-dir ~/dlstreamer` to use the build tree:

  ```bash
  ~/dlstreamer/scripts/setup_python_env.sh --venv ~/python3venv --dlstreamer-dir /opt/intel/dlstreamer
  source ~/python3venv/bin/activate
  ```

The script installs the requirement groups from
[requirements/](https://github.com/open-edge-platform/dlstreamer/tree/main/requirements)
(`runtime`, `onvif` and `optimizer` by default), writes a `dlstreamer.pth` file
that makes `gstgva`, `dlstreamer.onvif` and the GStreamer Python overrides
importable without `PYTHONPATH`, and verifies that the environment's Python
matches the one DL Streamer's Python plugins were built against.

> **NOTE:** Python elements run in an interpreter embedded in GStreamer, which
> uses the first `python3` found on `PATH`. Keep `~/python3venv` activated (or
> its `bin/` directory first on `PATH`) when running pipelines.

### (Optional) Install DL Streamer ONVIF Python package

If you want to use the `dlstreamer.onvif` in your own Python code,
install the `intel-dlstreamer` wheel on top of the DL Streamer.
See [ONVIF sample](https://github.com/open-edge-platform/dlstreamer/tree/main/samples/gstreamer/python/onvif_cameras_discovery) for more details of ONVIF support.

The easiest way is to download and install the wheel directly from the GitHub Release:

  ```bash
  uv pip install https://github.com/open-edge-platform/dlstreamer/releases/download/v2026.2.0/intel_dlstreamer-2026.2.0-py3-none-any.whl
  ```

Alternatively, build the wheel from the cloned sources:

  ```bash
  uv build --wheel ~/dlstreamer/python
  uv pip install ~/dlstreamer/python/dist/intel_dlstreamer-*.whl
  ```

This installs:

- `dlstreamer.onvif` — ONVIF camera discovery and pipeline integration
- All required Python dependencies (`numpy`, `onvif-zeep`, `urllib3`) automatically

After installation, no `PYTHONPATH` configuration is needed:

  ```python
  from dlstreamer.onvif import create_video_engine, discover_onvif_cameras
  ```

> **NOTE:** The `~/python3venv` environment from Step 9 already provides
> `dlstreamer.onvif` (through `dlstreamer.pth`) together with its dependencies,
> so the wheel is only needed for other Python environments.

---

*Intel, the Intel logo, OpenVINO, and the OpenVINO logo are trademarks of Intel Corporation or its subsidiaries.*
