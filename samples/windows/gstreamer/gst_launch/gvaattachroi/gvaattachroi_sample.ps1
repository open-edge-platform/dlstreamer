# ==============================================================================
# Copyright (C) 2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================

param(
    [string]$InputSource = "DEFAULT",
    [string]$Device = "CPU",
    [string]$OutputType = "display",
    [string]$RoiCoords = "",
    [string]$Model = "yolov8s",
    [string]$Precision = "FP32",
    [string]$FrameLimiter = ""
)

# Show help
if ($InputSource -eq "--help" -or $InputSource -eq "-h") {
    Write-Host "Usage: gvaattachroi_sample.ps1 [-InputSource <path>] [-Device <device>] [-OutputType <type>] [-RoiCoords <coords>] [-Model <model>] [-Precision <precision>] [-FrameLimiter <element>]"
    Write-Host ""
    Write-Host "Parameters:"
    Write-Host "  -InputSource    Input source (default: Pexels video URL)"
    Write-Host "                  Use 'DEFAULT' for default video"
    Write-Host "  -Device         Device (default: CPU). Supported: CPU, GPU, NPU"
    Write-Host "  -OutputType     Output type (default: display). Supported: display, json, fps, display-and-json"
    Write-Host "  -RoiCoords      ROI coordinates in format: x_top_left,y_top_left,x_bottom_right,y_bottom_right"
    Write-Host "                  Example: '100,150,200,300'"
    Write-Host "                  If not specified, uses roi_list.json file"
    Write-Host "  -Model          Model name (default: yolov8s)"
    Write-Host "  -Precision      Model precision (default: FP32). Supported: FP32, FP16, INT8"
    Write-Host "  -FrameLimiter   Optional GStreamer element to add after decode (e.g., ' ! identity eos-after=1000')"
    Write-Host ""
    exit 0
}

# Check MODELS_PATH
if (-not $env:MODELS_PATH) {
    Write-Host "ERROR: MODELS_PATH is not set." -ForegroundColor Red
    exit 1
}
Write-Host "MODELS_PATH: $env:MODELS_PATH"

# Handle default input source
if ($InputSource -eq "DEFAULT" -or $InputSource -eq ".") {
    $InputSource = "https://videos.pexels.com/video-files/1192116/1192116-sd_640_360_30fps.mp4"
}

# Set model path
$MODEL_PATH = "$env:MODELS_PATH\public\$Model\$Precision\$Model.xml"
$MODEL_PATH = $MODEL_PATH -replace '\\', '/'

# Check if model exists
if (-not (Test-Path ($MODEL_PATH -replace '/', '\'))) {
    Write-Host "ERROR: Model not found: $MODEL_PATH" -ForegroundColor Red
    exit 1
}

# Set source element based on input type
if ($InputSource -match "://") {
    $SOURCE_ELEMENT = "urisourcebin buffer-size=4096 uri=$InputSource"
} else {
    $SRC_FIXED = $InputSource -replace '\\', '/'
    $SOURCE_ELEMENT = "filesrc location=`"$SRC_FIXED`""
}

# Set preprocessing backend based on device
if ($Device -eq "CPU") {
    $PREPROC_BACKEND = "opencv"
} else {
    $PREPROC_BACKEND = "d3d11"
}

# Set sink element based on output type
switch ($OutputType) {
    "json" {
        if (Test-Path "output.json") { Remove-Item "output.json" }
        $SINK_STR = "gvametaconvert add-tensor-data=true ! gvametapublish file-format=json-lines file-path=output.json ! fakesink async=false"
    }
    "fps" {
        $SINK_STR = "gvafpscounter ! fakesink async=false"
    }
    "display" {
        if ($Device -eq "CPU") {
            $SINK_STR = "gvawatermark ! videoconvert ! gvafpscounter ! autovideosink sync=false"
        } else {
            $SINK_STR = "d3d11convert ! gvawatermark ! videoconvert ! gvafpscounter ! d3d11videosink sync=false"
        }
    }
    "display-and-json" {
        if (Test-Path "output.json") { Remove-Item "output.json" }
        if ($Device -eq "CPU") {
            $SINK_STR = "gvawatermark ! gvametaconvert add-tensor-data=true ! gvametapublish file-format=json-lines file-path=output.json ! videoconvert ! gvafpscounter ! autovideosink sync=false"
        } else {
            $SINK_STR = "d3d11convert ! gvawatermark ! gvametaconvert add-tensor-data=true ! gvametapublish file-format=json-lines file-path=output.json ! videoconvert ! gvafpscounter ! d3d11videosink sync=false"
        }
    }
    default {
        Write-Host "ERROR: Invalid -OutputType parameter" -ForegroundColor Red
        Write-Host "Valid values: display, json, fps, display-and-json"
        exit 1
    }
}

# Build ROI element
if ($RoiCoords -ne "") {
    # Use coordinates directly
    $ROI_ELEMENT = "gvaattachroi roi=$RoiCoords"
} else {
    # Use ROI list file
    $SCRIPT_DIR = Split-Path -Parent $MyInvocation.MyCommand.Path
    $ROI_FILE_PATH = Join-Path $SCRIPT_DIR "roi_list.json"
    $ROI_FILE_PATH = $ROI_FILE_PATH -replace '\\', '/'

    if (-not (Test-Path ($ROI_FILE_PATH -replace '/', '\'))) {
        Write-Host "ERROR: ROI list file not found: $ROI_FILE_PATH" -ForegroundColor Red
        Write-Host "Please create roi_list.json or specify ROI coordinates with -RoiCoords parameter"
        exit 1
    }

    $ROI_ELEMENT = "gvaattachroi mode=1 file-path=$ROI_FILE_PATH"
}

# Build and run pipeline
Write-Host ""
Write-Host "=============================================================================="
Write-Host "Running Pipeline:"
$CMD = "gst-launch-1.0 -e $SOURCE_ELEMENT ! decodebin3$FrameLimiter ! $ROI_ELEMENT ! gvadetect inference-region=1 model=$MODEL_PATH device=$Device pre-process-backend=$PREPROC_BACKEND ! queue ! $SINK_STR"
Write-Host $CMD
Write-Host "=============================================================================="
Write-Host ""

Invoke-Expression $CMD

if ($LASTEXITCODE -ne 0) {
    Write-Host "[ERROR] Pipeline failed with exit code $LASTEXITCODE" -ForegroundColor Red
    exit $LASTEXITCODE
}

exit 0
