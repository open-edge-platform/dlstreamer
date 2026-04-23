# ==============================================================================
# Copyright (C) 2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================

param(
    [string]$InputSource = "$PSScriptRoot\how_are_you_doing.wav",
    [string]$OutputFile = ""
)

# Show help
if ($InputSource -eq "--help" -or $InputSource -eq "-h") {
    Write-Host "Usage: audio_event_detection.ps1 [-InputSource <path>] [-OutputFile <file>]"
    Write-Host ""
    Write-Host "Parameters:"
    Write-Host "  -InputSource  Input audio source (default: how_are_you_doing.wav in script directory)"
    Write-Host "                Local file path (e.g., C:\audio\sample.wav)"
    Write-Host "                URL (e.g., https://...)"
    Write-Host "  -OutputFile   JSON output file path (default: empty, prints to console)"
    Write-Host ""
    exit 0
}

# Check AUDIO_MODELS_PATH or MODELS_PATH
if (-not $env:AUDIO_MODELS_PATH) {
    if (-not $env:MODELS_PATH) {
        Write-Host "ERROR: Environment variables AUDIO_MODELS_PATH or MODELS_PATH not specified. Models not found, execute download_audio_models.bat to download models" -ForegroundColor Red
        exit 1
    } else {
        $env:AUDIO_MODELS_PATH = $env:MODELS_PATH
    }
}

$MODEL_NAME = "aclnet"

# Set source element based on input type
if ($InputSource -match "://") {
    $SOURCE_ELEMENT = "urisourcebin uri=$InputSource"
} else {
    $INPUT_PATH = $InputSource -replace '\\', '/'
    $SOURCE_ELEMENT = "filesrc location=`"$INPUT_PATH`""
}

# Set model paths
$MODEL_PATH = "$env:AUDIO_MODELS_PATH\public\aclnet\FP32\$MODEL_NAME.xml"
$MODEL_PROC_PATH = "$PSScriptRoot\model_proc\$MODEL_NAME.json"

# Check if model-proc file exists
if (-not (Test-Path $MODEL_PROC_PATH)) {
    Write-Host "ERROR: Invalid model-proc file path $MODEL_PROC_PATH" -ForegroundColor Red
    exit 1
}

# Convert paths to forward slashes for GStreamer
$MODEL_PATH = $MODEL_PATH -replace '\\', '/'
$MODEL_PROC_PATH = $MODEL_PROC_PATH -replace '\\', '/'

# Set publish element based on output
if ($OutputFile -eq "") {
    $PUBLISH_ELEMENT = "gvametapublish file-format=json-lines"
} else {
    $OUTPUT_PATH = $OutputFile -replace '\\', '/'
    $PUBLISH_ELEMENT = "gvametapublish file-format=json-lines file-path=$OUTPUT_PATH"
}

# Build and run pipeline
Write-Host ""
Write-Host "Running pipeline:"
$CMD = "gst-launch-1.0 $SOURCE_ELEMENT ! decodebin3 ! audioresample ! audioconvert ! `"audio/x-raw, channels=1, format=S16LE, rate=16000`" ! audiomixer output-buffer-duration=100000000 ! gvaaudiodetect model=`"$MODEL_PATH`" model-proc=`"$MODEL_PROC_PATH`" sliding-window=0.2 ! gvametaconvert ! $PUBLISH_ELEMENT ! fakesink"
Write-Host $CMD
Write-Host ""

Invoke-Expression $CMD

exit $LASTEXITCODE
