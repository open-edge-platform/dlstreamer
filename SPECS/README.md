# DL Streamer RPM Generation Guide

A comprehensive guide to downloading, building, installing, and distributing DL Streamer RPM packages.

## Table of Contents

1. [Overview](#overview)
2. [Prerequisites](#prerequisites)
3. [Downloading Sources](#downloading-sources)
4. [Building and Installing Packages (Developer Testing)](#building-and-installing-packages-developer-testing)
    - [Uninstall Existing DL Streamer](#uninstall-existing-dl-streamer)
    - [Build and Install Dependent Packages](#build-and-install-dependent-packages)
    - [Install DL Streamer RPM Package](#install-dl-streamer-rpm-package)
5. [Installing Pre-built RPM Packages](#installing-pre-built-rpm-packages)
    - [Configure EdgeAI Repository](#configure-edgeai-repository)
    - [Install Dependencies](#install-dependencies)
    - [Install DL Streamer](#install-dl-streamer)
6. [Validating Installation](#validating-installation)

---

## Overview

This guide details the steps to:

- Download all required sources
- Build RPM and SRPM packages
- Install the resulting RPMs
- Set up and use a YUM/DNF repository for distribution

---

## Prerequisites

- Linux system with `dnf` package manager
- Sudo privileges
- [OpenVINO 2025.4](https://storage.openvinotoolkit.org/repositories/openvino/packages/2025.4/linux/openvino_toolkit_ubuntu24_2025.4.0.20398.8fdad55727d_x86_64.tgz) prebuilt binary

---

## Downloading Sources

If you need to update package versions, edit both the `versions.env` file and the relevant `.spec` files.

To download all required sources, run:

```sh
./download_sources.sh
```

This script will:

- Download major dependencies and DL Streamer sources
- Place them in the correct locations for RPM building

---

## Building and Installing Packages (Developer Testing)

### 1. Install OpenVINO 2025.4

```sh
sudo rm -rf /opt/intel/openvino*
wget https://storage.openvinotoolkit.org/repositories/openvino/packages/2025.4/linux/openvino_toolkit_ubuntu24_2025.4.0.20398.8fdad55727d_x86_64.tgz
tar -xvzf openvino_toolkit_ubuntu24_2025.4.0.20398.8fdad55727d_x86_64.tgz
sudo mv openvino_toolkit_ubuntu24_2025.4.0.20398.8fdad55727d_x86_64 /opt/intel/openvino_2025.4.0
cd /opt/intel/openvino_2025.4.0/
sudo -E python3 -m pip install -r ./python/requirements.txt
cd /opt/intel
sudo ln -s openvino_2025.4.0 openvino_2025
```

### 2. Uninstall Existing DL Streamer

If you have an existing DL Streamer installation, remove it:

```sh
sudo dnf remove -y intel-dlstreamer
```

### 3. Build and Install Dependent Packages

Run the following script to build and install all dependent packages:

```sh
./build_and_install_packages.sh
```

This script will:

- Install build dependencies
- Set up the RPM build environment (`~/rpmbuild`)
- Build and install all packages in dependency order
- Output `.rpm` and `.src.rpm` files to `~/rpmbuild/RPMS/x86_64/` and `~/rpmbuild/SRPMS/`

### 4. Install DL Streamer RPM Package

> **Note**: The flag `--setopt=install_weak_deps=False` is used
> to avoid installation of weaker dependencies which still show up
> even after having `AutoReq: no` in the DL Streamer spec file

To install the DL Streamer RPM, use the following command to avoid installing weak dependencies:

```sh
sudo dnf install -y --setopt=install_weak_deps=False ~/rpmbuild/RPMS/x86_64/intel-dlstreamer-*.rpm
```

Set up the environment:

```sh
source /opt/intel/openvino_2025/setupvars.sh
source /opt/intel/dlstreamer/setupvars.sh
```

---

## Installing Pre-built RPM Packages

### 1. Configure EdgeAI Repository

Create a DNF repo file as a normal user:

```sh
tee > /tmp/edgeai.repo << EOF
[EdgeAI]
name=Edge AI repository
baseurl=https://yum.repos.intel.com/edgeai/
enabled=1
gpgcheck=1
repo_gpgcheck=1
gpgkey=https://yum.repos.intel.com/edgeai/GPG-PUB-KEY-INTEL-DLS.gpg
EOF
```

Move the repo file to the configuration directory:

```sh
sudo mv /tmp/edgeai.repo /etc/yum.repos.d
```

Verify the repository:

```sh
dnf repolist | grep -i EdgeAI
```

### 2. Install Dependencies

```sh
sudo dnf install -y paho-mqtt-c ffmpeg gstreamer opencv
```

### 3. Install DL Streamer

> **Note**: The flag `--setopt=install_weak_deps=False` is used
> to avoid installation of weaker dependencies which still show up
> even after having `AutoReq: no` in the DL Streamer spec file

To install DL Streamer and avoid weak dependencies:

```sh
sudo dnf install --setopt=install_weak_deps=False intel-dlstreamer
```

Set up the environment:

```sh
source /opt/intel/openvino_2025/setupvars.sh
source /opt/intel/dlstreamer/setupvars.sh
```

---

## Validating Installation

After installation, you can try DL Streamer pipelines as described in:

- [Tutorial](../docs/source/get_started/tutorial.rst)
- [Performance Guide](../docs/source/dev_guide/performance_guide.rst)

These resources provide sample pipelines and performance validation steps for DL Streamer GStreamer elements.

---
