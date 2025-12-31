# Advanced Installation on Windows - compilation from source files

The instructions below are intended for building Deep Learning Streamer Pipeline Framework
from the source code provided in

[Open Edge Platform repository](https://github.com/open-edge-platform/dlstreamer.git).

## Step 1: Clone Deep Learning Streamer repository

```bash
git clone https://github.com/open-edge-platform/edge-ai-libraries.git
cd edge-ai-libraries
git submodule update --init libraries/dl-streamer/thirdparty/spdlog
```

## Step 2: Run installation script

### Build script details:

- The script will install automatically following dependencies if they don't exist:
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

- The script assues that proxy is properly configured


### Run PowerShell with administrative privileges and execute:

```
cd ./libraries/dl-streamer/
./scripts/build_dlstreamer_dlls.ps1
```




