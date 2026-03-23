# Advanced Installation On Ubuntu - Prerequisites

If you want to leverage GPU and/or NPU for inference or use graphics
hardware encoding/decoding capabilities, you need to install
appropriate drivers. The easiest way is to use the automated script described in
[the installation guide](../../get_started/install/install_guide_ubuntu.md).
The instructions below are intended for performing manual installation.

## Prerequisite 1 - Intel® GPU drivers for computing and media runtimes

To use GPU as an inference device or to use graphics hardware
encoding/decoding capabilities, install GPU computing
and media runtime drivers. Follow the up-to-date instructions
for your hardware:

- [Intel® Data Center GPU Flex Series and Intel® Data Center GPU Max Series](https://dgpu-docs.intel.com/driver/client/overview.html)
- [Intel® Client and Arc™ GPUs](https://dgpu-docs.intel.com/driver/installation.html)

## (Optional) Prerequisite 2 - Install Intel® NPU drivers

To use NPU (AI accelerator) of Intel® Core™ Ultra processors,
you need to install Intel® NPU driver:

1. First, make sure that the `intel_vpu.ko` module is enabled on your host:

   ```bash
   user@your-host:~$ lsmod | grep intel_vpu
   intel_vpu             245760  0
   ```

2. Installing the driver requires the device to be recognized by your
   system. The Kernel Mode driver should be available as
   an `accel` device in the `/dev/dri` directory. If it is not there,
   reboot the host.

   ```bash
   user@my-host:~$ ll /dev/accel/ | grep accel
   crw-rw----  1 root render 261, 0   Aug  6 22:41 accel0
   ```

3. Follow the
   ["Installation procedure"](https://github.com/intel/linux-npu-driver/releases) for
   the newest Intel® NPU driver.

> **NOTE:** If you are experiencing issues with the installation, check all
> notes and tips in the release notes for the newest [Intel® NPU driver
> version](https://github.com/intel/linux-npu-driver/releases). Please
> pay attention to **access to the device as a non-root user**.

Note that the following error can occur when running DL Streamer on
NPU device:

```bash
Setting pipeline to PLAYING ...
New clock: GstSystemClock
Caught SIGSEGV
Spinning.
```

For a temporary workaround, use the following setting:

```bash
export ZE_ENABLE_ALT_DRIVERS=libze_intel_npu.so
```

The issue should be fixed with newer versions of Intel® NPU drivers and
Intel® OpenVINO™ NPU plugins.

------------------------------------------------------------------------

> **\*** *Other names and brands may be claimed as the property of
> others.*
