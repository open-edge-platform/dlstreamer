# Advanced Uninstallation On Ubuntu

This guide presents several methods of uninstalling Deep Learning Streamer Pipeline Framework
from Ubuntu operating system.

## Option #1: APT repository

If you installed Deep Learning Streamer via apt package, simply uninstall it, using:

```bash
sudo dpkg-query -l | awk '/^ii/ && /intel-dlstreamer/ {print $2}' | sudo xargs apt-get remove -y --purge
```

## Option #2: Docker

If you used docker, you just need to remove the container and the `dlstreamer:devel` docker
image.

```bash
docker rm <container-name>
docker rmi dlstreamer:devel
```

## Option #3: Compiled version

If you compiled Deep Learning Streamer from source, use the instructions below.

### Step 1: Uninstall apt packages

```bash
sudo apt-get remove intel-dlstreamer-gst libpython3-dev python-gi-dev libopencv-dev libva-dev
```

### Step 2: Uninstall Python dependencies

```bash
cd ~/intel/dlstreamer_gst
sudo python3 -m pip uninstall -r requirements.txt
```

### Step 3: Uninstall optional components

> **NOTE:** If you have not installed any optional/additional components, you can
> skip this step.

Uninstall OpenCL™:

```bash
sudo apt remove intel-opencl-icd ocl-icd-opencl-dev opencl-clhpp-headers
```

Uninstall Intel® oneAPI DPC++/C++ Compiler:

```bash
sudo apt remove intel-oneapi-compiler-dpcpp-cpp intel-level-zero-gpu level-zero-dev
```

Uninstall VA-API dependencies:

```bash
sudo apt remove intel-dlstreamer-gst-vaapi libva-dev vainfo intel-media-va-driver-non-free
```

### Step 4: Remove source directory

```bash
rm -rf ~/intel/dlstreamer_gst
```

------------------------------------------------------------------------

> **\*** *Other names and brands may be claimed as the property of
> others.*
