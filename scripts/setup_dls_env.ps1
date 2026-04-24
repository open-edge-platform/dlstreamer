# ==============================================================================
# Copyright (C) 2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================

Write-Host "`n=== DL Streamer Environment Setup ==="
Write-Host 'Setting environment variables: DLSTREAMER_DIR, GST_PLUGIN_PATH, PATH'

# Resolve DLL directory
$SCRIPT_DIR = Split-Path -Parent $MyInvocation.MyCommand.Definition
$BIN_DIR = Join-Path $SCRIPT_DIR "..\bin"
if (Test-Path $BIN_DIR) {
	$DLL_DIR = (Resolve-Path $BIN_DIR).Path
}
else {
	Write-Host "Error: Could not find DL Streamer DLLs. Please ensure DL Streamer is installed correctly."
	exit 1
}

$DLSTREAMER_ROOT = (Resolve-Path (Join-Path $DLL_DIR "..")).Path
$env:DLSTREAMER_DIR = $DLSTREAMER_ROOT
Write-Host "Set DLSTREAMER_DIR: $DLSTREAMER_ROOT"

$env:GST_PLUGIN_PATH = $DLL_DIR
Write-Host "Set GST_PLUGIN_PATH: $DLL_DIR"

$envMsvcX64 = [Environment]::GetEnvironmentVariable('GSTREAMER_1_0_ROOT_MSVC_X86_64', 'Machine').TrimEnd('\')
If (-Not $envMsvcX64) {
	Write-Host "Error: GSTREAMER_1_0_ROOT_MSVC_X86_64 environment variable is not set. Please ensure GStreamer is installed correctly."
	exit 1
}
$GSTREAMER_BIN_DIR = "$envMsvcX64\bin"

$pathEntries = $env:PATH -split ';'
if (-Not ($pathEntries -contains $DLL_DIR)) {
	$env:PATH = $env:PATH + ';' + $DLL_DIR
	Write-Host "Added to Path: $DLL_DIR"
}
if (-Not ($pathEntries -contains $GSTREAMER_BIN_DIR)) {
	$env:PATH = $env:PATH + ';' + $GSTREAMER_BIN_DIR
	Write-Host "Added to Path: $GSTREAMER_BIN_DIR"
}

# Check if gvadetect element is available
if (Test-Path "$env:LOCALAPPDATA\Microsoft\Windows\INetCache\gstreamer-1.0\registry.x86_64-msvc.bin") {
	Write-Host "Clearing existing GStreamer cache"
	Remove-Item "$env:LOCALAPPDATA\Microsoft\Windows\INetCache\gstreamer-1.0\registry.x86_64-msvc.bin"
}
Write-Host "Generating GStreamer cache. It may take up to a few minutes. Please wait for a moment..."
$output = & gst-inspect-1.0.exe gvadetect 2>&1
if ($LASTEXITCODE -ne 0 -or $output -match "No such element or plugin") {
	Write-Host "Error: Failed to find gvadetect element."
	Write-Host $output
	Write-Host "Please try updating GPU/NPU drivers and rebooting the system."
	Write-Host "Optionally run the command to debug plugin loading:"
	Write-Host "  `$env:GST_DEBUG=`"GST_PLUGIN_LOADING:5,GST_REGISTRY:5`"; `$env:GST_DEBUG_FILE=`"gst-plugin-loading-%p.log`"; gst-inspect-1.0 gvadetect"
}
else {
	Write-Host "DLStreamer is properly configured for this session."
}
