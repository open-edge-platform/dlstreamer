# ==============================================================================
# Copyright (C) 2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================
# This sample refers to a video file by Rihard-Clement-Ciprian Diac via Pexels
# (https://www.pexels.com)
# ==============================================================================

param(
    [string]$Model = "yolox_s",
    [string]$Device = "GPU",
    [string]$InputSource = "https://videos.pexels.com/video-files/1192116/1192116-sd_640_360_30fps.mp4",
    [string]$OutputType = "display",
    [string]$PreprocessBackend = "",
    [string]$Precision = "FP16",
    [string]$FrameLimiter = ""
)

# Show help
if ($Model -eq "--help" -or $Model -eq "-h") {
    Write-Host "Usage: yolo_detect.ps1 [-Model <model>] [-Device <device>] [-InputSource <path>] [-OutputType <type>] [-PreprocessBackend <backend>] [-Precision <precision>] [-FrameLimiter <element>]"
    Write-Host ""
    Write-Host "Parameters:"
    Write-Host "  -Model              Model name (default: yolox_s)"
    Write-Host "                      Supported: yolox-tiny, yolox_s, yolov5s, yolov5su, yolov7, yolov8s,"
    Write-Host "                                 yolov8n-obb, yolov8n-seg, yolov9c, yolov10s, yolo11s,"
    Write-Host "                                 yolo11s-seg, yolo11s-obb, yolo11s-pose, yolo26n, yolo26s,"
    Write-Host "                                 yolo26m, yolo26l, yolo26x, yolo26s-obb, yolo26s-seg, yolo26s-pose"
    Write-Host "  -Device             Device (default: GPU). Supported: CPU, GPU, NPU"
    Write-Host "  -InputSource        Input source (default: Pexels video URL)"
    Write-Host "  -OutputType         Output type (default: display). Supported: file, display, fps, json, display-and-json"
    Write-Host "  -PreprocessBackend  Preprocessing backend (default: auto). Supported: ie, opencv (CPU), d3d11 (GPU/NPU)"
    Write-Host "  -Precision          Model precision (default: FP16). Supported: INT8, FP32, FP16"
    Write-Host "  -FrameLimiter       Optional GStreamer element to add after decode (e.g., ' ! identity eos-after=100')"
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
$MODELS_LIST = @("yolox-tiny", "yolox_s", "yolov5s", "yolov5su", "yolov7", "yolov8s",
                 "yolov8n-obb", "yolov8n-seg", "yolov9c", "yolov10s", "yolo11s",
                 "yolo11s-seg", "yolo11s-obb", "yolo11s-pose", "yolo26n", "yolo26s",
                 "yolo26m", "yolo26l", "yolo26x", "yolo26s-obb", "yolo26s-seg", "yolo26s-pose")

if ($MODELS_LIST -notcontains $Model) {
    Write-Host "ERROR: Unsupported model: $Model" -ForegroundColor Red
    Write-Host "Supported models: $($MODELS_LIST -join ', ')"
    exit 1
}

# Check for yolov10s NPU restriction
if ($Model -eq "yolov10s" -and $Device -eq "NPU") {
    Write-Host "ERROR: No support of Yolov10s for NPU." -ForegroundColor Red
    exit 1
}

# Validate precision
$VALID_PRECISIONS = @("INT8", "FP32", "FP16")
if ($VALID_PRECISIONS -notcontains $Precision) {
    Write-Host "ERROR: Unsupported model precision: $Precision" -ForegroundColor Red
    Write-Host "Supported precisions: $($VALID_PRECISIONS -join ', ')"
    exit 1
}

# Validate device
$VALID_DEVICES = @("CPU", "GPU", "NPU")
if ($VALID_DEVICES -notcontains $Device) {
    Write-Host "ERROR: Unsupported device: $Device" -ForegroundColor Red
    Write-Host "Supported devices: $($VALID_DEVICES -join ', ')"
    exit 1
}

# Set model-proc file based on model
$MODEL_PROC = ""
switch ($Model) {
    "yolox-tiny" { $MODEL_PROC = "$PSScriptRoot\..\..\..\..\gstreamer\model_proc\public\yolo-x.json" }
    "yolox_s"    { $MODEL_PROC = "$PSScriptRoot\..\..\..\..\gstreamer\model_proc\public\yolo-x.json" }
    "yolov5s"    { $MODEL_PROC = "$PSScriptRoot\..\..\..\..\gstreamer\model_proc\public\yolo-v7.json" }
    "yolov5su"   { $MODEL_PROC = "$PSScriptRoot\..\..\..\..\gstreamer\model_proc\public\yolo-v8.json" }
    "yolov7"     { $MODEL_PROC = "$PSScriptRoot\..\..\..\..\gstreamer\model_proc\public\yolo-v7.json" }
}

# Set model path
$MODEL_PATH = "$env:MODELS_PATH\public\$Model\$Precision\$Model.xml"

# Check if model exists
if (-not (Test-Path $MODEL_PATH)) {
    Write-Host "ERROR: Model not found: $MODEL_PATH" -ForegroundColor Red
    Write-Host "Please run download_public_models.bat to download the models first."
    exit 1
}

# Set source element based on input type
if ($InputSource -match "://") {
    $SOURCE_ELEMENT = "urisourcebin uri=$InputSource"
} else {
    $INPUT_PATH = $InputSource -replace '\\', '/'
    $SOURCE_ELEMENT = "filesrc location=`"$INPUT_PATH`""
}

# Set pre-process backend based on device
if ($PreprocessBackend -eq "") {
    if ($Device -eq "CPU") {
        $PREPROC_BACKEND = "opencv"
    } else {
        $PREPROC_BACKEND = "d3d11"
    }
} else {
    $VALID_BACKENDS = @("ie", "opencv", "d3d11")
    if ($VALID_BACKENDS -notcontains $PreprocessBackend) {
        Write-Host "ERROR: Invalid PREPROC_BACKEND parameter. Supported values: ie, opencv, d3d11" -ForegroundColor Red
        exit 1
    }
    $PREPROC_BACKEND = $PreprocessBackend
}

# Set IE config for yolov10s on GPU
$IE_CONFIG = ""
if ($Model -eq "yolov10s" -and $Device -eq "GPU") {
    $IE_CONFIG = "ie-config=GPU_DISABLE_WINOGRAD_CONVOLUTION=YES"
}

# Set sink element based on output type
switch ($OutputType) {
    "file" {
        $FILENAME = [System.IO.Path]::GetFileNameWithoutExtension($InputSource)
        $OUTPUT_FILE = "yolo_${FILENAME}_${Model}_${Precision}_${Device}.mp4"
        if (Test-Path $OUTPUT_FILE) { Remove-Item $OUTPUT_FILE }
        $SINK_ELEMENT = "! queue ! d3d11convert ! gvawatermark ! gvafpscounter ! d3d11h264enc ! h264parse ! mp4mux ! filesink location=$OUTPUT_FILE"
    }
    "display" {
        $SINK_ELEMENT = "! queue ! d3d11convert ! gvawatermark ! videoconvert ! gvafpscounter ! d3d11videosink sync=false"
    }
    "fps" {
        $SINK_ELEMENT = "! queue ! gvafpscounter ! fakesink async=false"
    }
    "json" {
        if (Test-Path "output.json") { Remove-Item "output.json" }
        $SINK_ELEMENT = "! queue ! gvametaconvert add-tensor-data=true ! gvametapublish file-format=json-lines file-path=output.json ! fakesink async=false"
    }
    "display-and-json" {
        if (Test-Path "output.json") { Remove-Item "output.json" }
        $SINK_ELEMENT = "! queue ! d3d11convert ! gvawatermark ! gvametaconvert add-tensor-data=true ! gvametapublish file-format=json-lines file-path=output.json ! videoconvert ! gvafpscounter ! d3d11videosink sync=false"
    }
    default {
        Write-Host "ERROR: Invalid OUTPUT parameter" -ForegroundColor Red
        Write-Host "Valid values: file, display, fps, json, display-and-json"
        exit 1
    }
}

# Convert paths to forward slashes for GStreamer
$MODEL_PATH = $MODEL_PATH -replace '\\', '/'
if ($MODEL_PROC -ne "") {
    $MODEL_PROC = $MODEL_PROC -replace '\\', '/'
}

# Build pipeline parts
$MODEL_PROC_PART = ""
if ($MODEL_PROC -ne "") {
    $MODEL_PROC_PART = " model-proc=$MODEL_PROC"
}

$IE_CONFIG_PART = ""
if ($IE_CONFIG -ne "") {
    $IE_CONFIG_PART = " $IE_CONFIG"
}

# Build and run pipeline
Write-Host ""
Write-Host "Running pipeline:"
$CMD = "gst-launch-1.0 $SOURCE_ELEMENT ! decodebin3$FrameLimiter ! gvadetect model=$MODEL_PATH$MODEL_PROC_PART device=$Device pre-process-backend=$PREPROC_BACKEND$IE_CONFIG_PART $SINK_ELEMENT"
Write-Host $CMD
Write-Host ""

Invoke-Expression $CMD

exit $LASTEXITCODE
