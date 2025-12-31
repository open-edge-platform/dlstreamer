# Install Guide Ubuntu

The easiest way to install Deep Learning Streamer Pipeline Framework is
[using Debian packages from APT repository](#option-1-install-deep-learning-streamer-pipeline-framework-from-debian-packages-using-apt-repository).

If you prefer a containerized environment based on Docker, download a pre-made
[Docker image](#option-2-install-docker-image-from-docker-hub-and-run-it) or
build it with a Dockerfile. Regardless of the installation path,
make sure to [set up the prerequisites](#prerequisites) first.

For a detailed description of the installation process, including the option
of building Deep Learning Streamer Pipeline Framework from source code,
follow the [advanced installation guide](../../dev_guide/advanced_install/advanced_install_guide_compilation.md).

## Prerequisites

To use GPU and/or NPU as an inference device or to use graphics
hardware encoding/decoding capabilities, you need to install
appropriate drivers. Use the script below to detect available
device(s) and install the drivers. Also, pay attention to the
displayed information, as the script uses multiple references to other Intel®
resources when additional configuration is required.

### Step 1: Download the installation script

```bash
mkdir -p ~/intel/dlstreamer_gst
cd ~/intel/dlstreamer_gst/
wget -O DLS_install_prerequisites.sh https://raw.githubusercontent.com/open-edge-platform/edge-ai-libraries/main/libraries/dl-streamer/scripts/DLS_install_prerequisites.sh && chmod +x DLS_install_prerequisites.sh
```

### Step 2: Execute the script and follow its instructions

```bash
./DLS_install_prerequisites.sh
```

The essential packages needed for most Intel® Client GPU users are installed:

```bash
GPU:
   libze-intel-gpu1
   libze1
   intel-opencl-icd
   clinfo
   intel-gsc
Media:
   intel-media-va-driver-non-free
NPU:
   intel-driver-compiler-npu
   intel-fw-npu
   intel-level-zero-npu
   level-zero
```

More details about the packages can be found in:

- [Intel® Client GPU](https://dgpu-docs.intel.com/driver/client/overview.html#installing-gpu-packages).
- [Media](https://github.com/intel/media-driver/releases).
- [NPU](https://github.com/intel/linux-npu-driver/releases/tag/v1.13.0).

To run Deep Learning Streamer on Intel® Data Center GPU (Flex) you need to use specific
drivers, and follow the instructions on the
[Intel® Data Center GPU website](https://dgpu-docs.intel.com/driver/installation.html#installing-data-center-gpu-lts-releases).

## Option #1: Install Deep Learning Streamer Pipeline Framework from Debian packages using APT repository

This option provides the simplest installation flow using the `apt-get`
install command.

### Step 1: Installing prerequisites

Run the script `DLS_install_prerequisites.sh` to install the required GPU/NPU
drivers. For more details see [prerequisites](#prerequisites).

```bash
./DLS_install_prerequisites.sh
```

### Step 2: Set up the repositories

- **In Ubuntu 22**

  ```bash
  sudo -E wget -O- https://apt.repos.intel.com/intel-gpg-keys/GPG-PUB-KEY-INTEL-SW-PRODUCTS.PUB | gpg --dearmor | sudo tee /usr/share/keyrings/intel-gpg-archive-keyring.gpg > /dev/null
  sudo -E wget -O- https://apt.repos.intel.com/edgeai/dlstreamer/GPG-PUB-KEY-INTEL-DLS.gpg | sudo tee /usr/share/keyrings/dls-archive-keyring.gpg > /dev/null
  echo "deb [signed-by=/usr/share/keyrings/dls-archive-keyring.gpg] https://apt.repos.intel.com/edgeai/dlstreamer/ubuntu22 ubuntu22 main" | sudo tee /etc/apt/sources.list.d/intel-dlstreamer.list
  sudo bash -c 'echo "deb [signed-by=/usr/share/keyrings/intel-gpg-archive-keyring.gpg] https://apt.repos.intel.com/openvino/2025 ubuntu22 main" | sudo tee /etc/apt/sources.list.d/intel-openvino-2025.list'
  ```

- **Ubuntu 24**

  ```bash
  sudo -E wget -O- https://apt.repos.intel.com/intel-gpg-keys/GPG-PUB-KEY-INTEL-SW-PRODUCTS.PUB | gpg --dearmor | sudo tee /usr/share/keyrings/intel-gpg-archive-keyring.gpg > /dev/null
  sudo -E wget -O- https://apt.repos.intel.com/edgeai/dlstreamer/GPG-PUB-KEY-INTEL-DLS.gpg | sudo tee /usr/share/keyrings/dls-archive-keyring.gpg > /dev/null
  echo "deb [signed-by=/usr/share/keyrings/dls-archive-keyring.gpg] https://apt.repos.intel.com/edgeai/dlstreamer/ubuntu24 ubuntu24 main" | sudo tee /etc/apt/sources.list.d/intel-dlstreamer.list
  sudo bash -c 'echo "deb [signed-by=/usr/share/keyrings/intel-gpg-archive-keyring.gpg] https://apt.repos.intel.com/openvino/2025 ubuntu24 main" | sudo tee /etc/apt/sources.list.d/intel-openvino-2025.list'
  ```

  > **NOTE:** If you have OpenVINO™ installed in any version other than 2025.3.0,
  > please uninstall the OpenVINO™ packages using the following commands.

  ```bash
  sudo apt remove -y openvino* libopenvino-* python3-openvino*
  sudo apt-get autoremove
  ```

### Step 3: Install the Deep Learning Streamer Pipeline Framework

> **NOTE:** This step will also install the required dependencies, including the
> OpenVINO™ toolkit and GStreamer.

```bash
sudo apt update
sudo apt-get install intel-dlstreamer
```

**Congratulations! Deep Learning Streamer is now installed and ready for
use!**

To see the full list of installed components check the
[Dockerfile content for Ubuntu 24](https://github.com/open-edge-platform/dlstreamer/blob/master/docker/ubuntu/ubuntu24.Dockerfile)

### [Optional] Step 4: Python dependencies

The Python packages required to run Deep Learning Streamer python elements
or samples are not installed by default. You can install them using
the commands from
[Advanced Install Guide Compilation / Install Python dependencies](../../dev_guide/advanced_install/advanced_install_guide_compilation.md#step-9-install-python-dependencies-optional).

### [Optional] Step 5: Post installation steps

#### Download the model and run hello_dlstreamer script

Before executing any scripts, ensure you have set the MODELS_PATH
environment variable to the directory where the model will be downloaded
or where it already exists. The `hello_dlstreamer.sh` script assumes the
availability of the YOLO11s model. If you do not have it, download it
using the following command:

> **NOTE:**
>
> - The `download_public_models.sh` script will download the YOLO11s model
>   from the Ultralytics website along with other required components and
>   convert it to the OpenVINO™ format.
>
> - If you add the `coco128` argument to the script, the downloaded model
>   will also be quantized to the INT8 precision.
>
> - If you already have the model, skip this step and simply export the
>   MODELS_PATH and execute the `hello_dlstreamer.sh` script.

```bash
mkdir -p /home/${USER}/models
export MODELS_PATH=/home/${USER}/models
/opt/intel/dlstreamer/samples/download_public_models.sh yolo11s coco128
```

The `hello_dlstreamer.sh` script will set up the required environment
variables and runs a sample pipeline to confirm that Deep Learning Streamer
is installed correctly. To run the `hello_dlstreamer.sh` script, execute the
following command:

```bash
/opt/intel/dlstreamer/scripts/hello_dlstreamer.sh
```

> **NOTE:** To set up Linux with the relevant environment variables every time a new
> terminal is opened, open `~/.bashrc` and add the following lines:

- **Ubuntu 24**

  ```bash
  export LIBVA_DRIVER_NAME=iHD
  export GST_PLUGIN_PATH=/opt/intel/dlstreamer/lib:/opt/intel/dlstreamer/gstreamer/lib/gstreamer-1.0:/opt/intel/dlstreamer/streamer/lib/
  export LD_LIBRARY_PATH=/opt/intel/dlstreamer/gstreamer/lib:/opt/intel/dlstreamer/lib:/opt/intel/dlstreamer/lib/gstreamer-1.0:/sr/lib:/opt/intel/dlstreamer/lib:/usr/local/lib/gstreamer-1.0:/usr/local/lib:/opt/opencv:/opt/rdkafka
  export LIBVA_DRIVERS_PATH=/usr/lib/x86_64-linux-gnu/dri
  export GST_VA_ALL_DRIVERS=1
  export PATH=/opt/intel/dlstreamer/gstreamer/bin:/opt/intel/dlstreamer/bin:$PATH
  export GST_PLUGIN_FEATURE_RANK=${GST_PLUGIN_FEATURE_RANK},ximagesink:MAX
  export GI_TYPELIB_PATH=/opt/intel/dlstreamer/gstreamer/lib/girepository-1.0:/usr/lib/x86_64-linux-gnu/girepository-1.0
  ```

- **Ubuntu 22**

  ```bash

  export LIBVA_DRIVER_NAME=iHD
  export GST_PLUGIN_PATH=/opt/intel/dlstreamer/lib:/opt/intel/dlstreamer/gstreamer/lib/gstreamer-1.0:/opt/intel/dlstreamer/streamer/lib/
  export LD_LIBRARY_PATH=/opt/intel/dlstreamer/gstreamer/lib:/opt/intel/dlstreamer/lib:/opt/intel/dlstreamer/lib/gstreamer-1.0:/sr/lib:/opt/intel/dlstreamer/lib:/usr/local/lib/gstreamer-1.0:/usr/local/lib:/opt/opencv:/opt/rdkafka
  export LIBVA_DRIVERS_PATH=/usr/lib/x86_64-linux-gnu/dri
  export GST_VA_ALL_DRIVERS=1
  export PATH=/opt/intel/dlstreamer/gstreamer/bin:/opt/intel/dlstreamer/bin:$PATH
  export GST_PLUGIN_FEATURE_RANK=${GST_PLUGIN_FEATURE_RANK},ximagesink:MAX
  export GI_TYPELIB_PATH=/opt/intel/dlstreamer/gstreamer/lib/girepository-1.0:/usr/lib/x86_64-linux-gnu/girepository-1.0
  ```

- **Fedora 41**

  ```bash
  export LIBVA_DRIVER_NAME=iHD
  export GST_PLUGIN_PATH=/opt/intel/dlstreamer/lib:/opt/intel/dlstreamer/gstreamer/lib/gstreamer-1.0:/opt/intel/dlstreamer/gstreamer/lib/
  export LD_LIBRARY_PATH=/opt/intel/dlstreamer/gstreamer/lib:/opt/intel/dlstreamer/lib:/opt/intel/dlstreamer/lib/gstreamer-1.0:/usr/lib:/opt/intel/dlstreamer/lib:/usr/local/lib/gstreamer-1.0:/usr/local/lib:/opt/opencv:/opt/rdkafka:/opt/ffmpeg
  export LIBVA_DRIVERS_PATH=/usr/lib64/dri-nonfree
  export GST_VA_ALL_DRIVERS=1
  export PATH=/opt/intel/dlstreamer/gstreamer/bin:/opt/intel/dlstreamer/bin:$PATH
  export GST_PLUGIN_FEATURE_RANK=${GST_PLUGIN_FEATURE_RANK},ximagesink:MAX
  export GI_TYPELIB_PATH=/opt/intel/dlstreamer/gstreamer/lib/girepository-1.0:/usr/lib/x86_64-linux-gnu/girepository-1.0
  ```

  or run:

  ```bash
  source /opt/intel/dlstreamer/scripts/setup_dls_env.sh
  ```

  to configure environment variables for the current terminal session.

### [Optional] Step 6: Auxiliary installation steps

A. Check for installed packages and versions

```bash
apt list --installed | grep intel-dlstreamer
```

B. To install a specific version run the following command:

```bash
sudo apt install intel-dlstreamer=<VERSION>.<UPDATE>.<PATCH>
```

For example

```bash
sudo apt install intel-dlstreamer=2025.2.0
```

C. List available versions

```bash
sudo apt show -a intel-dlstreamer
```

## Option #2: Install Docker image from Docker Hub and run it

### Step 1: Installation of prerequisites

Run the script `DLS_install_prerequisites.sh` to setup your environment.
For more details see [prerequisites](#prerequisites).

```bash
./DLS_install_prerequisites.sh
```

### Step 2: Installation of Docker

[Get Docker](https://docs.docker.com/get-docker/) for your host OS.
To prevent file permission issues, check how to
[manage Docker as a non-root user](https://docs.docker.com/engine/install/linux-postinstall/).

### Step 3: Allowing connection to X server

Some Pipeline Framework samples use display. Hence, first run the
following commands to allow connection from Docker container to X server
on host:

```bash
xhost local:root
setfacl -m user:1000:r ~/.Xauthority
```

> **NOTE**: If you want to build Docker image from DLStreamer Dockerfiles, please
> follow [the instructions](../../dev_guide/advanced_install/advanced_build_docker_image.md).

### Step 4: Pull the Deep Learning Streamer Docker image from Docker Hub

Visit the [Deep Learning Streamer image docker hub](https://hub.docker.com/r/intel/dlstreamer) to
select the most appropriate version. By default, the latest docker image points to Ubuntu
24 version.

For **Ubuntu 22.04**, specify the tag e.g. **2025.2.0-ubuntu22**.
For **Ubuntu 24.04**, use the **latest** tag or specify the version, such as
**2025.2.0-ubuntu24**.

- **Ubuntu 22**

  ```bash
  docker pull intel/dlstreamer:2025.2.0-ubuntu22
  ```

- **Ubuntu 24**

  ```bash
  docker pull intel/dlstreamer:latest
  ```

### Step 5: Run Deep Learning Streamer Pipeline Framework container

To confirm that your installation is completed successfully, please run
a container

- **Ubuntu 22**

  ```bash
  docker run -it intel/dlstreamer:2025.2.0-ubuntu22
  ```

- **Ubuntu 24**

  ```bash
  docker run -it intel/dlstreamer:latest
  ```

In the container, please run the command `gst-inspect-1.0 gvadetect` to confirm
that GStreamer and Deep Learning Streamer are running

```bash
gst-inspect-1.0 gvadetect
```

If your can see the documentation of `gvadetect` element, the
installation process is completed.

![image](gvadetect_sample_help.png)

## Next Steps

You are ready to use Deep Learning Streamer. For further instructions to run
sample pipeline(s), please go to the [tutorial](../tutorial.md).

------------------------------------------------------------------------

> **\*** *Other names and brands may be claimed as the property of
> others.*
