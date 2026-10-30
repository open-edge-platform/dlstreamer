# ==============================================================================
# Copyright (C) 2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================

param(
    [string]$InputSource = "https://videos.pexels.com/video-files/1192116/1192116-sd_640_360_30fps.mp4",
    [string]$Device = "CPU",
    [string]$Precision = "FP16",
    [string]$Model = "openai_clip-vit-large-patch14",
    [string]$PreprocessBackend = "",
    [string]$OutputType = "json",
    [string]$FrameLimiter = ""
)

# Show help
if ($InputSource -eq "--help" -or $InputSource -eq "-h") {
    Write-Host "Usage: generate_frame_embedding.ps1 [-InputSource <path>] [-Device <device>] [-Precision <precision>] [-Model <model>] [-PreprocessBackend <backend>] [-OutputType <type>] [-FrameLimiter <element>]"
    Write-Host ""
    Write-Host "Parameters:"
    Write-Host "  -InputSource        Input source (default: Pexels video URL)"
    Write-Host "  -Device             Device (default: CPU). Supported: CPU, GPU, NPU"
    Write-Host "  -Precision          Model precision (default: FP16). Supported: FP32, FP16, INT8"
    Write-Host "  -Model              Model name (default: openai_clip-vit-large-patch14)"
    Write-Host "  -PreprocessBackend  Preprocessing backend (default: opencv for CPU, d3d12-surface-sharing for GPU, d3d12 for NPU)"
    Write-Host "                      Supported: ie, opencv, d3d11, d3d11-surface-sharing, d3d12, d3d12-surface-sharing"
    Write-Host "  -OutputType         Output type (default: json). Supported: json, fps"
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
$VALID_DEVICES = @("CPU", "GPU", "NPU")
if ($VALID_DEVICES -notcontains $Device) {
    Write-Host "ERROR: Unsupported device: $Device" -ForegroundColor Red
    Write-Host "Supported devices: $($VALID_DEVICES -join ', ')"
    exit 1
}

# Validate precision
$VALID_PRECISIONS = @("FP32", "FP16", "INT8")
if ($VALID_PRECISIONS -notcontains $Precision) {
    Write-Host "ERROR: Unsupported precision: $Precision" -ForegroundColor Red
    Write-Host "Supported precisions: $($VALID_PRECISIONS -join ', ')"
    exit 1
}

# Set model path
$MODEL_PATH = "$env:MODELS_PATH\public\$Model\$Precision\$Model.xml"

# Check if model exists
if (-not (Test-Path $MODEL_PATH)) {
    Write-Host "ERROR: Model not found: $MODEL_PATH" -ForegroundColor Red
    Write-Host "Please check if the precision $Precision folder exists."
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
if ($PreprocessBackend -eq "") {
    switch ($Device) {
        "CPU" { $PPBKEND = "opencv" }
        "GPU" { $PPBKEND = "d3d12-surface-sharing" }
        "NPU" { $PPBKEND = "d3d12" }
    }
} else {
    $VALID_BACKENDS = @("ie", "opencv", "d3d11", "d3d11-surface-sharing", "d3d12", "d3d12-surface-sharing")
    if ($VALID_BACKENDS -notcontains $PreprocessBackend) {
        Write-Host "ERROR: Unsupported preprocessing backend: $PreprocessBackend" -ForegroundColor Red
        Write-Host "Supported backends: $($VALID_BACKENDS -join ', ')"
        exit 1
    }
    if ($Device -eq "CPU" -and $PreprocessBackend -like "d3d*") {
        Write-Host "ERROR: $PreprocessBackend backend requires GPU or NPU device" -ForegroundColor Red
        exit 1
    }
    $PPBKEND = $PreprocessBackend
}

# The processing element selects the memory type the inference element receives, which must
# match the backend: system memory for ie/opencv, D3D12Memory for d3d12 backends and
# D3D11Memory for d3d11 backends.
if ($PPBKEND -like "d3d12*") {
    $PROC_ELEMENT = "d3d12convert"
} elseif ($PPBKEND -like "d3d11*") {
    $PROC_ELEMENT = "d3d11convert"
} else {
    # videoconvert passes D3D memory through untouched, so system memory is requested explicitly
    $PROC_ELEMENT = "videoconvert ! videoscale ! video/x-raw"
}

# Set sink element based on output type
switch ($OutputType) {
    "json" {
        if (Test-Path "output.json") { Remove-Item "output.json" -Force }
        $SINK_ELEMENT = "gvametaconvert format=json add-tensor-data=true ! gvametapublish method=file file-format=json-lines file-path=output.json ! fakesink"
    }
    "fps" {
        $SINK_ELEMENT = "gvafpscounter ! fakesink"
    }
    default {
        Write-Host "ERROR: Unsupported output type: $OutputType" -ForegroundColor Red
        Write-Host "Supported outputs: json, fps"
        exit 1
    }
}

# Convert paths to forward slashes for GStreamer
$MODEL_PATH = $MODEL_PATH -replace '\\', '/'

# Build and run pipeline
Write-Host ""
Write-Host "Running pipeline:"
Write-Host "gst-launch-1.0 $SOURCE_ELEMENT ! decodebin3$FrameLimiter ! $PROC_ELEMENT ! gvainference model=$MODEL_PATH device=$Device pre-process-backend=$PPBKEND ! $SINK_ELEMENT"
Write-Host ""

$CMD = "gst-launch-1.0 $SOURCE_ELEMENT ! decodebin3$FrameLimiter ! $PROC_ELEMENT ! gvainference model=$MODEL_PATH device=$Device pre-process-backend=$PPBKEND ! $SINK_ELEMENT"
Invoke-Expression $CMD

exit $LASTEXITCODE
