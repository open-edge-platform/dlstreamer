# ==============================================================================
# Copyright (C) 2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================

param(
    [string]$InputSource = "DEFAULT",
    [string]$Method = "file",
    [string]$Output = "",
    [string]$Format = "",
    [string]$Topic = "dlstreamer",
    [string]$Device = "CPU",
    [string]$FrameLimiter = ""
)

if ($InputSource -eq "--help" -or $InputSource -eq "-h") {
    Write-Host "Usage: metapublish.ps1 [-InputSource <path>] [-Method <method>] [-Output <output>] [-Format <format>] [-Topic <topic>] [-Device <device>] [-FrameLimiter <element>]"
    Write-Host ""
    Write-Host "Parameters:"
    Write-Host "  -InputSource    Input source (default: sample video URL)"
    Write-Host "  -Method         Metapublish method (default: file). Supported: file, kafka, mqtt"
    Write-Host "  -Output         Output destination (default: stdout for file, localhost:9092 for kafka, localhost:1883 for mqtt)"
    Write-Host "  -Format         Output format (default: json for file, json-lines for kafka/mqtt). Supported: json, json-lines"
    Write-Host "  -Topic          Topic name (default: dlstreamer). Required for kafka and mqtt"
    Write-Host "  -Device         Inference device (default: CPU). Supported: CPU, GPU, NPU"
    Write-Host "  -FrameLimiter   Optional GStreamer element (e.g., ' ! identity eos-after=1000')"
    Write-Host ""
    exit 0
}

if (-not $env:MODELS_PATH) {
    Write-Host "ERROR: MODELS_PATH is not set." -ForegroundColor Red
    exit 1
}
Write-Host "MODELS_PATH: $env:MODELS_PATH"

if ($InputSource -eq "DEFAULT" -or $InputSource -eq ".") {
    $InputSource = "https://github.com/intel-iot-devkit/sample-videos/raw/master/head-pose-face-detection-female-and-male.mp4"
}

if ($InputSource -match "://") {
    $SOURCE_ELEMENT = "urisourcebin buffer-size=4096 uri=$InputSource"
} else {
    $SRC_FIXED = $InputSource -replace '\\', '/'
    $SOURCE_ELEMENT = "filesrc location=`"$SRC_FIXED`""
}

$OUTPUT_PROPERTY = ""
if ($Method -eq "file") {
    if ($Output -ne "") {
        $OUTPUT_PROPERTY = "file-path=$Output"
        if (Test-Path $Output) { Remove-Item $Output }
    }
    if ($Format -eq "") { $Format = "json" }
} elseif ($Method -eq "kafka") {
    if ($Output -eq "") { $Output = "localhost:9092" }
    if ($Format -eq "") { $Format = "json-lines" }
    $OUTPUT_PROPERTY = "address=$Output topic=$Topic"
} elseif ($Method -eq "mqtt") {
    if ($Output -eq "") { $Output = "localhost:1883" }
    if ($Format -eq "") { $Format = "json-lines" }
    $OUTPUT_PROPERTY = "address=$Output topic=$Topic"
} else {
    Write-Host "ERROR: Invalid method: $Method" -ForegroundColor Red
    Write-Host "Supported: file, kafka, mqtt"
    exit 1
}

$JSON_INDENT = if ($Format -eq "json-lines") { -1 } else { 4 }

$MODEL1_PATH = "$env:MODELS_PATH\intel\face-detection-adas-0001\FP32\face-detection-adas-0001.xml" -replace '\\', '/'
$MODEL2_PATH = "$env:MODELS_PATH\intel\age-gender-recognition-retail-0013\FP32\age-gender-recognition-retail-0013.xml" -replace '\\', '/'
$SCRIPTDIR = Split-Path -Parent $MyInvocation.MyCommand.Path
$MODEL2_PROC = (Join-Path $SCRIPTDIR "model_proc\age-gender-recognition-retail-0013.json") -replace '\\', '/'

if (-not (Test-Path ($MODEL1_PATH -replace '/', '\'))) {
    Write-Host "ERROR: Face detection model not found: $MODEL1_PATH" -ForegroundColor Red
    exit 1
}
if (-not (Test-Path ($MODEL2_PATH -replace '/', '\'))) {
    Write-Host "ERROR: Age-gender model not found: $MODEL2_PATH" -ForegroundColor Red
    exit 1
}

Write-Host ""
Write-Host "=============================================================================="
Write-Host "Running Pipeline:"
$PIPELINE = "$SOURCE_ELEMENT ! decodebin3$FrameLimiter ! gvadetect model=$MODEL1_PATH device=$Device ! queue ! gvaclassify model=$MODEL2_PATH model-proc=$MODEL2_PROC device=$Device ! queue ! gvametaconvert json-indent=$JSON_INDENT ! gvametapublish method=$Method file-format=$Format $OUTPUT_PROPERTY ! fakesink sync=false"
Write-Host "gst-launch-1.0 $PIPELINE"

cmd /c "gst-launch-1.0 $PIPELINE"

if ($LASTEXITCODE -ne 0) {
    Write-Host "[ERROR] Pipeline failed with exit code $LASTEXITCODE" -ForegroundColor Red
    exit $LASTEXITCODE
}

exit 0
