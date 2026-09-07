#!/bin/bash
# ==============================================================================
# Copyright (C) 2025-2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================

set -euo pipefail

# Default values
DEFAULT_SOURCE="https://videos.pexels.com/video-files/1192116/1192116-sd_640_360_30fps.mp4"
DEFAULT_DEVICE="CPU"
DEFAULT_PROMPT="Describe what you see in this video."
DEFAULT_FRAME_RATE="1"
DEFAULT_CHUNK_SIZE="10"
DEFAULT_MAX_NEW_TOKENS="100"
DEFAULT_METRICS="false"
DEFAULT_VISION_MODE="image"
DEFAULT_RESOLUTION=""       # empty = keep source resolution (no scaling)
DEFAULT_PIPELINE_CONFIG=""  # empty = no extra device/plugin properties
DEFAULT_SCHEDULER_CONFIG="" # empty = no continuous-batching scheduler config
DEFAULT_BACKEND="openvino-genai" # or "openai-http" for an external VLM service
DEFAULT_HTTP_SERVER_URL="http://localhost:8000/v1"
DEFAULT_TRIGGER_CLASSES=""  # empty = no detection-based frame selection (frame-rate sampling only)
DEFAULT_TRIGGER_MODE="any"
DEFAULT_TRIGGER_MIN_CONFIDENCE="0.5"
DEFAULT_DETECT_THRESHOLD="0.5"
DEFAULT_OUTPUT="genai_output.json"

# Function to display usage information
show_usage() {
    echo "Usage: $0 [OPTIONS]"
    echo ""
    echo "Video Summarization with MiniCPM-V, Phi-4-multimodal-instruct or Gemma 3 model using gvagenai element"
    echo ""
    echo "Options:"
    echo "  -S, --source FILE/URL/CAMERA      Input source (file path, URL or web camera)"
    echo "  -D, --device DEVICE               Inference device (CPU, GPU, NPU, or indexed GPU like GPU.0)"
    echo "  -P, --prompt TEXT                 Text prompt for the model"
    echo "  -F, --frame-rate RATE             Frame sampling rate (fps)"
    echo "  -C, --chunk-size NUM              Chunk size, or frames per inference call"
    echo "  -T, --max-tokens NUM              Maximum new tokens to generate"
    echo "  -R, --resolution WxH              Scale frames to WxH before inference (e.g. 320x240)."
    echo "                                    Smaller frames mean faster inference. Default: source resolution"
    echo "  -V, --vision-mode image|video     How frames are presented to the model. 'video' requires a"
    echo "                                    video-capable model (e.g. Qwen2/2.5/3-VL). Default: image"
    echo "  -A, --pipeline-config KEY=VAL,..  OpenVINO device/plugin properties passed to the pipeline,"
    echo "                                    as KEY=VALUE,KEY=VALUE. For NPU, nest per device, e.g."
    echo "                                    NPU.MAX_PROMPT_LEN=2048,NPU.MIN_RESPONSE_LEN=512"
    echo "  -B, --scheduler-config KEY=VAL,.. Continuous-batching scheduler config, as KEY=VALUE,KEY=VALUE"
    echo "                                    (e.g. enable_prefix_caching=true,use_cache_eviction=true)"
    echo "  -b, --backend NAME                VLM backend: 'openvino-genai' (local) or 'openai-http'"
    echo "                                    (external OpenAI-compatible server). Default: openvino-genai"
    echo "  -U, --http-server-url URL         Base URL of the external server (openai-http backend),"
    echo "                                    e.g. http://localhost:8000/v1"
    echo "  -c, --trigger-classes LIST        Comma-separated object classes from an upstream gvadetect that"
    echo "                                    force VLM analysis, e.g. \"person,car\". Requires MODELS_PATH or"
    echo "                                    DETECTION_MODEL. Empty (default) disables frame selection"
    echo "  -m, --trigger-mode any|all        'any' triggers on one detected class, 'all' requires every listed"
    echo "                                    class in the same frame. Default: any"
    echo "  -n, --trigger-min-confidence NUM  Minimum gvadetect confidence [0.0-1.0] for a trigger. Default: 0.5"
    echo "  -t, --threshold NUM               gvadetect detection threshold [0.0-1.0]. Default: 0.5"
    echo "  -O, --output FILE                 Output JSON file path. Default: genai_output.json"
    echo "  -M, --metrics                     Include performance metrics in JSON output"
    echo "  -H, --help                        Show this help message"
    echo ""
    echo "Environment:"
    echo "  GENAI_MODEL_PATH   Required for the local 'openvino-genai' backend: path to the VLM model"
    echo "  GENAI_MODEL_NAME   Optional model name for the 'openai-http' backend (default: GENAI_MODEL_PATH or 'vlm')"
    echo "  HTTP_API_KEY       Optional. Bearer token / API key for the 'openai-http' backend"
    echo "  MODELS_PATH        Required with --trigger-classes (unless DETECTION_MODEL is set): root of the"
    echo "                     downloaded detection models"
    echo "  DETECTION_MODEL    Optional. Full path to the gvadetect model .xml"
    echo "                     (default: \$MODELS_PATH/public/yolov8s/FP16/yolov8s.xml)"
    echo ""
    echo "Examples:"
    echo "  $0 --source video.mp4 --device GPU"
    echo "  $0 --chunk-size 1 --frame-rate 10"
    echo "  $0 --prompt \"Describe what do you see in this video?\""
    echo "  $0 --resolution 320x240 --metrics --max-tokens 200"
    echo "  $0 --vision-mode video --chunk-size 16 --frame-rate 2"
    echo "  $0 --device NPU --pipeline-config NPU.MAX_PROMPT_LEN=2048,NPU.MIN_RESPONSE_LEN=512"
    echo "  $0 --scheduler-config enable_prefix_caching=true --output results.json"
    echo "  $0 --trigger-classes person --frame-rate 0   # event-driven: VLM only when a person is detected"
    echo "  $0 --trigger-classes car --frame-rate 2       # hybrid: fixed-rate sampling plus car-triggered frames"
    echo "  $0 --backend openai-http --http-server-url http://localhost:8000/v1 --trigger-classes person"
    echo ""
}

# Initialize variables with defaults
INPUT="$DEFAULT_SOURCE"
DEVICE="$DEFAULT_DEVICE"
PROMPT="$DEFAULT_PROMPT"
FRAME_RATE="$DEFAULT_FRAME_RATE"
CHUNK_SIZE="$DEFAULT_CHUNK_SIZE"
MAX_NEW_TOKENS="$DEFAULT_MAX_NEW_TOKENS"
METRICS="$DEFAULT_METRICS"
VISION_MODE="$DEFAULT_VISION_MODE"
RESOLUTION="$DEFAULT_RESOLUTION"
PIPELINE_CONFIG="$DEFAULT_PIPELINE_CONFIG"
SCHEDULER_CONFIG="$DEFAULT_SCHEDULER_CONFIG"
BACKEND="$DEFAULT_BACKEND"
HTTP_SERVER_URL="$DEFAULT_HTTP_SERVER_URL"
TRIGGER_CLASSES="$DEFAULT_TRIGGER_CLASSES"
TRIGGER_MODE="$DEFAULT_TRIGGER_MODE"
TRIGGER_MIN_CONFIDENCE="$DEFAULT_TRIGGER_MIN_CONFIDENCE"
DETECT_THRESHOLD="$DEFAULT_DETECT_THRESHOLD"
OUTPUT_FILE="$DEFAULT_OUTPUT"

# Parse command line arguments
while [[ $# -gt 0 ]]; do
    case $1 in
        -S|--source)
            INPUT="$2"
            shift 2
            ;;
        -D|--device)
            DEVICE="$2"
            shift 2
            ;;
        -P|--prompt)
            PROMPT="$2"
            shift 2
            ;;
        -F|--frame-rate)
            FRAME_RATE="$2"
            shift 2
            ;;
        -C|--chunk-size)
            CHUNK_SIZE="$2"
            shift 2
            ;;
        -T|--max-tokens)
            MAX_NEW_TOKENS="$2"
            shift 2
            ;;
        -R|--resolution)
            RESOLUTION="$2"
            shift 2
            ;;
        -V|--vision-mode)
            VISION_MODE="$2"
            shift 2
            ;;
        -A|--pipeline-config)
            PIPELINE_CONFIG="$2"
            shift 2
            ;;
        -B|--scheduler-config)
            SCHEDULER_CONFIG="$2"
            shift 2
            ;;
        -b|--backend)
            BACKEND="$2"
            shift 2
            ;;
        -U|--http-server-url)
            HTTP_SERVER_URL="$2"
            shift 2
            ;;
        -c|--trigger-classes)
            TRIGGER_CLASSES="$2"
            shift 2
            ;;
        -m|--trigger-mode)
            TRIGGER_MODE="$2"
            shift 2
            ;;
        -n|--trigger-min-confidence)
            TRIGGER_MIN_CONFIDENCE="$2"
            shift 2
            ;;
        -t|--threshold)
            DETECT_THRESHOLD="$2"
            shift 2
            ;;
        -O|--output)
            OUTPUT_FILE="$2"
            shift 2
            ;;
        -M|--metrics)
            METRICS="true"
            shift
            ;;
        -H|--help)
            show_usage
            exit 0
            ;;
        *)
            echo "Unknown option: $1" >&2
            show_usage
            exit 1
            ;;
    esac
done

# Validate arguments
if [[ ! "$DEVICE" =~ ^(CPU|NPU|GPU(\.[0-9]+)?)$ ]]; then
    echo "ERROR - Invalid device: $DEVICE. Use CPU, GPU, NPU, or an indexed GPU device like GPU.0." >&2
    exit 1
fi

if [[ "$VISION_MODE" != "image" && "$VISION_MODE" != "video" ]]; then
    echo "ERROR - Invalid vision-mode: $VISION_MODE. Use 'image' or 'video'." >&2
    exit 1
fi

if [[ "$BACKEND" != "openvino-genai" && "$BACKEND" != "openai-http" ]]; then
    echo "ERROR - Invalid backend: $BACKEND. Use 'openvino-genai' or 'openai-http'." >&2
    exit 1
fi

if [[ "$TRIGGER_MODE" != "any" && "$TRIGGER_MODE" != "all" ]]; then
    echo "ERROR - Invalid trigger-mode: $TRIGGER_MODE. Use 'any' or 'all'." >&2
    exit 1
fi

if [[ -n "$RESOLUTION" && ! "$RESOLUTION" =~ ^[0-9]+x[0-9]+$ ]]; then
    echo "ERROR - Invalid resolution: $RESOLUTION. Expected WxH, e.g. 320x240." >&2
    exit 1
fi

# Detection model, needed only when --trigger-classes selects frames via gvadetect
if [[ -n "$TRIGGER_CLASSES" ]]; then
    if [ -z "${MODELS_PATH:-}" ] && [ -z "${DETECTION_MODEL:-}" ]; then
        echo "ERROR - MODELS_PATH or DETECTION_MODEL must be set to use --trigger-classes (needed for gvadetect)." >&2
        exit 1
    fi
    DETECTION_MODEL="${DETECTION_MODEL:-${MODELS_PATH}/public/yolov8s/FP16/yolov8s.xml}"
    if [ ! -f "$DETECTION_MODEL" ]; then
        echo "ERROR - Detection model not found: $DETECTION_MODEL" >&2
        exit 1
    fi
fi

# Backend-specific configuration and validation
if [[ "$BACKEND" == "openvino-genai" ]]; then
    if [ -z "${GENAI_MODEL_PATH:-}" ]; then
        echo "ERROR - GENAI_MODEL_PATH environment variable is not set." >&2
        echo "Please set it to the path where your MiniCPM-V, Phi-4-multimodal-instruct or Gemma 3 model is located." >&2
        echo "Examples: export GENAI_MODEL_PATH=/path/to/minicpm-v-model" >&2
        echo "          export GENAI_MODEL_PATH=/path/to/Phi-4-multimodal" >&2
        echo "          export GENAI_MODEL_PATH=/path/to/gemma-3-model" >&2
        exit 1
    fi
    if [ ! -d "$GENAI_MODEL_PATH" ]; then
        echo "ERROR - Model directory not found: $GENAI_MODEL_PATH" >&2
        exit 1
    fi
    # For the local backend, model-path points to the on-disk VLM model
    GENAI_MODEL_PROP="model-path=\"$GENAI_MODEL_PATH\" device=$DEVICE"
else
    # openai-http: model-path is the model NAME requested from the server
    GENAI_MODEL_NAME="${GENAI_MODEL_NAME:-${GENAI_MODEL_PATH:-vlm}}"
    API_KEY_PROP=""
    if [ -n "${HTTP_API_KEY:-}" ]; then
        API_KEY_PROP="http-api-key=\"$HTTP_API_KEY\""
    fi
    GENAI_MODEL_PROP="model-path=\"$GENAI_MODEL_NAME\" http-server-url=\"$HTTP_SERVER_URL\" $API_KEY_PROP"
fi

# Enable element-scoped debug logging if metrics is enabled (only if GST_DEBUG is not set)
if [[ "$METRICS" == "true" && -z "${GST_DEBUG:-}" ]]; then
    export GST_DEBUG=gvagenai:4
fi

# Print configuration
echo "=== sample gvagenai configuration ==="
echo "Source: $INPUT"
echo "Device: $DEVICE"
echo "Prompt: $PROMPT"
echo "Frame Rate: $FRAME_RATE fps"
echo "Chunk Size: $CHUNK_SIZE"
echo "Max New Tokens: $MAX_NEW_TOKENS"
echo "Vision Mode: $VISION_MODE"
echo "Resolution: ${RESOLUTION:-source (no scaling)}"
echo "Pipeline Config: ${PIPELINE_CONFIG:-none}"
echo "Scheduler Config: ${SCHEDULER_CONFIG:-none}"
echo "Backend: $BACKEND"
if [[ "$BACKEND" == "openvino-genai" ]]; then
    echo "Model Path: $GENAI_MODEL_PATH"
else
    echo "HTTP Server URL: $HTTP_SERVER_URL"
fi
echo "Trigger Classes: ${TRIGGER_CLASSES:-none (frame-rate sampling only)}"
if [[ -n "$TRIGGER_CLASSES" ]]; then
    echo "Trigger Mode: $TRIGGER_MODE"
    echo "Trigger Min Confidence: $TRIGGER_MIN_CONFIDENCE"
    echo "Detection Model: $DETECTION_MODEL"
    echo "Detection Threshold: $DETECT_THRESHOLD"
fi
echo "Output File: $OUTPUT_FILE"
echo "Metrics: $METRICS"
echo "==========================================="

# Determine the source element based on the input
if [[ $INPUT == "/dev/video"* ]]; then
    SOURCE_ELEMENT="v4l2src device=${INPUT}"
elif [[ "$INPUT" == *"://"* ]]; then
    SOURCE_ELEMENT="urisourcebin buffer-size=4096 uri=${INPUT}"
else
    SOURCE_ELEMENT="filesrc location=${INPUT}"
fi

# Generation configuration
GENERATION_CONFIG="max_new_tokens=${MAX_NEW_TOKENS}"

# Optional downscaling stage: smaller frames -> faster inference. Prefer VA-API scaling
# (vapostproc) when available; fall back to the CPU videoscale otherwise. gvagenai
# accepts RGB/BGR/NV12/I420 directly and converts internally.
SCALE_ELEMENT=""
if [[ -n "$RESOLUTION" ]]; then
    SCALE_WIDTH="${RESOLUTION%x*}"
    SCALE_HEIGHT="${RESOLUTION#*x}"
    if gst-inspect-1.0 vapostproc >/dev/null 2>&1; then
        SCALER="vapostproc"
    else
        SCALER="videoscale"
    fi
    SCALE_ELEMENT="${SCALER} ! video/x-raw,width=${SCALE_WIDTH},height=${SCALE_HEIGHT} ! "
fi

# Add optional properties only when provided (e.g. NPU tuning, continuous batching).
PIPELINE_CONFIG_PROP=""
if [[ -n "$PIPELINE_CONFIG" ]]; then
    PIPELINE_CONFIG_PROP="pipeline-config=\"$PIPELINE_CONFIG\""
fi

SCHEDULER_CONFIG_PROP=""
if [[ -n "$SCHEDULER_CONFIG" ]]; then
    SCHEDULER_CONFIG_PROP="scheduler-config=\"$SCHEDULER_CONFIG\""
fi

# Optional detection stage: only added when --trigger-classes selects frames by object class.
# gvadetect attaches per-frame object metadata (GstAnalyticsODMtd); gvagenai reads it and
# forwards matching frames to the VLM backend regardless of frame-rate sampling.
DETECT_ELEMENT=""
TRIGGER_CLASSES_PROP=""
TRIGGER_MODE_PROP=""
TRIGGER_MIN_CONFIDENCE_PROP=""
if [[ -n "$TRIGGER_CLASSES" ]]; then
    DETECT_ELEMENT="gvadetect model=$DETECTION_MODEL device=$DEVICE threshold=$DETECT_THRESHOLD ! queue ! "
    TRIGGER_CLASSES_PROP="trigger-classes=\"$TRIGGER_CLASSES\""
    TRIGGER_MODE_PROP="trigger-mode=$TRIGGER_MODE"
    TRIGGER_MIN_CONFIDENCE_PROP="trigger-min-confidence=$TRIGGER_MIN_CONFIDENCE"
fi

PIPELINE="gst-launch-1.0 \
    $SOURCE_ELEMENT ! \
    decodebin3 ! \
    ${SCALE_ELEMENT}\
    ${DETECT_ELEMENT}\
    gvagenai \
        backend=$BACKEND \
        $GENAI_MODEL_PROP \
        prompt=\"$PROMPT\" \
        generation-config=\"$GENERATION_CONFIG\" \
        frame-rate=$FRAME_RATE \
        chunk-size=$CHUNK_SIZE \
        vision-mode=$VISION_MODE \
        $PIPELINE_CONFIG_PROP \
        $SCHEDULER_CONFIG_PROP \
        $TRIGGER_CLASSES_PROP \
        $TRIGGER_MODE_PROP \
        $TRIGGER_MIN_CONFIDENCE_PROP \
        metrics=$METRICS ! \
    gvametapublish file-path=$OUTPUT_FILE ! \
    fakesink async=false"

echo ""
echo "Running gvagenai inference pipeline..."
echo "Pipeline: $PIPELINE"
echo ""

eval "$PIPELINE"

echo ""
echo "Pipeline execution completed."
echo "Results saved to: $OUTPUT_FILE"
