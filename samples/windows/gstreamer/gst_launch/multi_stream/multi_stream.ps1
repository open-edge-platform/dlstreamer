# ==============================================================================
# Copyright (C) 2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================

param(
    [string]$InputSource = "DEFAULT",
    [string]$DeviceStream12 = "",
    [string]$DeviceStream34 = "",
    [string]$Model1 = "",
    [string]$Model2 = "",
    [string]$Precision = "FP16",
    [string]$OutputType = "file",
    [string]$FrameLimiter = "",
    # Legacy single-device parameters (for backward compatibility)
    [string]$Device = "",
    [string]$Model = "",
    [int]$NumStreams = 0
)

# Show help
if ($InputSource -eq "--help" -or $InputSource -eq "-h") {
    Write-Host "Usage: multi_stream.ps1 [-InputSource <path>] [-DeviceStream12 <device>] [-DeviceStream34 <device>] [-Model1 <model>] [-Model2 <model>] [-Precision <precision>] [-OutputType <type>] [-FrameLimiter <element>]"
    Write-Host ""
    Write-Host "Parameters:"
    Write-Host "  -InputSource     Input source (default: Pexels video URL)"
    Write-Host "                   Use 'DEFAULT' for default video"
    Write-Host "  -DeviceStream12  Device for stream 1 & 2 (default: CPU). Supported: CPU, GPU, NPU"
    Write-Host "  -DeviceStream34  Device for stream 3 & 4 (default: GPU). Supported: CPU, GPU, NPU"
    Write-Host "  -Model1          Model for stream 1 & 2 (default: yolov8s)"
    Write-Host "                   Supported: yolox-tiny, yolox_s, yolov7, yolov8s, yolov9c, yolo11s, yolo26s"
    Write-Host "  -Model2          Model for stream 3 & 4 (default: yolov8s)"
    Write-Host "                   Supported: yolox-tiny, yolox_s, yolov7, yolov8s, yolov9c, yolo11s, yolo26s"
    Write-Host "  -Precision       Model precision (default: FP16). Supported: FP32, FP16, INT8"
    Write-Host "  -OutputType      Output type (default: file). Supported: file, json, fps"
    Write-Host "  -FrameLimiter    Optional GStreamer element to add after decode (e.g., ' ! identity eos-after=1000')"
    Write-Host ""
    Write-Host "Examples:"
    Write-Host "  # Two models on different devices (CPU + GPU)"
    Write-Host "  .\multi_stream.ps1 -DeviceStream12 CPU -DeviceStream34 GPU -Model1 yolov8s -Model2 yolov9c"
    Write-Host ""
    Write-Host "  # Same model on same device (all 4 streams on CPU)"
    Write-Host "  .\multi_stream.ps1 -DeviceStream12 CPU -DeviceStream34 CPU -Model1 yolov8s -Model2 yolov8s"
    Write-Host ""
    Write-Host "  # Legacy mode: simple multi-stream"
    Write-Host "  .\multi_stream.ps1 -Device CPU -Model yolov8s -NumStreams 4"
    Write-Host ""
    exit 0
}

# Get script directory at the beginning (before any functions are called)
$SCRIPT_DIR = Split-Path -Parent $MyInvocation.MyCommand.Path

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

# Determine operation mode: new (dual-device) or legacy (single-device)
$UseLegacyMode = $false
if ($Device -ne "" -or $Model -ne "" -or $NumStreams -gt 0) {
    $UseLegacyMode = $true
    Write-Host "Using legacy single-device mode" -ForegroundColor Yellow

    # Set defaults for legacy mode
    if ($Device -eq "") { $Device = "CPU" }
    if ($Model -eq "") { $Model = "yolov8s" }
    if ($NumStreams -eq 0) { $NumStreams = 4 }

    # Validate NumStreams
    if ($NumStreams -lt 1 -or $NumStreams -gt 8) {
        Write-Host "ERROR: NumStreams must be between 1 and 8." -ForegroundColor Red
        exit 1
    }
} else {
    # New dual-device mode (Linux-compatible)
    Write-Host "Using dual-device mode (4 streams: 2 + 2)" -ForegroundColor Cyan

    # Set defaults for new mode
    if ($DeviceStream12 -eq "") { $DeviceStream12 = "CPU" }
    if ($DeviceStream34 -eq "") { $DeviceStream34 = "GPU" }
    if ($Model1 -eq "") { $Model1 = "yolov8s" }
    if ($Model2 -eq "") { $Model2 = "yolov8s" }
}

# Function to get model path and model-proc
function Get-ModelInfo {
    param(
        [string]$ModelName,
        [string]$Precision,
        [string]$ScriptDirectory
    )

    $ModelPath = "$env:MODELS_PATH\public\$ModelName\$Precision\$ModelName.xml"
    $ModelPath = $ModelPath -replace '\\', '/'

    # Check if model exists
    if (-not (Test-Path ($ModelPath -replace '/', '\'))) {
        Write-Host "ERROR: Model not found: $ModelPath" -ForegroundColor Red
        exit 1
    }

    # Check for model-proc file
    $ModelProcPath = ""
    $MODEL_PROC_MAP = @{
        "yolox-tiny" = "yolo-x.json"
        "yolox_s" = "yolo-x.json"
        "yolov7" = "yolo-v7.json"
    }
    if ($MODEL_PROC_MAP.ContainsKey($ModelName)) {
        $ModelProcFile = $MODEL_PROC_MAP[$ModelName]
        $SearchPath = Join-Path (Split-Path (Split-Path $ScriptDirectory -Parent) -Parent) "model_proc\public\$ModelProcFile"
        if (Test-Path $SearchPath) {
            $ModelProcPath = ($SearchPath -replace '\\', '/')
            Write-Host "Using model-proc for ${ModelName}: $ModelProcPath" -ForegroundColor Cyan
        }
    }

    # Build model parameters
    $ModelParams = "model=$ModelPath"
    if ($ModelProcPath -ne "") {
        $ModelParams += " model-proc=$ModelProcPath"
    }

    return @{
        Path = $ModelPath
        Params = $ModelParams
    }
}

# Function to get preprocessing backend and sink elements
function Get-DeviceConfig {
    param([string]$DeviceName, [string]$OutputType)

    if ($DeviceName -eq "CPU") {
        $PreprocBackend = "opencv"
    } else {
        $PreprocBackend = "d3d11"
    }

    # Set sink element based on output type and device
    switch ($OutputType) {
        "json" {
            $SinkBase = "gvametaconvert add-tensor-data=true ! gvametapublish file-format=json-lines"
        }
        "fps" {
            $SinkBase = "gvafpscounter ! fakesink async=false"
        }
        "file" {
            if ($DeviceName -eq "CPU") {
                $SinkBase = "gvawatermark ! videoconvert ! x264enc ! h264parse ! mp4mux"
            } else {
                $SinkBase = "d3d11convert ! gvawatermark ! mfh264enc ! h264parse ! mp4mux"
            }
        }
        default {
            Write-Host "ERROR: Invalid OutputType parameter" -ForegroundColor Red
            Write-Host "Valid values: file, json, fps"
            exit 1
        }
    }

    return @{
        PreprocBackend = $PreprocBackend
        SinkBase = $SinkBase
    }
}

# Set source element based on input type
if ($InputSource -match "://") {
    $SOURCE_ELEMENT = "urisourcebin buffer-size=4096 uri=$InputSource"
} else {
    $SRC_FIXED = $InputSource -replace '\\', '/'
    $SOURCE_ELEMENT = "filesrc location=`"$SRC_FIXED`""
}

# ==============================================================================
# LEGACY MODE: Single-device multiple streams
# ==============================================================================
if ($UseLegacyMode) {
    Write-Host ""
    Write-Host "=============================================================================="
    Write-Host "Running Multi-Stream Pipeline (Legacy Mode):"
    Write-Host "  Streams: $NumStreams"
    Write-Host "  Model: $Model ($Precision)"
    Write-Host "  Device: $Device"
    Write-Host "  Output: $OutputType"
    Write-Host "=============================================================================="
    Write-Host ""

    # Get model info
    $ModelInfo = Get-ModelInfo -ModelName $Model -Precision $Precision -ScriptDirectory $SCRIPT_DIR

    # Get device config
    $DeviceConfig = Get-DeviceConfig -DeviceName $Device -OutputType $OutputType

    # Clean up previous output files
    if ($OutputType -eq "json") {
        for ($i = 1; $i -le $NumStreams; $i++) {
            $OutputFile = "multi_stream_$i.json"
            if (Test-Path $OutputFile) { Remove-Item $OutputFile }
        }
    } elseif ($OutputType -eq "file") {
        for ($i = 1; $i -le $NumStreams; $i++) {
            $OutputFile = "multi_stream_$i.mp4"
            if (Test-Path $OutputFile) { Remove-Item $OutputFile }
        }
    }

    # Build individual stream pipelines
    $STREAM_PIPELINES = @()
    for ($i = 1; $i -le $NumStreams; $i++) {
        $SINK_STR = switch ($OutputType) {
            "json" {
                "$($DeviceConfig.SinkBase) file-path=multi_stream_$i.json ! fakesink async=false"
            }
            "fps" {
                $DeviceConfig.SinkBase
            }
            "file" {
                "$($DeviceConfig.SinkBase) ! filesink location=multi_stream_$i.mp4"
            }
        }

        # Use model-instance-id to share model across streams
        $INSTANCE_ID = "model-instance-id=shared_model"

        $STREAM_PIPELINE = "$SOURCE_ELEMENT ! decodebin3$FrameLimiter ! gvadetect $($ModelInfo.Params) device=$Device pre-process-backend=$($DeviceConfig.PreprocBackend) $INSTANCE_ID nireq=4 ! queue ! $SINK_STR"
        $STREAM_PIPELINES += $STREAM_PIPELINE
    }

    # Combine all streams into single gst-launch command
    $CMD = "gst-launch-1.0 -e " + ($STREAM_PIPELINES -join " ")

} else {
    # ==============================================================================
    # NEW MODE: Dual-device (4 streams: 2+2)
    # ==============================================================================
    Write-Host ""
    Write-Host "=============================================================================="
    Write-Host "Running Multi-Stream Pipeline (Dual-Device Mode):"
    Write-Host "  Stream 1 & 2: $DeviceStream12 - $Model1 ($Precision)"
    Write-Host "  Stream 3 & 4: $DeviceStream34 - $Model2 ($Precision)"
    Write-Host "  Output: $OutputType"
    Write-Host "=============================================================================="
    Write-Host ""

    # Get model info for both models
    $Model1Info = Get-ModelInfo -ModelName $Model1 -Precision $Precision -ScriptDirectory $SCRIPT_DIR
    $Model2Info = Get-ModelInfo -ModelName $Model2 -Precision $Precision -ScriptDirectory $SCRIPT_DIR

    # Get device configs
    $Device12Config = Get-DeviceConfig -DeviceName $DeviceStream12 -OutputType $OutputType
    $Device34Config = Get-DeviceConfig -DeviceName $DeviceStream34 -OutputType $OutputType

    # Clean up previous output files
    if ($OutputType -eq "json") {
        1..4 | ForEach-Object {
            $OutputFile = "multi_stream_${_}.json"
            if (Test-Path $OutputFile) { Remove-Item $OutputFile }
        }
    } elseif ($OutputType -eq "file") {
        1..4 | ForEach-Object {
            $OutputFile = "multi_stream_${_}.mp4"
            if (Test-Path $OutputFile) { Remove-Item $OutputFile }
        }
    }

    # Build 4 stream pipelines (2 with Device1/Model1, 2 with Device2/Model2)
    $STREAM_PIPELINES = @()

    # Streams 1 & 2: DeviceStream12 + Model1
    for ($i = 1; $i -le 2; $i++) {
        $SINK_STR = switch ($OutputType) {
            "json" {
                "$($Device12Config.SinkBase) file-path=multi_stream_$i.json ! fakesink async=false"
            }
            "fps" {
                $Device12Config.SinkBase
            }
            "file" {
                "$($Device12Config.SinkBase) ! filesink location=multi_stream_$i.mp4"
            }
        }

        $INSTANCE_ID = "model-instance-id=inf0"
        $STREAM_PIPELINE = "$SOURCE_ELEMENT ! decodebin3$FrameLimiter ! gvadetect $($Model1Info.Params) device=$DeviceStream12 pre-process-backend=$($Device12Config.PreprocBackend) $INSTANCE_ID nireq=4 ! queue ! $SINK_STR"
        $STREAM_PIPELINES += $STREAM_PIPELINE
    }

    # Streams 3 & 4: DeviceStream34 + Model2
    for ($i = 3; $i -le 4; $i++) {
        $SINK_STR = switch ($OutputType) {
            "json" {
                "$($Device34Config.SinkBase) file-path=multi_stream_$i.json ! fakesink async=false"
            }
            "fps" {
                $Device34Config.SinkBase
            }
            "file" {
                "$($Device34Config.SinkBase) ! filesink location=multi_stream_$i.mp4"
            }
        }

        $INSTANCE_ID = "model-instance-id=inf1"
        $STREAM_PIPELINE = "$SOURCE_ELEMENT ! decodebin3$FrameLimiter ! gvadetect $($Model2Info.Params) device=$DeviceStream34 pre-process-backend=$($Device34Config.PreprocBackend) $INSTANCE_ID nireq=4 ! queue ! $SINK_STR"
        $STREAM_PIPELINES += $STREAM_PIPELINE
    }

    # Combine all streams into single gst-launch command
    $CMD = "gst-launch-1.0 -e " + ($STREAM_PIPELINES -join " ")
}

Write-Host "Pipeline command:"
Write-Host $CMD
Write-Host ""

# Measure execution time
$StartTime = Get-Date

Invoke-Expression $CMD

$EndTime = Get-Date
$Duration = ($EndTime - $StartTime).TotalSeconds

if ($LASTEXITCODE -ne 0) {
    Write-Host "[ERROR] Pipeline failed with exit code $LASTEXITCODE" -ForegroundColor Red
    exit $LASTEXITCODE
}

Write-Host ""
Write-Host "=============================================================================="
Write-Host "Multi-Stream Processing Complete" -ForegroundColor Green
Write-Host "  Total Duration: $([math]::Round($Duration, 2)) seconds"

if ($UseLegacyMode) {
    Write-Host "  Streams Processed: $NumStreams"
    Write-Host "  Average Time per Stream: $([math]::Round($Duration / $NumStreams, 2)) seconds"
} else {
    Write-Host "  Streams Processed: 4 (2 + 2)"
}

# If JSON output, combine all files
if ($OutputType -eq "json") {
    Write-Host ""
    Write-Host "Combining JSON outputs into output.json..."

    if (Test-Path "output.json") { Remove-Item "output.json" }

    $NumFiles = if ($UseLegacyMode) { $NumStreams } else { 4 }
    for ($i = 1; $i -le $NumFiles; $i++) {
        $StreamFile = "multi_stream_$i.json"
        if (Test-Path $StreamFile) {
            Get-Content $StreamFile | Where-Object { $_.Trim() -ne "" } | Add-Content "output.json"
            Write-Host "  Added stream $i results"
        }
    }

    Write-Host "  Combined output saved to: output.json" -ForegroundColor Cyan
}

# If file output, list created files
if ($OutputType -eq "file") {
    Write-Host ""
    Write-Host "Output files created:"
    $NumFiles = if ($UseLegacyMode) { $NumStreams } else { 4 }
    for ($i = 1; $i -le $NumFiles; $i++) {
        $OutputFile = "multi_stream_$i.mp4"
        if (Test-Path $OutputFile) {
            $FileSize = [math]::Round((Get-Item $OutputFile).Length / 1MB, 2)
            Write-Host "  $OutputFile ($FileSize MB)" -ForegroundColor Cyan
        }
    }
}

Write-Host "=============================================================================="
Write-Host ""

exit 0
