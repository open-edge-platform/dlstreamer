# ==============================================================================
# Copyright (C) 2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================

param(
    [string]$InputSource = "https://github.com/intel-iot-devkit/sample-videos/raw/master/person-bicycle-car-detection.mp4",
    [int]$DetectionInterval = 3,
    [string]$Device = "AUTO",
    [string]$OutputType = "display",
    [string]$TrackingType = "short-term-imageless",
    [string]$JsonFile = "output.json",
    [string]$FrameLimiter = ""
)

# Show help
if ($InputSource -eq "--help" -or $InputSource -eq "-h") {
    Write-Host "Usage: vehicle_pedestrian_tracking.ps1 [-InputSource <path>] [-DetectionInterval <interval>] [-Device <device>] [-OutputType <type>] [-TrackingType <type>] [-JsonFile <file>] [-FrameLimiter <element>]"
    Write-Host ""
    Write-Host "Parameters:"
    Write-Host "  -InputSource        Input source (default: GitHub sample video URL)"
    Write-Host "  -DetectionInterval  Object detection interval (default: 3). 1 means detection every frame, 2 means every second frame, etc."
    Write-Host "  -Device             Device (default: AUTO). Supported: AUTO, CPU, GPU, NPU"
    Write-Host "  -OutputType         Output type (default: display). Supported: display, display-async, fps, json, display-and-json, file"
    Write-Host "  -TrackingType       Object tracking type (default: short-term-imageless). Supported: short-term-imageless, zero-term, zero-term-imageless"
    Write-Host "  -JsonFile           JSON output file name (default: output.json)"
    Write-Host "  -FrameLimiter       Optional GStreamer element to add after decode (e.g., ' ! identity eos-after=1000')"
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
$VALID_DEVICES = @("AUTO", "CPU", "GPU", "GPU.0", "NPU")
if ($VALID_DEVICES -notcontains $Device) {
    Write-Host "ERROR: Unsupported device: $Device" -ForegroundColor Red
    Write-Host "Supported devices: $($VALID_DEVICES -join ', ')"
    exit 1
}

# Set source element based on input type
if ($InputSource -match "://") {
    $SOURCE_ELEMENT = "urisourcebin buffer-size=4096 uri=$InputSource"
} else {
    $INPUT_PATH = $InputSource -replace '\\', '/'
    $SOURCE_ELEMENT = "filesrc location=`"$INPUT_PATH`""
}

# Set decode element and preprocessing based on device
if ($Device -eq "CPU") {
    $DECODE_ELEMENT = "decodebin3"
    $PREPROC_BACKEND = "opencv"
} else {
    $DECODE_ELEMENT = "decodebin3"
    $PREPROC_BACKEND = "d3d11"
}

# Set sink element based on output type
switch ($OutputType) {
    "file" {
        $FILENAME = [System.IO.Path]::GetFileNameWithoutExtension($InputSource)
        $OUTPUT_FILE = "vehicle_pedestrian_tracking_${FILENAME}_${Device}.mp4"
        if (Test-Path $OUTPUT_FILE) { Remove-Item $OUTPUT_FILE }

        if ($Device -eq "CPU") {
            $SINK_ELEMENT = "videoconvert ! gvawatermark ! gvafpscounter ! openh264enc ! h264parse ! mp4mux ! filesink location=$OUTPUT_FILE"
        } else {
            $SINK_ELEMENT = "d3d11convert ! gvawatermark ! gvafpscounter ! mfh264enc ! h264parse ! mp4mux ! filesink location=$OUTPUT_FILE"
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

# Set model paths (aligned with Linux gst_launch sample)
$MODEL_1 = "yolo26s"
$MODEL_2 = "dima806_vehicle_10_types_image_detection"

$DETECTION_MODEL = "$env:MODELS_PATH\public\$MODEL_1\FP16\$MODEL_1.xml"
$VEHICLE_CLASSIFICATION_MODEL = "$env:MODELS_PATH\public\$MODEL_2\FP16\$MODEL_2.xml"

# Check if models exist
if (-not (Test-Path $DETECTION_MODEL)) {
    Write-Host "ERROR: Model not found: $DETECTION_MODEL" -ForegroundColor Red
    Write-Host "Please download models first using scripts/download_models helpers."
    exit 1
}

if (-not (Test-Path $VEHICLE_CLASSIFICATION_MODEL)) {
    Write-Host "ERROR: Model not found: $VEHICLE_CLASSIFICATION_MODEL" -ForegroundColor Red
    Write-Host "Please download models first using scripts/download_models helpers."
    exit 1
}

# Convert paths to forward slashes for GStreamer
$DETECTION_MODEL = $DETECTION_MODEL -replace '\\', '/'
$VEHICLE_CLASSIFICATION_MODEL = $VEHICLE_CLASSIFICATION_MODEL -replace '\\', '/'

# Reclassify interval (run classification every 10th frame)
$RECLASSIFY_INTERVAL = 10

# Build and run pipeline
Write-Host ""
Write-Host "Running pipeline..."
Write-Host ""

$CMD = "gst-launch-1.0 $SOURCE_ELEMENT ! $DECODE_ELEMENT$FrameLimiter ! queue ! gvadetect model=$DETECTION_MODEL inference-interval=$DetectionInterval threshold=0.4 device=$Device pre-process-backend=$PREPROC_BACKEND ! queue ! gvatrack tracking-type=$TrackingType ! queue ! gvaclassify model=$VEHICLE_CLASSIFICATION_MODEL reclassify-interval=$RECLASSIFY_INTERVAL device=$Device pre-process-backend=$PREPROC_BACKEND object-class=person ! queue ! $SINK_ELEMENT"
Invoke-Expression $CMD

exit $LASTEXITCODE
