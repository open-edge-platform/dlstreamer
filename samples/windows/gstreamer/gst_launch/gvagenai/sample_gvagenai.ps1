# ==============================================================================
# Copyright (C) 2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================
# Video Summarization with MiniCPM-V, Phi-4-multimodal-instruct or Gemma 3
# model using the gvagenai element.
# ==============================================================================

param(
    [Alias("S")]  [string]$Source          = "https://videos.pexels.com/video-files/1192116/1192116-sd_640_360_30fps.mp4",
    [Alias("D")]  [string]$Device          = "CPU",
    [Alias("P")]  [string]$Prompt          = "Describe what you see in this video.",
    [Alias("F")]  [string]$FrameRate       = "1",
    [Alias("C")]  [string]$ChunkSize       = "10",
    [Alias("T")]  [string]$MaxTokens       = "100",
    [Alias("R")]  [string]$Resolution      = "",
    [Alias("V")]  [string]$VisionMode      = "image",
    [Alias("A")]  [string]$PipelineConfig  = "",
    [Alias("B")]  [string]$SchedulerConfig = "",
    [Alias("O")]  [string]$Output          = "genai_output.json",
    [Alias("M")]  [switch]$Metrics,
    [Alias("H")]  [switch]$Help
)

function Show-Usage {
    Write-Host "Usage: .\sample_gvagenai.ps1 [OPTIONS]"
    Write-Host ""
    Write-Host "Video Summarization with MiniCPM-V, Phi-4-multimodal-instruct or Gemma 3 model using gvagenai element"
    Write-Host ""
    Write-Host "Options:"
    Write-Host "  -Source FILE/URL/CAMERA        Input source (file path, URL, or 'camera'/webcam device index)"
    Write-Host "  -Device DEVICE                 Inference device (CPU, GPU, NPU, or indexed GPU like GPU.0). Default: CPU"
    Write-Host "  -Prompt TEXT                   Text prompt for the model"
    Write-Host "  -FrameRate RATE                Frame sampling rate (fps). Default: 1"
    Write-Host "  -ChunkSize NUM                 Chunk size, or frames per inference call. Default: 10"
    Write-Host "  -MaxTokens NUM                 Maximum new tokens to generate. Default: 100"
    Write-Host "  -Resolution WxH                Scale frames to WxH before inference (e.g. 320x240)."
    Write-Host "                                 Smaller frames mean faster inference. Default: source resolution"
    Write-Host "  -VisionMode image|video        How frames are presented to the model. 'video' requires a"
    Write-Host "                                 video-capable model (e.g. Qwen2/2.5/3-VL). Default: image"
    Write-Host "  -PipelineConfig KEY=VAL,..     OpenVINO device/plugin properties passed to the pipeline,"
    Write-Host "                                 as KEY=VALUE,KEY=VALUE. For NPU, nest per device, e.g."
    Write-Host "                                 NPU.MAX_PROMPT_LEN=2048,NPU.MIN_RESPONSE_LEN=512"
    Write-Host "  -SchedulerConfig KEY=VAL,..    Continuous-batching scheduler config, as KEY=VALUE,KEY=VALUE"
    Write-Host "                                 (e.g. enable_prefix_caching=true,use_cache_eviction=true)"
    Write-Host "  -Output FILE                   Output JSON file path. Default: genai_output.json"
    Write-Host "  -Metrics                       Include performance metrics in JSON output"
    Write-Host "  -Help                          Show this help message"
    Write-Host ""
    Write-Host "Examples:"
    Write-Host "  .\sample_gvagenai.ps1 -Source video.mp4 -Device GPU"
    Write-Host "  .\sample_gvagenai.ps1 -ChunkSize 1 -FrameRate 10"
    Write-Host "  .\sample_gvagenai.ps1 -Prompt `"Describe what do you see in this video?`""
    Write-Host "  .\sample_gvagenai.ps1 -Resolution 320x240 -Metrics -MaxTokens 200"
    Write-Host "  .\sample_gvagenai.ps1 -VisionMode video -ChunkSize 16 -FrameRate 2"
    Write-Host "  .\sample_gvagenai.ps1 -Device NPU -PipelineConfig NPU.MAX_PROMPT_LEN=2048,NPU.MIN_RESPONSE_LEN=512"
    Write-Host "  .\sample_gvagenai.ps1 -SchedulerConfig enable_prefix_caching=true -Output results.json"
    Write-Host ""
}

if ($Help) {
    Show-Usage
    exit 0
}

# Check if GENAI_MODEL_PATH is set
if (-not $env:GENAI_MODEL_PATH) {
    Write-Host "ERROR: GENAI_MODEL_PATH environment variable is not set." -ForegroundColor Red
    Write-Host "Please set it to the path where your MiniCPM-V, Phi-4-multimodal-instruct or Gemma 3 model is located."
    Write-Host 'Example: $env:GENAI_MODEL_PATH = "C:\models\MiniCPM-V-2_6"'
    exit 1
}

# Validate arguments
if ($Device -notmatch '^(CPU|NPU|GPU(\.[0-9]+)?)$') {
    Write-Host "ERROR: Invalid device: $Device. Use CPU, GPU, NPU, or indexed GPU like GPU.0." -ForegroundColor Red
    exit 1
}

$VALID_VISION_MODES = @("image", "video")
if ($VALID_VISION_MODES -notcontains $VisionMode) {
    Write-Host "ERROR: Invalid vision-mode: $VisionMode. Use 'image' or 'video'." -ForegroundColor Red
    exit 1
}

if ($Resolution -and ($Resolution -notmatch '^[0-9]+x[0-9]+$')) {
    Write-Host "ERROR: Invalid resolution: $Resolution. Expected WxH, e.g. 320x240." -ForegroundColor Red
    exit 1
}

# Check if model exists
if (-not (Test-Path -Path $env:GENAI_MODEL_PATH -PathType Container)) {
    Write-Host "ERROR: Model directory not found: $($env:GENAI_MODEL_PATH)" -ForegroundColor Red
    exit 1
}

# Enable element-scoped debug logging if metrics is enabled (only if GST_DEBUG is not set)
if ($Metrics -and -not $env:GST_DEBUG) {
    $env:GST_DEBUG = "gvagenai:4"
}

$MetricsValue = if ($Metrics) { "true" } else { "false" }

# Print configuration
Write-Host "=== sample gvagenai configuration ==="
Write-Host "Model Path: $($env:GENAI_MODEL_PATH)"
Write-Host "Source: $Source"
Write-Host "Device: $Device"
Write-Host "Prompt: $Prompt"
Write-Host "Frame Rate: $FrameRate fps"
Write-Host "Chunk Size: $ChunkSize"
Write-Host "Max New Tokens: $MaxTokens"
Write-Host "Vision Mode: $VisionMode"
Write-Host "Resolution: $(if ($Resolution) { $Resolution } else { 'source (no scaling)' })"
Write-Host "Pipeline Config: $(if ($PipelineConfig) { $PipelineConfig } else { 'none' })"
Write-Host "Scheduler Config: $(if ($SchedulerConfig) { $SchedulerConfig } else { 'none' })"
Write-Host "Output File: $Output"
Write-Host "Metrics: $MetricsValue"
Write-Host "==========================================="

# Determine the source element based on the input.
# On Windows a webcam is captured with mfvideosrc: pass '-Source camera'
# for the default device or '-Source <index>' (e.g. 0, 1) to pick a specific one.
if ($Source -match "://") {
    $SOURCE_ELEMENT = "urisourcebin buffer-size=4096 uri=$Source"
} elseif ($Source -eq "camera") {
    $SOURCE_ELEMENT = "mfvideosrc"
} elseif ($Source -match '^[0-9]+$') {
    $SOURCE_ELEMENT = "mfvideosrc device-index=$Source"
} else {
    $INPUT_PATH = $Source -replace '\\', '/'
    $SOURCE_ELEMENT = "filesrc location=`"$INPUT_PATH`""
}

# Generation configuration
$GENERATION_CONFIG = "max_new_tokens=$MaxTokens"

# Optional downscaling stage: smaller frames -> faster inference. gvagenai
# accepts RGB/BGR/NV12/I420 directly and converts internally.
#   - GPU/NPU: scale on the GPU with d3d11convert (feeds D3D11 memory straight into gvagenai).
#     d3d11upload in front is a pass-through when decodebin3 already decoded to D3D11 memory and
#     an upload when it decoded on the CPU.
#   - CPU: plain videoscale.
$SCALE_ELEMENT = ""
if ($Resolution) {
    $SCALE_WIDTH, $SCALE_HEIGHT = $Resolution -split 'x'
    if ($Device -eq "CPU") {
        $SCALE_ELEMENT = "videoscale ! video/x-raw,width=$SCALE_WIDTH,height=$SCALE_HEIGHT ! "
    } else {
        $SCALE_ELEMENT = "d3d11upload ! d3d11convert ! `"video/x-raw(memory:D3D11Memory),width=$SCALE_WIDTH,height=$SCALE_HEIGHT`" ! "
    }
}

# Model path with forward slashes for GStreamer
$MODEL_PATH = ($env:GENAI_MODEL_PATH) -replace '\\', '/'

# Add optional properties only when provided (e.g. NPU tuning, continuous batching).
$PIPELINE_CONFIG_PROP = ""
if ($PipelineConfig) {
    $PIPELINE_CONFIG_PROP = "pipeline-config=`"$PipelineConfig`" "
}

$SCHEDULER_CONFIG_PROP = ""
if ($SchedulerConfig) {
    $SCHEDULER_CONFIG_PROP = "scheduler-config=`"$SchedulerConfig`" "
}

$PIPELINE = "gst-launch-1.0 " +
    "$SOURCE_ELEMENT ! " +
    "decodebin3 ! " +
    "$SCALE_ELEMENT" +
    "gvagenai " +
        "device=$Device " +
        "model-path=`"$MODEL_PATH`" " +
        "prompt=`"$Prompt`" " +
        "generation-config=`"$GENERATION_CONFIG`" " +
        "frame-rate=$FrameRate " +
        "chunk-size=$ChunkSize " +
        "vision-mode=$VisionMode " +
        "$PIPELINE_CONFIG_PROP" +
        "$SCHEDULER_CONFIG_PROP" +
        "metrics=$MetricsValue ! " +
    "gvametapublish file-path=`"$Output`" ! " +
    "fakesink async=false"

Write-Host ""
Write-Host "Running gvagenai inference pipeline..."
Write-Host "Pipeline: $PIPELINE"
Write-Host ""

Invoke-Expression $PIPELINE

Write-Host ""
Write-Host "Pipeline execution completed."
Write-Host "Results saved to: $Output"

exit $LASTEXITCODE
