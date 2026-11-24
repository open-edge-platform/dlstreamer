# ==============================================================================
# Copyright (C) 2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================

param(
    [string]$Device = "CPU",
    [string]$InputSource = "DEFAULT",
    [string]$Model = "",
    [string]$Precision = "FP16",
    [string]$PreprocessBackend = "",
    [string]$OutputType = "display",
    [string]$MotionDetectOptions = "",
    [string]$FrameLimiter = ""
)

# Show help
if ($Device -eq "--help" -or $Device -eq "-h") {
    Write-Host "Usage: motion_detect.ps1 [-Device <device>] [-InputSource <path>] [-Model <model>] [-Precision <precision>] [-PreprocessBackend <backend>] [-OutputType <type>] [-MotionDetectOptions <options>] [-FrameLimiter <element>]"
    Write-Host ""
    Write-Host "Parameters:"
    Write-Host "  -Device                Device (default: CPU). Supported: CPU, GPU, NPU"
    Write-Host "  -InputSource           Input source (default: Pexels video URL)"
    Write-Host "                         Use 'DEFAULT' for default video"
    Write-Host "  -Model                 Model path (default: yolov8n from MODELS_PATH)"
    Write-Host "  -Precision             Model precision (default: FP32). Supported: FP32, FP16, INT8"
    Write-Host "  -PreprocessBackend     Preprocessing backend (default: opencv for CPU, d3d12-surface-sharing for GPU, d3d12 for NPU)"
    Write-Host "                         Supported: ie, opencv, d3d11, d3d11-surface-sharing, d3d12, d3d12-surface-sharing"
    Write-Host "  -OutputType            Output type (default: display). Supported: display, json"
    Write-Host "  -MotionDetectOptions   Optional gvamotiondetect properties (e.g., 'threshold=0.5')"
    Write-Host "  -FrameLimiter          Optional GStreamer element to add after decode (e.g., ' ! identity eos-after=1000')"
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

# Validate device
$VALID_DEVICES = @("CPU", "GPU", "NPU")
if ($VALID_DEVICES -notcontains $Device) {
    Write-Host "ERROR: Unsupported device: $Device" -ForegroundColor Red
    Write-Host "Supported devices: $($VALID_DEVICES -join ', ')"
    exit 1
}

# Set preprocessing backend based on device
if ($PreprocessBackend -eq "") {
    switch ($Device) {
        "CPU" { $PREPROC_BACKEND = "opencv" }
        "GPU" { $PREPROC_BACKEND = "d3d12-surface-sharing" }
        "NPU" { $PREPROC_BACKEND = "d3d12" }
    }
} else {
    $VALID_BACKENDS = @("ie", "opencv", "d3d11", "d3d11-surface-sharing", "d3d12", "d3d12-surface-sharing")
    if ($VALID_BACKENDS -notcontains $PreprocessBackend) {
        Write-Host "ERROR: Invalid PreprocessBackend parameter. Supported values: $($VALID_BACKENDS -join ', ')" -ForegroundColor Red
        exit 1
    }
    if ($Device -eq "CPU" -and $PreprocessBackend -like "d3d*") {
        Write-Host "ERROR: $PreprocessBackend backend requires GPU or NPU device" -ForegroundColor Red
        exit 1
    }
    $PREPROC_BACKEND = $PreprocessBackend
}

# gvamotiondetect and gvadetect receive the memory type requested after decode, which must
# match the backend: system memory for ie/opencv, D3D12Memory for d3d12 backends and
# D3D11Memory for d3d11 backends.
if ($PREPROC_BACKEND -like "d3d12*") {
    $MEMORY = "D3D12Memory"
    $DISPLAY_STR = "d3d12convert ! gvawatermark ! gvafpscounter ! d3d12videosink"
} elseif ($PREPROC_BACKEND -like "d3d11*") {
    $MEMORY = "D3D11Memory"
    $DISPLAY_STR = "d3d11convert ! gvawatermark ! videoconvert ! gvafpscounter ! d3d11videosink"
} else {
    $MEMORY = "SystemMemory"
    $DISPLAY_STR = "gvawatermark ! videoconvert ! gvafpscounter ! autovideosink"
}

# Set model path
$MODEL_NAME = "yolov8n"
if ($Model -eq "" -or $Model -eq ".") {
    $MODEL_FINAL = "$env:MODELS_PATH\public\$MODEL_NAME\$Precision\$MODEL_NAME.xml"
} else {
    $MODEL_FINAL = $Model
}

# Convert model path to forward slashes
$MODEL_FINAL = $MODEL_FINAL -replace '\\', '/'

# Check if model exists
if (-not (Test-Path ($MODEL_FINAL -replace '/', '\'))) {
    Write-Host "ERROR: Model not found: $MODEL_FINAL" -ForegroundColor Red
    Write-Host "Please prepare this model via scripts/download_models/download_ultralytics_models.py (see scripts/download_models/README.md)."
    exit 1
}

# Set source element based on input type
if ($InputSource -match "://") {
    $SOURCE_ELEMENT = "urisourcebin uri=$InputSource"
} else {
    $SRC_FIXED = $InputSource -replace '\\', '/'
    $SOURCE_ELEMENT = "filesrc location=`"$SRC_FIXED`""
}

# Set sink element based on output type
switch ($OutputType) {
    "json" {
        if (Test-Path "output.json") { Remove-Item "output.json" }
        $SINK_STR = "gvametaconvert format=json ! gvametapublish method=file file-format=json-lines file-path=output.json ! gvafpscounter ! fakesink"
    }
    "display" {
        $SINK_STR = $DISPLAY_STR
    }
    default {
        Write-Host "ERROR: Invalid OUTPUT parameter" -ForegroundColor Red
        Write-Host "Valid values: display, json"
        exit 1
    }
}

# Build gvadetect element
$GVADET = "gvadetect model=$MODEL_FINAL device=$Device pre-process-backend=$PREPROC_BACKEND inference-region=1"

# Build caps part
$CAPS_PART = "! `"video/x-raw(memory:$MEMORY)`" "

# Build motion detect options part
$MD_OPTS_PART = ""
if ($MotionDetectOptions -ne "") {
    $MD_OPTS_PART = " $MotionDetectOptions"
}

# Build and run pipeline
Write-Host ""
Write-Host "=============================================================================="
Write-Host "Running Pipeline:"
$CMD = "gst-launch-1.0 -e $SOURCE_ELEMENT ! decodebin3$FrameLimiter $CAPS_PART ! gvamotiondetect$MD_OPTS_PART ! $GVADET ! $SINK_STR"
Write-Host $CMD
Write-Host "=============================================================================="
Write-Host ""

Invoke-Expression $CMD

if ($LASTEXITCODE -ne 0) {
    Write-Host "[ERROR] Pipeline failed with exit code $LASTEXITCODE" -ForegroundColor Red
    exit $LASTEXITCODE
}

exit 0
