# ==============================================================================
# Copyright (C) 2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================

param(
    [string]$InputSource = "https://github.com/intel-iot-devkit/sample-videos/raw/master/face-demographics-walking.mp4",
    [string]$Device = "CPU",
    [string]$OutputType = "display",
    [string]$JsonFile = "output.json",
    [string]$FrameLimiter = ""
)

# Show help
if ($InputSource -eq "--help" -or $InputSource -eq "-h") {
    Write-Host "Usage: human_pose_estimation.ps1 [-InputSource <path>] [-Device <device>] [-OutputType <type>] [-JsonFile <file>] [-FrameLimiter <element>]"
    Write-Host ""
    Write-Host "Parameters:"
    Write-Host "  -InputSource    Input source (default: GitHub sample video URL)"
    Write-Host "  -Device         Device (default: CPU). Supported: CPU, GPU, NPU"
    Write-Host "  -OutputType     Output type (default: display). Supported: file, display, fps, json, display-and-json"
    Write-Host "  -JsonFile       JSON output file name (default: output.json)"
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

# Set source element based on input type
if ($InputSource -match "://") {
    $SOURCE_ELEMENT = "urisourcebin buffer-size=4096 uri=$InputSource"
} else {
    $INPUT_PATH = $InputSource -replace '\\', '/'
    $SOURCE_ELEMENT = "filesrc location=`"$INPUT_PATH`""
}

# Set preprocessing backend based on device
if ($Device -eq "CPU") {
    $PREPROC_BACKEND = "opencv"
    $DECODE_ELEMENT = "decodebin3"
} else {
    $PREPROC_BACKEND = "d3d11"
    $DECODE_ELEMENT = "decodebin3"
}

# Set sink element based on output type
switch ($OutputType) {
    "file" {
        $FILENAME = [System.IO.Path]::GetFileNameWithoutExtension($InputSource)
        $OUTPUT_FILE = "human_pose_estimation_${FILENAME}_${Device}.mp4"
        if (Test-Path $OUTPUT_FILE) { Remove-Item $OUTPUT_FILE }

        if ($Device -eq "CPU") {
            $SINK_ELEMENT = "queue ! videoconvert ! gvawatermark ! gvafpscounter ! openh264enc ! h264parse ! mp4mux ! filesink location=$OUTPUT_FILE"
        } else {
            $SINK_ELEMENT = "queue ! d3d11convert ! gvawatermark ! gvafpscounter ! d3d11h264enc ! h264parse ! mp4mux ! filesink location=$OUTPUT_FILE"
        }
    }
    "display" {
        if ($Device -eq "CPU") {
            $SINK_ELEMENT = "queue ! gvawatermark ! videoconvert ! gvafpscounter ! autovideosink sync=false"
        } else {
            $SINK_ELEMENT = "queue ! d3d11convert ! gvawatermark ! videoconvert ! gvafpscounter ! d3d11videosink sync=false"
        }
    }
    "fps" {
        $SINK_ELEMENT = "queue ! gvafpscounter ! fakesink async=false"
    }
    "json" {
        if (Test-Path $JsonFile) { Remove-Item $JsonFile }
        $SINK_ELEMENT = "queue ! gvametaconvert add-tensor-data=true ! gvametapublish file-format=json-lines file-path=$JsonFile ! fakesink async=false"
    }
    "display-and-json" {
        if (Test-Path $JsonFile) { Remove-Item $JsonFile }
        if ($Device -eq "CPU") {
            $SINK_ELEMENT = "queue ! gvawatermark ! gvametaconvert add-tensor-data=true ! gvametapublish file-format=json-lines file-path=$JsonFile ! videoconvert ! gvafpscounter ! autovideosink sync=false"
        } else {
            $SINK_ELEMENT = "queue ! d3d11convert ! gvawatermark ! gvametaconvert add-tensor-data=true ! gvametapublish file-format=json-lines file-path=$JsonFile ! videoconvert ! gvafpscounter ! d3d11videosink sync=false"
        }
    }
    default {
        Write-Host "ERROR: Invalid OUTPUT parameter" -ForegroundColor Red
        Write-Host "Valid values: file, display, fps, json, display-and-json"
        exit 1
    }
}

# Set model paths
$MODEL = "human-pose-estimation-0001"
$MODEL_PATH = "$env:MODELS_PATH\intel\$MODEL\FP32\$MODEL.xml"
$MODEL_PROC = "$PSScriptRoot\model_proc\$MODEL.json"

# Check if model exists
if (-not (Test-Path $MODEL_PATH)) {
    Write-Host "ERROR: Model not found: $MODEL_PATH" -ForegroundColor Red
    Write-Host "Please run download_omz_models.bat to download the models first."
    exit 1
}

# Convert paths to forward slashes for GStreamer
$MODEL_PATH = $MODEL_PATH -replace '\\', '/'
$MODEL_PROC = $MODEL_PROC -replace '\\', '/'

# Build and run pipeline
Write-Host ""
Write-Host "Running pipeline:"
Write-Host "gst-launch-1.0 $SOURCE_ELEMENT ! $DECODE_ELEMENT$FrameLimiter ! gvaclassify model=$MODEL_PATH model-proc=$MODEL_PROC device=$Device inference-region=full-frame pre-process-backend=$PREPROC_BACKEND ! $SINK_ELEMENT"
Write-Host ""

# Build pipeline command - expand variables first, then execute
$CMD = "gst-launch-1.0 $SOURCE_ELEMENT ! $DECODE_ELEMENT$FrameLimiter ! gvaclassify model=$MODEL_PATH model-proc=$MODEL_PROC device=$Device inference-region=full-frame pre-process-backend=$PREPROC_BACKEND ! $SINK_ELEMENT"

# Execute using Invoke-Expression (properly handles the command string)
Invoke-Expression $CMD

exit $LASTEXITCODE
