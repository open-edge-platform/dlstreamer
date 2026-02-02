# escape=`

# ==============================================================================
# Copyright (C) 2025 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================

# Use the Windows Server Core 2022 image.
FROM mcr.microsoft.com/windows/servercore:ltsc2022

ARG GSTREAMER_VERSION=1.26.6

# Restore the default Windows shell for correct batch processing.
SHELL ["cmd", "/S", "/C"]

# Install Build Tools
RUN `
    # Download the Build Tools bootstrapper.
    curl -SL --output vs_buildtools.exe https://aka.ms/vs/17/release/vs_buildtools.exe `
   `
   # Install Build Tools with the Microsoft.VisualStudio.Workload.AzureBuildTools workload, excluding workloads and components with known issues.
   && (start /w vs_buildtools.exe --quiet --wait --norestart --nocache `
       --installPath "%ProgramFiles(x86)%\Microsoft Visual Studio\2022\BuildTools" `
       --add Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
       --add Microsoft.VisualStudio.ComponentGroup.NativeDesktop.Core `
       || IF "%ERRORLEVEL%"=="3010" EXIT 0) `
   `
   # Cleanup
   && del /q vs_buildtools.exe

# Install Windows SDK
RUN `
    curl -SL --output winsdksetup.exe https://go.microsoft.com/fwlink/?linkid=2320455 `
    && winsdksetup.exe /features + /q /norestart `
    && del /q winsdksetup.exe

# Download GStreamer
RUN `
    curl -SL --output gstreamer-1.0-msvc-x86_64.msi https://gstreamer.freedesktop.org/data/pkg/windows/%GSTREAMER_VERSION%/msvc/gstreamer-1.0-msvc-x86_64-%GSTREAMER_VERSION%.msi `
    && msiexec /passive INSTALLDIR=C:\gstreamer /i gstreamer-1.0-msvc-x86_64.msi `
    && del /q gstreamer-1.0-msvc-x86_64.msi `
    && curl -SL --output gstreamer-1.0-devel-msvc-x86_64.msi https://gstreamer.freedesktop.org/data/pkg/windows/%GSTREAMER_VERSION%/msvc/gstreamer-1.0-devel-msvc-x86_64-%GSTREAMER_VERSION%.msi `
    && msiexec /passive INSTALLDIR=C:\gstreamer /i gstreamer-1.0-devel-msvc-x86_64.msi `
    && del /q gstreamer-1.0-devel-msvc-x86_64.msi

# Download OpenVINO
RUN `
    curl -SL --output openvino.zip https://storage.openvinotoolkit.org/repositories/openvino/packages/2025.4/windows/openvino_toolkit_windows_2025.4.0.20398.8fdad55727d_x86_64.zip
    && powershell -command "Expand-Archive -Path \"openvino.zip\" -DestinationPath \"C:\" " `
    && del /q openvino.zip `
    && move openvino_toolkit_windows_2025.4.0.20398.8fdad55727d_x86_64 openvino

# Install git and vcpkg
ENV VCPKG_ROOT="C:\vcpkg"

RUN `
    curl -SL --output git_setup.exe https://github.com/git-for-windows/git/releases/download/v2.49.0.windows.1/Git-2.49.0-64-bit.exe `
    && powershell -command "Start-Process -FilePath git_setup.exe -ArgumentList \"/SILENT\", \"/NORESTART\", \"/DIR=C:\git\" -Wait -NoNewWindow" `
    && del /q git_setup.exe `
    && C:\git\bin\git.exe clone https://github.com/microsoft/vcpkg.git `
    && cd %VCPKG_ROOT% `
    && "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat" `
    && bootstrap-vcpkg.bat `
    && %VCPKG_ROOT%\vcpkg.exe integrate install

# Setup environment
RUN `
    setx path "%path%;%VCPKG_ROOT%;C:\vcpkg\downloads\tools\cmake-3.30.1-windows\cmake-3.30.1-windows-i386\bin;C:\vcpkg\downloads\tools\python\python-3.12.7-x64-1" `
    && setx PKG_CONFIG_PATH "%PKG_CONFIG_PATH%;C:\gstreamer\1.0\msvc_x86_64\lib\pkgconfig" `
    && powershell -command "(Get-Content C:\gstreamer\1.0\msvc_x86_64\lib\pkgconfig\gstreamer-analytics-1.0.pc).Replace('-lm', '') | Set-Content C:\gstreamer\1.0\msvc_x86_64\lib\pkgconfig\gstreamer-analytics-1.0.pc"

# Copy dlstreamer
COPY dlstreamer C:\dlstreamer

# Install VCPKG
RUN `
    mkdir C:\dlstreamer\build `
    && cd C:\dlstreamer\build `
    && "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat" `
    && echo.set(VCPKG_BUILD_TYPE release)>> %VCPKG_ROOT%\triplets\x64-windows.cmake `
    && vcpkg install --triplet x64-windows

# Install libva
RUN `
    curl -SL --output nuget.exe https://dist.nuget.org/win-x86-commandline/latest/nuget.exe `
    && nuget install Microsoft.Direct3D.VideoAccelerationCompatibilityPack `
    && setx PKG_CONFIG_PATH "%PKG_CONFIG_PATH%;C:\Microsoft.Direct3D.VideoAccelerationCompatibilityPack.1.0.2\build\native\x64\lib\pkgconfig" `
    && setx LIBVA_DRIVER_NAME "vaon12" `
    && setx LIBVA_DRIVERS_PATH "C:\Microsoft.Direct3D.VideoAccelerationCompatibilityPack.1.0.2\build\native\x64\bin"

# Build dlstreamer
RUN `
    cd C:\dlstreamer\build `
    && C:\openvino\setupvars.bat `
    && cmake -DVCPKG_BUILD_TYPE=release -DCMAKE_TOOLCHAIN_FILE=%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake .. `
    && cmake --build . --target ALL_BUILD --config Release
