# ==============================================================================
# Copyright (C) 2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================

param(
    [string]$InputSource = "https://github.com/intel-iot-devkit/sample-videos/raw/master/head-pose-face-detection-female-and-male.mp4",
    [string]$Device = "CPU",
    [string]$OutputType = "display",
    [string]$JsonFile = "output.json",
    [string]$FrameLimiter = ""
)

# Show help
if ($InputSource -eq "--help" -or $InputSource -eq "-h") {
    Write-Host "Usage: face_detection_and_classification.ps1 [-InputSource <path>] [-Device <device>] [-OutputType <type>] [-JsonFile <file>] [-FrameLimiter <element>]"
    Write-Host ""
    Write-Host "Parameters:"
    Write-Host "  -InputSource  Input source (default: GitHub sample video URL)"
    Write-Host "                Local file path (e.g., C:\videos\video.mp4)"
    Write-Host "                URL (e.g., https://...)"
    Write-Host "                USB camera (e.g., \\?\usb#...)"
    Write-Host "  -Device       Device for inference (default: CPU)"
    Write-Host "                Supported: CPU, GPU, NPU"
    Write-Host "  -OutputType   Output type (default: display)"
    Write-Host "                display - Show video with overlay"
    Write-Host "                fps - Benchmark mode (no display)"
    Write-Host "                json - Export metadata to JSON"
    Write-Host "  -JsonFile     JSON output filename (default: output.json)"
    Write-Host "  -FrameLimiter Optional GStreamer element to add after decode (e.g., ' ! identity eos-after=1000')"
    Write-Host ""
    exit 0
}

# Check MODELS_PATH
if (-not $env:MODELS_PATH) {
    Write-Host "ERROR: MODELS_PATH is not set." -ForegroundColor Red
    exit 1
}
Write-Host "MODELS_PATH: $env:MODELS_PATH"

# Validate device
$VALID_DEVICES = @("CPU", "GPU", "NPU")
if ($VALID_DEVICES -notcontains $Device) {
    Write-Host "ERROR: Unsupported device: $Device" -ForegroundColor Red
    Write-Host "Supported devices: $($VALID_DEVICES -join ', ')"
    exit 1
}

# Set source element based on input type
if ($InputSource -match "^\\\\\?\\usb#") {
    $SOURCE_ELEMENT = "ksvideosrc device-path=$InputSource"
} elseif ($InputSource -match "://") {
    $SOURCE_ELEMENT = "urisourcebin buffer-size=4096 uri=$InputSource"
} else {
    $INPUT_PATH = $InputSource -replace '\\', '/'
    $ESCAPED_INPUT_PATH = $INPUT_PATH -replace '"', '\"'
    $SOURCE_ELEMENT = "filesrc location=`"$ESCAPED_INPUT_PATH`""
}

# Set sink element based on output type
switch ($OutputType) {
    "json" {
        if (Test-Path $JsonFile) { Remove-Item $JsonFile }
        $SINK_ELEMENT = "gvametaconvert ! gvametapublish file-format=json-lines file-path=$JsonFile ! fakesink async=false"
    }
    "fps" {
        $SINK_ELEMENT = "gvafpscounter ! fakesink async=false"
    }
    "display" {
        $SINK_ELEMENT = "gvawatermark ! videoconvert ! autovideosink sync=false"
    }
    default {
        Write-Host "ERROR: Invalid OUTPUT parameter" -ForegroundColor Red
        Write-Host "Valid values: display, fps, json"
        exit 1
    }
}

# Set model names
$MODEL1 = "face-detection-adas-0001"
$MODEL2 = "age-gender-recognition-retail-0013"
$MODEL3 = "emotions-recognition-retail-0003"
$MODEL4 = "landmarks-regression-retail-0009"

# Set model paths
$DETECT_MODEL_PATH = "$env:MODELS_PATH\intel\$MODEL1\FP32\$MODEL1.xml"
$CLASS_MODEL_PATH = "$env:MODELS_PATH\intel\$MODEL2\FP32\$MODEL2.xml"
$CLASS_MODEL_PATH1 = "$env:MODELS_PATH\intel\$MODEL3\FP32\$MODEL3.xml"
$CLASS_MODEL_PATH2 = "$env:MODELS_PATH\intel\$MODEL4\FP32\$MODEL4.xml"

$MODEL2_PROC = "$PSScriptRoot\model_proc\$MODEL2.json"
$MODEL3_PROC = "$PSScriptRoot\model_proc\$MODEL3.json"
$MODEL4_PROC = "$PSScriptRoot\model_proc\$MODEL4.json"

# Check if detection model exists
if (-not (Test-Path $DETECT_MODEL_PATH)) {
    Write-Host "ERROR: Model not found: $DETECT_MODEL_PATH" -ForegroundColor Red
    Write-Host "Please run download_omz_models.bat to download the models first."
    exit 1
}

# Convert paths to forward slashes for GStreamer
$DETECT_MODEL_PATH = $DETECT_MODEL_PATH -replace '\\', '/'
$CLASS_MODEL_PATH = $CLASS_MODEL_PATH -replace '\\', '/'
$CLASS_MODEL_PATH1 = $CLASS_MODEL_PATH1 -replace '\\', '/'
$CLASS_MODEL_PATH2 = $CLASS_MODEL_PATH2 -replace '\\', '/'
$MODEL2_PROC = $MODEL2_PROC -replace '\\', '/'
$MODEL3_PROC = $MODEL3_PROC -replace '\\', '/'
$MODEL4_PROC = $MODEL4_PROC -replace '\\', '/'

# Build and run pipeline
Write-Host ""
Write-Host "Running pipeline:"
Write-Host "gst-launch-1.0 -v $SOURCE_ELEMENT ! decodebin3$FrameLimiter ! videoconvert ! gvadetect model=`"$DETECT_MODEL_PATH`" device=$Device ! queue ! gvaclassify model=`"$CLASS_MODEL_PATH`" model-proc=`"$MODEL2_PROC`" device=$Device ! queue ! gvaclassify model=`"$CLASS_MODEL_PATH1`" model-proc=`"$MODEL3_PROC`" device=$Device ! queue ! gvaclassify model=`"$CLASS_MODEL_PATH2`" model-proc=`"$MODEL4_PROC`" device=$Device ! queue ! $SINK_ELEMENT"
Write-Host ""

$CMD = "gst-launch-1.0 -v $SOURCE_ELEMENT ! decodebin3$FrameLimiter ! videoconvert ! gvadetect model=`"$DETECT_MODEL_PATH`" device=$Device ! queue ! gvaclassify model=`"$CLASS_MODEL_PATH`" model-proc=`"$MODEL2_PROC`" device=$Device ! queue ! gvaclassify model=`"$CLASS_MODEL_PATH1`" model-proc=`"$MODEL3_PROC`" device=$Device ! queue ! gvaclassify model=`"$CLASS_MODEL_PATH2`" model-proc=`"$MODEL4_PROC`" device=$Device ! queue ! $SINK_ELEMENT"
Invoke-Expression $CMD

exit $LASTEXITCODE
