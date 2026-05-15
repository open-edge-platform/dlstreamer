# Advanced Installation on Windows - Compilation From Source

The instructions below are intended for building Deep Learning Streamer Pipeline Framework
from the source code provided in

[Open Edge Platform repository](https://github.com/open-edge-platform/dlstreamer).

## Step 1: Clone Deep Learning Streamer repository

```bash
git clone --recursive https://github.com/open-edge-platform/dlstreamer.git
cd dlstreamer
git submodule update --init --recursive
```

## Step 2: Run installation script

Open PowerShell as administrator and run the `build_dlstreamer_dlls.ps1` script.

```powershell
.\scripts\build_dlstreamer_dlls.ps1
```

### Script parameters

| Parameter | Description |
| --------- | ----------- |
| `-useInternalProxy` | Configures HTTP/HTTPS proxy for Intel internal network |
| `-buildInstaller` | Builds NSIS installer package |
| `-installerSkipCompression` | Skip NSIS installer compression, speeds up packaging |
| `-installerCodeSignScript <path>` | Path to code signing script to sign the binaries |
| `-setEnv` | Sets user-level environment variables after a successful build (PATH, GST_PLUGIN_PATH) for running DL Streamer from the build tree |

### Details of the build script

- The script will install the following dependencies:
  | Required dependency | Path |
  | -------- | ------- |
  | Temporary downloaded files | %TEMP%\\dlstreamer_tmp |
  | WinGet PowerShell module from PSGallery | %programfiles%\\WindowsPowerShell\\Modules\\Microsoft.WinGet.Client |
  | Visual Studio 2026 BuildTools | %programfiles%\\Microsoft Visual Studio |
  | vcpkg | Included with Visual Studio BuildTools |
  | Microsoft Windows SDK | %programfiles(x86)%\\Windows Kits |
  | GStreamer | %programfiles%\\gstreamer\\1.0\\msvc_x86_64 |
  | OpenVINO GenAI | %localappdata%\\Programs\\openvino |
  | NSIS | %programfiles(x86)%\\NSIS |
  | Git | Installed via WinGet |
  | CMake | Installed via WinGet |
  | Python | Installed via official installer |

  If Visual Studio IDE is already installed, the script will skip installing BuildTools but will check for required workloads and components, and install any missing ones.

- The script will set the following environment variables:
  - `PKG_CONFIG_PATH` — points to GStreamer pkg-config directory
- When `-setEnv` is specified, the following user-level environment variables are also set:
  - `PATH` — adds DL Streamer build output, OpenVINO runtime, and TBB directories
  - `GST_PLUGIN_PATH` — points to the DL Streamer build output directory
- Build output is placed in `dlstreamer\build`
- The NSIS installer package is generated in `dlstreamer\build`
- The script assumes that the proxy is properly configured

------------------------------------------------------------------------

> **\*** *Other names and brands may be claimed as the property of
> others.*
