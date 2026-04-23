# ==============================================================================
# Copyright (C) 2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================

param(
    [string]$InputSource = "https://github.com/open-edge-platform/edge-ai-resources/raw/main/videos/ParkingVideo.mp4",
    [string]$Device = "GPU",
    [string]$OutputType = "fps",
    [string]$JsonFile = "output.json",
    [string]$FrameLimiter = ""
)

# Show help
if ($InputSource -eq "--help" -or $InputSource -eq "-h") {
    Write-Host "Usage: license_plate_recognition.ps1 [-InputSource <path>] [-Device <device>] [-OutputType <type>] [-JsonFile <file>] [-FrameLimiter <element>]"
    Write-Host ""
    Write-Host "Parameters:"
    Write-Host "  -InputSource    Input source (default: GitHub parking video URL)"
    Write-Host "  -Device         Device (default: GPU). Supported: CPU, GPU, NPU"
    Write-Host "  -OutputType     Output type (default: fps). Supported: display, display-async, fps, json, display-and-json, file"
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

# Validate device
$VALID_DEVICES = @("CPU", "GPU", "NPU")
if ($VALID_DEVICES -notcontains $Device) {
    Write-Host "ERROR: Unsupported device: $Device" -ForegroundColor Red
    Write-Host "Supported devices: $($VALID_DEVICES -join ', ')"
    exit 1
}

# Set model paths
$DETECTION_MODEL = "$env:MODELS_PATH\public\yolov8_license_plate_detector\FP32\yolov8_license_plate_detector.xml"
$OCR_CLASSIFICATION_MODEL = "$env:MODELS_PATH\public\ch_PP-OCRv4_rec_infer\FP32\ch_PP-OCRv4_rec_infer.xml"

# Check if models exist
if (-not (Test-Path $DETECTION_MODEL)) {
    Write-Host "ERROR: Model not found: $DETECTION_MODEL" -ForegroundColor Red
    Write-Host "Please run download_public_models.bat to download the models first."
    exit 1
}

if (-not (Test-Path $OCR_CLASSIFICATION_MODEL)) {
    Write-Host "ERROR: Model not found: $OCR_CLASSIFICATION_MODEL" -ForegroundColor Red
    Write-Host "Please run download_public_models.bat to download the models first."
    exit 1
}

# Set source element based on input type
if ($InputSource -match "://") {
    $SOURCE_ELEMENT = "urisourcebin buffer-size=4096 uri=$InputSource"
} else {
    $INPUT_PATH = $InputSource -replace '\\', '/'
    $SOURCE_ELEMENT = "filesrc location=`"$INPUT_PATH`""
}

# Set decode and preprocessing based on device
if ($Device -eq "CPU") {
    $DECODE_ELEMENT = "decodebin3"
    $PREPROC = "pre-process-backend=opencv"
} else {
    $DECODE_ELEMENT = "decodebin3"
    $PREPROC = "pre-process-backend=d3d11"
}

# Set sink element based on output type
switch ($OutputType) {
    "file" {
        $FILENAME = [System.IO.Path]::GetFileNameWithoutExtension($InputSource)
        $OUTPUT_FILE = "lpr_${FILENAME}_${Device}.mp4"
        if (Test-Path $OUTPUT_FILE) { Remove-Item $OUTPUT_FILE }

        if ($Device -eq "CPU") {
            $SINK_ELEMENT = "videoconvert ! gvawatermark ! gvafpscounter ! openh264enc ! h264parse ! mp4mux ! filesink location=$OUTPUT_FILE"
        } else {
            $SINK_ELEMENT = "d3d11convert ! gvawatermark ! gvafpscounter ! d3d11h264enc ! h264parse ! mp4mux ! filesink location=$OUTPUT_FILE"
        }
    }
    "display" {
        if ($Device -eq "CPU") {
            $SINK_ELEMENT = "gvawatermark ! videoconvert ! gvafpscounter ! autovideosink"
        } else {
            $SINK_ELEMENT = "d3d11convert ! gvawatermark ! videoconvert ! gvafpscounter ! d3d11videosink"
        }
    }
    "display-async" {
        if ($Device -eq "CPU") {
            $SINK_ELEMENT = "gvawatermark ! videoconvert ! gvafpscounter ! autovideosink sync=false"
        } else {
            $SINK_ELEMENT = "d3d11convert ! gvawatermark ! videoconvert ! gvafpscounter ! d3d11videosink sync=false"
        }
    }
    "fps" {
        $SINK_ELEMENT = "gvafpscounter ! fakesink async=false"
    }
    "json" {
        if (Test-Path $JsonFile) { Remove-Item $JsonFile }
        $SINK_ELEMENT = "gvametaconvert ! gvametapublish file-format=json-lines file-path=$JsonFile ! fakesink async=false"
    }
    "display-and-json" {
        if (Test-Path $JsonFile) { Remove-Item $JsonFile }
        if ($Device -eq "CPU") {
            $SINK_ELEMENT = "gvawatermark ! gvametaconvert ! gvametapublish file-format=json-lines file-path=$JsonFile ! videoconvert ! gvafpscounter ! autovideosink sync=false"
        } else {
            $SINK_ELEMENT = "d3d11convert ! gvawatermark ! gvametaconvert ! gvametapublish file-format=json-lines file-path=$JsonFile ! videoconvert ! gvafpscounter ! d3d11videosink sync=false"
        }
    }
    default {
        Write-Host "ERROR: Invalid OUTPUT parameter" -ForegroundColor Red
        Write-Host "Valid values: display, display-async, fps, json, display-and-json, file"
        exit 1
    }
}

# Convert paths to forward slashes for GStreamer
$DETECTION_MODEL = $DETECTION_MODEL -replace '\\', '/'
$OCR_CLASSIFICATION_MODEL = $OCR_CLASSIFICATION_MODEL -replace '\\', '/'

# Build and run pipeline
Write-Host ""
Write-Host "Running pipeline:"
Write-Host "gst-launch-1.0 $SOURCE_ELEMENT ! $DECODE_ELEMENT$FrameLimiter ! queue ! gvadetect model=$DETECTION_MODEL device=$Device $PREPROC ! queue ! videoconvert ! gvaclassify model=$OCR_CLASSIFICATION_MODEL device=$Device $PREPROC ! queue ! $SINK_ELEMENT"
Write-Host ""

$CMD = "gst-launch-1.0 $SOURCE_ELEMENT ! $DECODE_ELEMENT$FrameLimiter ! queue ! gvadetect model=$DETECTION_MODEL device=$Device $PREPROC ! queue ! videoconvert ! gvaclassify model=$OCR_CLASSIFICATION_MODEL device=$Device $PREPROC ! queue ! $SINK_ELEMENT"
Invoke-Expression $CMD

exit $LASTEXITCODE
