# ==============================================================================
# Copyright (C) 2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================

param(
    [string]$Device = "CPU",
    [string]$InputSource = "DEFAULT",
    [string]$Model = "",
    [string]$Precision = "FP32",
    [string]$PreprocessBackend = "opencv",
    [string]$OutputType = "display",
    [string]$MotionDetectOptions = "",
    [string]$FrameLimiter = ""
)

# Show help
if ($Device -eq "--help" -or $Device -eq "-h") {
    Write-Host "Usage: motion_detect.ps1 [-Device <device>] [-InputSource <path>] [-Model <model>] [-Precision <precision>] [-PreprocessBackend <backend>] [-OutputType <type>] [-MotionDetectOptions <options>] [-FrameLimiter <element>]"
    Write-Host ""
    Write-Host "Parameters:"
    Write-Host "  -Device                Device (default: CPU). Note: This demo is CPU-only"
    Write-Host "  -InputSource           Input source (default: Pexels video URL)"
    Write-Host "                         Use 'DEFAULT' for default video"
    Write-Host "  -Model                 Model path (default: yolov8n from MODELS_PATH)"
    Write-Host "  -Precision             Model precision (default: FP32). Supported: FP32, FP16, INT8"
    Write-Host "  -PreprocessBackend     Preprocessing backend (default: opencv)"
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

# Validate device (CPU only for this demo)
if ($Device -ne "CPU") {
    Write-Host "============================================================" -ForegroundColor Red
    Write-Host "[ERROR] Invalid Device: `"$Device`"" -ForegroundColor Red
    Write-Host "This specific demo script is configured for `"CPU`" ONLY." -ForegroundColor Red
    Write-Host "============================================================" -ForegroundColor Red
    exit 1
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
    Write-Host "Please run download_public_models.bat to download the models first."
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
        $SINK_STR = "gvawatermark ! videoconvert ! gvafpscounter ! autovideosink"
    }
    default {
        Write-Host "ERROR: Invalid OUTPUT parameter" -ForegroundColor Red
        Write-Host "Valid values: display, json"
        exit 1
    }
}

# Build gvadetect element
$GVADET = "gvadetect model=$MODEL_FINAL device=CPU pre-process-backend=$PreprocessBackend inference-region=1"

# Build caps part
$CAPS_PART = "! `"video/x-raw(memory:SystemMemory)`" "

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
