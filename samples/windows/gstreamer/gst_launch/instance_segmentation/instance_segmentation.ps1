# ==============================================================================
# Copyright (C) 2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================

param(
    [string]$Model = "mask_rcnn_inception_resnet_v2_atrous_coco",
    [string]$Device = "CPU",
    [string]$InputSource = "https://videos.pexels.com/video-files/1192116/1192116-sd_640_360_30fps.mp4",
    [string]$OutputType = "file",
    [string]$JsonFile = "output.json",
    [string]$FrameLimiter = ""
)

# Show help
if ($Model -eq "--help" -or $Model -eq "-h") {
    Write-Host "Usage: instance_segmentation.ps1 [-Model <model>] [-Device <device>] [-InputSource <path>] [-OutputType <type>] [-JsonFile <file>] [-FrameLimiter <element>]"
    Write-Host ""
    Write-Host "Parameters:"
    Write-Host "  -Model          Model to use (default: mask_rcnn_inception_resnet_v2_atrous_coco)"
    Write-Host "                  Supported: mask_rcnn_inception_resnet_v2_atrous_coco, mask_rcnn_resnet50_atrous_coco"
    Write-Host "  -Device         Device (default: CPU). Supported: CPU, GPU, NPU"
    Write-Host "  -InputSource    Input source (default: Pexels video URL)"
    Write-Host "  -OutputType     Output type (default: file). Supported: file, display, fps, json, display-and-json, jpeg"
    Write-Host "  -JsonFile       JSON output file name (default: output.json)"
    Write-Host "  -FrameLimiter   Optional GStreamer element to add after decode (e.g., ' ! identity eos-after=100')"
    Write-Host ""
    exit 0
}

# Check MODELS_PATH
if (-not $env:MODELS_PATH) {
    Write-Host "ERROR: MODELS_PATH is not set." -ForegroundColor Red
    exit 1
}
Write-Host "MODELS_PATH: $env:MODELS_PATH"

# Validate model
$VALID_MODELS = @("mask_rcnn_inception_resnet_v2_atrous_coco", "mask_rcnn_resnet50_atrous_coco")
if ($VALID_MODELS -notcontains $Model) {
    Write-Host "ERROR: Unsupported model: $Model" -ForegroundColor Red
    Write-Host "Supported models: $($VALID_MODELS -join ', ')"
    exit 1
}

# Validate device
$VALID_DEVICES = @("CPU", "GPU", "NPU")
if ($VALID_DEVICES -notcontains $Device) {
    Write-Host "ERROR: Unsupported device: $Device" -ForegroundColor Red
    Write-Host "Supported devices: $($VALID_DEVICES -join ', ')"
    exit 1
}

# Set model paths
$MODEL_PATH = "$env:MODELS_PATH\public\$Model\FP16\$Model.xml"
$MODEL_PROC = "$PSScriptRoot\..\..\..\..\gstreamer\model_proc\public\mask-rcnn.json"

# Check if model exists
if (-not (Test-Path $MODEL_PATH)) {
    Write-Host "ERROR: Model not found: $MODEL_PATH" -ForegroundColor Red
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
        $OUTPUT_FILE = "instance_segmentation_${FILENAME}_${Device}.mp4"
        if (Test-Path $OUTPUT_FILE) { Remove-Item $OUTPUT_FILE }

        if ($Device -eq "CPU") {
            $SINK_ELEMENT = "videoconvert ! gvawatermark ! gvafpscounter ! openh264enc ! h264parse ! mp4mux ! filesink location=$OUTPUT_FILE"
        } else {
            $SINK_ELEMENT = "d3d11convert ! gvawatermark ! gvafpscounter ! d3d11h264enc ! h264parse ! mp4mux ! filesink location=$OUTPUT_FILE"
        }
    }
    "display" {
        if ($Device -eq "CPU") {
            $SINK_ELEMENT = "gvawatermark ! videoconvertscale ! gvafpscounter ! autovideosink sync=false"
        } else {
            $SINK_ELEMENT = "d3d11convert ! gvawatermark ! videoconvertscale ! gvafpscounter ! d3d11videosink sync=false"
        }
    }
    "fps" {
        $SINK_ELEMENT = "gvafpscounter ! fakesink sync=false"
    }
    "json" {
        if (Test-Path $JsonFile) { Remove-Item $JsonFile }
        $SINK_ELEMENT = "gvametaconvert add-tensor-data=true ! gvametapublish file-format=json-lines file-path=$JsonFile ! fakesink sync=false"
    }
    "display-and-json" {
        if (Test-Path $JsonFile) { Remove-Item $JsonFile }
        if ($Device -eq "CPU") {
            $SINK_ELEMENT = "gvawatermark ! gvametaconvert add-tensor-data=true ! gvametapublish file-format=json-lines file-path=$JsonFile ! videoconvert ! gvafpscounter ! autovideosink sync=false"
        } else {
            $SINK_ELEMENT = "d3d11convert ! gvawatermark ! gvametaconvert add-tensor-data=true ! gvametapublish file-format=json-lines file-path=$JsonFile ! videoconvert ! gvafpscounter ! d3d11videosink sync=false"
        }
    }
    "jpeg" {
        $FILENAME = [System.IO.Path]::GetFileNameWithoutExtension($InputSource)
        if ($Device -eq "CPU") {
            $SINK_ELEMENT = "videoconvert ! gvawatermark ! jpegenc ! multifilesink location=instance_segmentation_${FILENAME}_${Device}_%05d.jpeg"
        } else {
            $SINK_ELEMENT = "d3d11convert ! gvawatermark ! videoconvert ! jpegenc ! multifilesink location=instance_segmentation_${FILENAME}_${Device}_%05d.jpeg"
        }
    }
    default {
        Write-Host "ERROR: Invalid OUTPUT parameter" -ForegroundColor Red
        Write-Host "Valid values: file, display, fps, json, display-and-json, jpeg"
        exit 1
    }
}

# Convert paths to forward slashes for GStreamer
$MODEL_PATH = $MODEL_PATH -replace '\\', '/'
$MODEL_PROC = $MODEL_PROC -replace '\\', '/'

# Build and run pipeline
Write-Host ""
Write-Host "Running pipeline:"
Write-Host "gst-launch-1.0 $SOURCE_ELEMENT ! $DECODE_ELEMENT$FrameLimiter ! gvadetect model=$MODEL_PATH model-proc=$MODEL_PROC device=$Device pre-process-backend=$PREPROC_BACKEND ! queue ! $SINK_ELEMENT"
Write-Host ""

$CMD = "gst-launch-1.0 $SOURCE_ELEMENT ! $DECODE_ELEMENT$FrameLimiter ! gvadetect model=$MODEL_PATH model-proc=$MODEL_PROC device=$Device pre-process-backend=$PREPROC_BACKEND ! queue ! $SINK_ELEMENT"
Invoke-Expression $CMD

exit $LASTEXITCODE
