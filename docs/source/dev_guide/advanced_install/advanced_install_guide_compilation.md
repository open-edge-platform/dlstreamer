# Advanced Installation - Compilation From Source

> **NOTE:** Installation of Deep Learning Streamer Pipeline Framework
> [from pre-built Debian packages using one-click script](../../get_started/install/install_guide_ubuntu.md)
> is the easiest approach.

The instructions below are intended for building Deep Learning Streamer Pipeline Framework
from the source code provided in

[Open Edge Platform repository](https://github.com/open-edge-platform/edge-ai-libraries.git).

## Step 1: Install prerequisites (only for Ubuntu)

Follow the instructions in
[the prerequisites](../../get_started/install/install_guide_ubuntu.md#prerequisites) section.

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
      libswscale-dev libswresample-dev libavutil-dev libavformat-dev libavcodec-dev libtbb12 libxml2-dev libopencv-dev \
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
      libswscale-dev libswresample-dev libavutil-dev libavformat-dev libavcodec-dev libxml2-dev ocl-icd-opencl-dev \
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

## Step 3: Set up a Python environment

Create a Python virtual environment and install required Python
packages:

```bash
python3 -m venv ~/python3venv
source ~/python3venv/bin/activate

pip install --upgrade pip==24.0
pip install meson==1.4.1 ninja==1.11.1.1
```

## Step 4: Clone Deep Learning Streamer repository

```bash
cd ~
git clone --recursive https://github.com/open-edge-platform/dlstreamer.git
cd dlstreamer
```

## Step 5: Install OpenVINO™ Toolkit

<!--hide_directive::::{tab-set}
:::{tab-item}hide_directive--> Ubuntu/Fedora
<!--hide_directive:sync: tab1hide_directive-->

  ```bash
  cd ~/dlstreamer
  sudo ./scripts/install_dependencies/install_openvino.sh
  ```

  In case of any problems with the installation scripts, [Follow OpenVINO™
  Toolkit instruction guide
  here](https://docs.openvino.ai/2025/get-started/install-openvino/install-openvino-archive-linux.html)
  to install OpenVINO™ on Linux.

  - Environment: **Runtime**
  - Operating System: **Linux**
  - Version: **Latest**
  - Distribution: **OpenVINO™ Archives**

  After successful OpenVINO™ Toolkit package installation, run the
  following commands to install OpenVINO™ Toolkit dependencies and enable
  OpenVINO™ Toolkit development environment:

  ```bash
  sudo -E /opt/intel/openvino_2025/install_dependencies/install_openvino_dependencies.sh
  source /opt/intel/openvino_2025/setupvars.sh
  ```

<!--hide_directive:::
:::{tab-item}hide_directive--> EMT
<!--hide_directive:sync: tab2hide_directive-->

  ```bash
  wget https://storage.openvinotoolkit.org/repositories/openvino/packages/2025.4/linux/openvino_toolkit_ubuntu24_2025.4.0.20398.8fdad55727d_x86_64.tgz
  tar -xvzf openvino_toolkit_ubuntu24_2025.4.0.20398.8fdad55727d_x86_64.tgz
  sudo mv openvino_toolkit_ubuntu24_2025.4.0.20398.8fdad55727d_x86_64.tgz /opt/intel/openvino_2025.4.0
  cd /opt/intel/openvino_2025.4.0/
  sudo -E python3 -m pip install -r ./python/requirements.txt
  cd /opt/intel
  sudo ln -s openvino_2025.4.0 openvino_2025
  ```

<!--hide_directive:::
::::hide_directive-->

### [Optional] Step 6: Install OpenVINO™ GenAI (only for Ubuntu)

To use [gvagenai element](https://docs.openedgeplatform.intel.com/dev/edge-ai-libraries/dl-streamer/elements/gvagenai.html)
there is need to install [OpenVINO GenAI archive](https://docs.openvino.ai/2025/get-started/install-openvino/install-openvino-genai.html) package.

<!--hide_directive::::{tab-set}
:::{tab-item}hide_directive--> Ubuntu 24
<!--hide_directive:sync: tab1hide_directive-->

  ```bash
  curl -L https://storage.openvinotoolkit.org/repositories/openvino_genai/packages/2025.4/linux/openvino_genai_ubuntu24_2025.4.0.0_x86_64.tar.gz | tar -xz &&
  sudo mv openvino_genai_ubuntu24_2025.4.0.0_x86_64 /opt/intel/openvino_genai
  source /opt/intel/openvino_genai/setupvars.sh
  ```

<!--hide_directive:::
:::{tab-item}hide_directive--> Ubuntu 22
<!--hide_directive:sync: tab2hide_directive-->

```bash
curl -L https://storage.openvinotoolkit.org/repositories/openvino_genai/packages/2025.4/linux/openvino_genai_ubuntu22_2025.4.0.0_x86_64.tar.gz | tar -xz &&
mv openvino_genai_ubuntu22_2025.4.0.0_x86_64 /opt/intel/openvino_genai
source /opt/intel/openvino_genai/setupvars.sh
```

<!--hide_directive:::
::::hide_directive-->

## Step 7: Build Deep Learning Streamer

To build DL Streamer is it recommended to use the provided makefile for ease of use:

```bash
make build
```

Running this command will build any major missing dependencies and then compile DL Streamer itself.

## Step 8: Install Deep Learning Streamer (optional)

After building DL Streamer you can install it on your local system by running:

```bash
sudo -E make install
```

## Step 9: Set up environment

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
  export GI_TYPELIB_PATH="/opt/intel/dlstreamer/gstreamer/lib/girepository-1.0:/usr/lib/x86_64-linux-gnu/girepository-1.0gi"
  export PYTHONPATH="/opt/intel/dlstreamer/gstreamer/lib/python3/dist-packages:/opt/intel/dlstreamer/python:/opt/intel/dlstreamer/gstreamer/lib/python3/dist-packages:$PYTHONPATH"
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
  export PYTHONPATH="/opt/intel/dlstreamer/gstreamer/lib/python3/dist-packages:/opt/intel/dlstreamer/python:/opt/intel/dlstreamer/gstreamer/lib/python3/dist-packages:$PYTHONPATH"
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
  export PYTHONPATH="/opt/intel/dlstreamer/gstreamer/lib/python3/dist-packages:/opt/intel/dlstreamer/python:/opt/intel/dlstreamer/gstreamer/lib/python3/dist-packages:$PYTHONPATH"
  ```

<!--hide_directive:::
::::hide_directive-->

> **NOTE:**  For a permament solution, open `\~/.bashrc` and add the variables above
> to set up Linux to use them for every terminal session.

## Step 10: Install Python dependencies (optional)

If you intend to use Python elements or samples, you need to install the
necessary dependencies using the following commands:

```bash
sudo apt-get install -y -q --no-install-recommends gcc cmake python3-full python-gi-dev python3-dev python3-pip \
    libglib2.0-dev libcairo2-dev libopencv-objdetect-dev libopencv-photo-dev libopencv-stitching-dev libopencv-video-dev \
    libopencv-calib3d-dev libopencv-core-dev libopencv-dnn-dev libgirepository1.0-dev

source ~/python3venv/bin/activate
cd ~/dlstreamer
python3 -m pip install -r requirements.txt
```
