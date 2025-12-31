# Advanced Installation on Windows - compilation from source files

The instructions below are intended for building Deep Learning Streamer Pipeline Framework
from the source code provided in

[Open Edge Platform repository](https://github.com/open-edge-platform/dlstreamer.git).

## Step 1: Clone Deep Learning Streamer repository

```bash
git clone --recursive https://github.com/open-edge-platform/dlstreamer.git
cd dlstreamer
```

## Step 2: Run installation script

Open PowerShell as administrator and run the `/build_dlstreamer_dlls.ps1` script.

```
cd ./dlstreamer/
./scripts/build_dlstreamer_dlls.ps1
```

### Details of the build script

- The script will install the following dependencies:
  | Required dependency | Path |
  | -------- | ------- |
  | Temporary downloaded files | C:\\dlstreamer_tmp |
  | WinGet PowerShell module from PSGallery | \%programfiles\%\\WindowsPowerShell\\Modules\\Microsoft.WinGet.Client |
  | Visual Studio BuildTools | C:\\BuildTools |
  | Microsoft Windows SDK | \%programfiles(x86)\%\\Windows Kits |
  | GStreamer | C:\\gstreamer |
  | OpenVINO GenAI | C:\\openvino |
  | Git | \%programfiles\%\\Git |
  | vcpkg | C:\\vcpkg |
  | Python | \%programfiles\%\\Python |
  | NuGet | C:\\libva |
  | Microsoft.Direct3D.VideoAccelerationCompatibilityPack (libva) | C:\\libva |
  | DL Streamer | C:\\dlstreamer_tmp\\build |

- The script will create or modify following environmental variables:
  - VCPKG_ROOT
  - PATH
  - PKG_CONFIG_PATH
  - LIBVA_DRIVER_NAME
  - LIBVA_DRIVERS_PATH

- The script assumes that the proxy is properly configured
