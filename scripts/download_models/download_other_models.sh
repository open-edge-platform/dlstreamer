#!/bin/bash
# ==============================================================================
# Copyright (C) 2021-2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================

MODEL=${1:-"all"} # Supported values listed in SUPPORTED_MODELS below. Type one model,list of models separated by coma or 'all' to download all models.
QUANTIZE=${2:-""} # Supported values listed in SUPPORTED_QUANTIZATION_DATASETS below.

# Save the directory where the script was launched from
LAUNCH_DIR="$PWD"

. /etc/os-release

# Changing the config dir for the duration of the script to prevent potential conflics with
# previous installations of ultralytics' tools. Quantization datasets could install
# incorrectly without this.
DOWNLOAD_CONFIG_DIR=$(mktemp -d /tmp/tmp.XXXXXXXXXXXXXXXXXXXXXXXXXXX)
QUANTIZE_CONFIG_DIR=$(mktemp -d /tmp/tmp.XXXXXXXXXXXXXXXXXXXXXXXXXXX)
YOLO_CONFIG_DIR=$DOWNLOAD_CONFIG_DIR

SUPPORTED_MODELS=(
  "all"
  "yolox-tiny"
  "yolox_s"
  "yolov7"
  "yolov8_license_plate_detector"
  "ch_PP-OCRv4_rec_infer"
  "centerface"
  "hsemotion"
  "deeplabv3"
  "mars-small128"
)

# Corresponds to files in 'datasets' directory
declare -A SUPPORTED_QUANTIZATION_DATASETS
SUPPORTED_QUANTIZATION_DATASETS=(
  ["coco"]="https://raw.githubusercontent.com/ultralytics/ultralytics/v8.4.0/ultralytics/cfg/datasets/coco.yaml"
  ["coco128"]="https://raw.githubusercontent.com/ultralytics/ultralytics/v8.4.0/ultralytics/cfg/datasets/coco128.yaml"
  ["coco8"]="https://raw.githubusercontent.com/ultralytics/ultralytics/v8.4.0/ultralytics/cfg/datasets/coco8.yaml"
)

# Function to display text in a given color
echo_color() {
    local text="$1"
    local color="$2"
    local color_code=""

    # Determine the color code based on the color name
    case "$color" in
        black) color_code="\e[30m" ;;
        red) color_code="\e[31m" ;;
        green) color_code="\e[32m" ;;
        bred) color_code="\e[91m" ;;
        bgreen) color_code="\e[92m" ;;
        yellow) color_code="\e[33m" ;;
        blue) color_code="\e[34m" ;;
        magenta) color_code="\e[35m" ;;
        cyan) color_code="\e[36m" ;;
        white) color_code="\e[37m" ;;
        *) echo "Invalid color name"; return 1 ;;
    esac

    # Display the text in the chosen color
    echo -e "${color_code}${text}\e[0m"
}

# Function to handle errors
handle_error() {
    echo -e "\e[31mError occurred: $1\e[0m"
    exit 1
}

# Function to display header in logs
display_header() {
    local text="$1"
    echo ""
    echo_color "═══════════════════════════════════════════════════════════════" "cyan"
    echo_color "  $text" "bgreen"
    echo_color "═══════════════════════════════════════════════════════════════" "cyan"
    echo ""
}

# Function to display help message
show_help() {
    cat << EOF
$(echo_color "Usage:" "cyan")
  $0 [MODEL] [QUANTIZE]

$(echo_color "Arguments:" "cyan")
  MODEL      Model name(s) to download. Can be:
             - Single model: yolox_s
             - Multiple models (comma-separated): yolox_s,yolov7,centerface
             - Special keyword: 'all' (all supported models)
             - Default: 'all'

  QUANTIZE   Optional. Quantization dataset for INT8 models.
             Supported values: coco, coco128, coco8
             Leave empty to skip quantization.

$(echo_color "Environment:" "cyan")
  MODELS_PATH    Required. Path where models will be downloaded.
                 Example: export MODELS_PATH=/path/to/models

$(echo_color "Examples:" "cyan")
  # Download all models
  export MODELS_PATH=~/models
  $0 all

  # Download specific models
  export MODELS_PATH=~/models
  $0 yolox_s,yolov7

  # Download multiple models with quantization
  export MODELS_PATH=~/models
  $0 yolox_s,yolov7 coco128

  # Download with quantization (single model)
  export MODELS_PATH=~/models
  $0 yolox_s coco128

$(echo_color "Supported Models:" "cyan")

EOF

    echo_color "  YOLO Models:" "yellow"
    printf "    "
    local count=0
    for model in "${SUPPORTED_MODELS[@]}"; do
        # Match YOLO variants
        if [[ $model =~ ^yolo ]]; then
            printf "%-30s" "$model"
            ((count++))
            if ((count % 3 == 0)); then
                printf "\n    "
            fi
        fi
    done
    echo -e "\n"

    echo_color "  Computer Vision Models:" "yellow"
    printf "    "
    count=0
    for model in "${SUPPORTED_MODELS[@]}"; do
        # Exclude YOLO models and special keywords
        if [[ ! $model =~ ^yolo && $model != "all" ]]; then
            printf "%-30s" "$model"
            ((count++))
            if ((count % 3 == 0)); then
                printf "\n    "
            fi
        fi
    done
    echo -e "\n"

    echo_color "  Special Keywords:" "yellow"
    printf "    %-30s - Download all available models\n" "all"
    echo ""
}

# Check for help argument
if [[ "${MODEL}" == "-h" || "${MODEL}" == "--help" ]]; then
    show_help
    exit 0
fi

# Validate QUANTIZE parameter early (fail-fast)
if [[ -n "$QUANTIZE" ]] && ! [[ "${!SUPPORTED_QUANTIZATION_DATASETS[*]}" =~ $QUANTIZE ]]; then
  echo "Unsupported quantization dataset: $QUANTIZE" >&2
  echo "Supported datasets: ${!SUPPORTED_QUANTIZATION_DATASETS[*]}" >&2
  exit 1
fi

# Function to validate models
validate_models() {
    local models_input="$1"
    local models_array
    # Split input by comma into array
    IFS=',' read -ra models_array <<< "$models_input"
    # Validate each model
    for model in "${models_array[@]}"; do
        model=$(echo "$model" | xargs)  # Trim whitespace

        # Check for exact match in supported models array
        local found=false
        for supported_model in "${SUPPORTED_MODELS[@]}"; do
            if [[ "$model" == "$supported_model" ]]; then
                found=true
                break
            fi
        done

        if [[ "$found" == false ]]; then
            echo_color "Error: Unsupported model '$model'" "red"
            echo ""
            show_help
            exit 1
        fi
    done
}

prepare_models_list() {
    local models_input="$1"

  # Expand 'all' into the explicit list of supported models except the keyword itself.
  if [[ "$models_input" == "all" ]]; then
    for model in "${SUPPORTED_MODELS[@]}"; do
      [[ "$model" != "all" ]] && echo "$model"
    done
    return
  fi

    local models_array
    # Split input by comma into array
    IFS=',' read -ra models_array <<< "$models_input"
    # Return models (newline-separated for mapfile)
    printf '%s\n' "${models_array[@]}"
}

# Function to check if array contains element
array_contains() {
    local element="$1"
    shift
    local array=("$@")
    for item in "${array[@]}"; do
        if [[ "$item" == "$element" ]]; then
            return 0
        fi
    done
    return 1
}

# Function to cleanup temporary directories and virtual environment
cleanup_temp_dirs() {
    if [ -n "${DOWNLOAD_CONFIG_DIR:-}" ] && [ -d "$DOWNLOAD_CONFIG_DIR" ]; then
        echo "Cleaning up temporary directory: $DOWNLOAD_CONFIG_DIR"
        rm -rf "$DOWNLOAD_CONFIG_DIR" 2>/dev/null || true
    fi
    if [ -n "${QUANTIZE_CONFIG_DIR:-}" ] && [ -d "$QUANTIZE_CONFIG_DIR" ]; then
        echo "Cleaning up temporary directory: $QUANTIZE_CONFIG_DIR"
        rm -rf "$QUANTIZE_CONFIG_DIR" 2>/dev/null || true
    fi
    if [ -n "${VENV_DIR:-}" ] && [ -d "$VENV_DIR" ]; then
        echo "Cleaning up virtual environment: $VENV_DIR"
        deactivate 2>/dev/null || true
        rm -rf "$VENV_DIR" 2>/dev/null || true
    fi
}

# Setup cleanup on script exit and interruption
trap cleanup_temp_dirs EXIT
trap 'echo "Script interrupted by user"; exit 130' INT TERM

# Trap errors and call handle_error
trap 'handle_error "- line $LINENO"' ERR

# Validate models before processing
validate_models "$MODEL"

# Prepare models list
mapfile -t MODELS_TO_PROCESS < <(prepare_models_list "$MODEL")
echo "Models to process: ${MODELS_TO_PROCESS[*]}"

set +u  # Disable nounset option: treat any unset variable as an empty string
if [ -z "$MODELS_PATH" ]; then
  echo_color "MODELS_PATH is not specified" "bred"
  echo_color "Please set MODELS_PATH env variable with target path to download models" "red"
  exit 1
fi

if [ ! -e "$MODELS_PATH" ]; then
    mkdir -p "$MODELS_PATH" || handle_error $LINENO
fi


set -u  # Re-enable nounset option: treat any attempt to use an unset variable as an error

if [ "$ID" == "fedora" ]; then
  export PYTHON_CREATE_VENV=/usr/bin/python3.10
  $PYTHON_CREATE_VENV -m ensurepip --upgrade || handle_error $LINENO
else
  export PYTHON_CREATE_VENV=python3
fi

# Set the name of the virtual environment directory (single venv for all operations)
VENV_DIR="$HOME/.virtualenvs/dlstreamer"

# Create a Python virtual environment if it doesn't exist
if [ ! -d "$VENV_DIR" ]; then
  echo "Creating virtual environment in $VENV_DIR..."
  $PYTHON_CREATE_VENV -m venv "$VENV_DIR" || handle_error $LINENO
fi

# Activate the virtual environment
echo "Activating virtual environment in $VENV_DIR..."
source "$VENV_DIR/bin/activate"

# Install all required packages for main virtual environment
pip install --no-cache-dir --upgrade pip      || handle_error $LINENO
pip install --no-cache-dir numpy==2.2.6       || handle_error $LINENO
pip install --no-cache-dir openvino==2026.2.0 || handle_error $LINENO
pip install --no-cache-dir onnx==1.21.0       || handle_error $LINENO
pip install --no-cache-dir onnxscript==0.5.7  || handle_error $LINENO
pip install --no-cache-dir seaborn==0.13.2    || handle_error $LINENO
pip install --no-cache-dir opencv-python-headless==4.12.0.88 || handle_error $LINENO
pip install --no-cache-dir nncf==2.19.0       || handle_error $LINENO
pip install --no-cache-dir tqdm==4.67.1       || handle_error $LINENO
pip install --no-cache-dir requests==2.32.5   || handle_error $LINENO
pip install --no-cache-dir pyyaml==6.0.3   || handle_error $LINENO

# Install PyTorch CPU version
pip install --no-cache-dir --upgrade --extra-index-url https://download.pytorch.org/whl/cpu torch==2.8.0 torchaudio==2.8.0 torchvision==0.23.0 || handle_error $LINENO

echo Downloading models to folder "$MODELS_PATH".
set -euo pipefail


# ================================= YOLOx-TINY FP16 & FP32 =================================
if array_contains "yolox-tiny" "${MODELS_TO_PROCESS[@]}"; then
  display_header "Downloading YOLOx-TINY model"
  MODEL_NAME="yolox-tiny"
  model_status="ok"
  MODEL_DIR="$MODELS_PATH/public/$MODEL_NAME"
  DST_FILE1="$MODEL_DIR/FP16/$MODEL_NAME.xml"
  DST_FILE2="$MODEL_DIR/FP32/$MODEL_NAME.xml"

  if [[ ! -f "$DST_FILE1" || ! -f "$DST_FILE2" ]]; then
    cd "$MODELS_PATH"
    echo "Downloading and converting: ${MODEL_DIR}"

    # Create temporary new Python virtual environment for omz tools
    deactivate 2>/dev/null || true
    $PYTHON_CREATE_VENV -m venv "$HOME/.virtualenvs/dlstreamer_openvino_dev" || handle_error $LINENO
    source "$HOME/.virtualenvs/dlstreamer_openvino_dev/bin/activate"
    python -m pip install --upgrade pip                 || handle_error $LINENO
    pip install --no-cache-dir "openvino-dev==2024.6.0" || handle_error $LINENO
    pip install --no-cache-dir --upgrade --extra-index-url https://download.pytorch.org/whl/cpu torch==2.8.0 torchaudio==2.8.0 torchvision==0.23.0 || handle_error $LINENO
    pip install --no-cache-dir onnxscript==0.5.7        || handle_error $LINENO

    omz_downloader --name "$MODEL_NAME"
    omz_converter --name "$MODEL_NAME"
    cd "$MODEL_DIR"

    # Clean up temporary files created by omz_converter
    find . -maxdepth 1 -type f -name 'yolox*' -delete 2>/dev/null || true
    find . -maxdepth 1 -type d -name 'yolox*' -exec rm -rf {} + 2>/dev/null || true
    rm -rf models utils

    # Cleanup temporary virtual environment
    deactivate 2>/dev/null || true
    rm -rf "$HOME/.virtualenvs/dlstreamer_openvino_dev" 2>/dev/null || true
    source "$VENV_DIR/bin/activate" 
  else
    model_status="cached"
    echo_color "\nModel already exists: $MODEL_DIR.\n" "yellow"
  fi
fi

# ================================= YOLOx-S FP16 & FP32 =================================
if array_contains "yolox_s" "${MODELS_TO_PROCESS[@]}"; then
  display_header "Downloading YOLOx-S model"
  MODEL_NAME="yolox_s"
  model_status="ok"
  MODEL_DIR="$MODELS_PATH/public/$MODEL_NAME"
  DST_FILE1="$MODEL_DIR/FP16/$MODEL_NAME.xml"
  DST_FILE2="$MODEL_DIR/FP32/$MODEL_NAME.xml"

  if [[ ! -f "$DST_FILE1" || ! -f "$DST_FILE2" ]]; then
    mkdir -p "$MODEL_DIR"
    mkdir -p "$MODEL_DIR/FP16"
    mkdir -p "$MODEL_DIR/FP32"
    cd "$MODEL_DIR"
    curl -O -L https://github.com/Megvii-BaseDetection/YOLOX/releases/download/0.1.1rc0/yolox_s.onnx
    ovc yolox_s.onnx --compress_to_fp16=True
    mv yolox_s.xml "$MODEL_DIR/FP16"
    mv yolox_s.bin "$MODEL_DIR/FP16"
    ovc yolox_s.onnx --compress_to_fp16=False
    mv yolox_s.xml "$MODEL_DIR/FP32"
    mv yolox_s.bin "$MODEL_DIR/FP32"
    rm -f yolox_s.onnx
  else
    model_status="cached"
    echo_color "\nModel already exists: $MODEL_DIR.\n" "yellow"
  fi
fi

# ================================= YOLOv7* FP16 & FP32 =================================
if array_contains "yolov7" "${MODELS_TO_PROCESS[@]}"; then
  display_header "Downloading YOLOv7 model"
  MODEL_NAME="yolov7"
  model_status="ok"
  MODEL_DIR="$MODELS_PATH/public/$MODEL_NAME"
  DST_FILE1="$MODEL_DIR/FP16/$MODEL_NAME.xml"
  DST_FILE2="$MODEL_DIR/FP32/$MODEL_NAME.xml"

  if [[ ! -f "$DST_FILE1" || ! -f "$DST_FILE2" ]]; then
    mkdir -p "$MODEL_DIR"
    mkdir -p "$MODEL_DIR/FP16"
    mkdir -p "$MODEL_DIR/FP32"
    cd "$MODEL_DIR"
    echo "Downloading and converting: ${MODEL_DIR}"
    git clone https://github.com/WongKinYiu/yolov7.git
    cd yolov7
    
    # Patch for PyTorch 2.6+ compatibility (weights_only parameter)
    sed -i 's/torch\.load(w, map_location=map_location)/torch.load(w, map_location=map_location, weights_only=False)/g' models/experimental.py
    
    python3 export.py --weights  yolov7.pt  --grid --dynamic-batch
    ovc yolov7.onnx --compress_to_fp16=True
    mv yolov7.xml "$MODEL_DIR/FP16"
    mv yolov7.bin "$MODEL_DIR/FP16"
    ovc yolov7.onnx --compress_to_fp16=False
    mv yolov7.xml "$MODEL_DIR/FP32"
    mv yolov7.bin "$MODEL_DIR/FP32"
    cd ..
    rm -rf yolov7
  else
    model_status="cached"
    echo_color "\nModel already exists: $MODEL_DIR.\n" "yellow"
  fi
fi

# ================================= YOLOv8 License Plate Detector FP32 - Edge AI Resources =================================
if array_contains "yolov8_license_plate_detector" "${MODELS_TO_PROCESS[@]}"; then
  display_header "Downloading YOLOv8 License Plate Detector model"
  MODEL_NAME="yolov8_license_plate_detector"
  model_status="ok"
  MODEL_DIR="$MODELS_PATH/public/$MODEL_NAME"
  DST_FILE1="$MODEL_DIR/FP32/$MODEL_NAME.xml"

  if [[ ! -f "$DST_FILE1" ]]; then
    echo "Downloading and converting: ${MODEL_DIR}"
    mkdir -p "$MODEL_DIR"
    cd "$MODEL_DIR"

    curl -L -k -o "${MODEL_NAME}.zip" 'https://github.com/open-edge-platform/edge-ai-resources/raw/main/models/license-plate-reader.zip'
    python3 -c "
import zipfile
import os
with zipfile.ZipFile('${MODEL_NAME}.zip', 'r') as zip_ref:
    zip_ref.extractall('.')
os.remove('${MODEL_NAME}.zip')
"

    mkdir -p FP32
    cp license-plate-reader/models/yolov8n/yolov8n_retrained.bin FP32/${MODEL_NAME}.bin
    cp license-plate-reader/models/yolov8n/yolov8n_retrained.xml FP32/${MODEL_NAME}.xml
    chmod -R u+w license-plate-reader
    rm -rf license-plate-reader
    cd ..
  else
    model_status="cached"
    echo_color "\nModel already exists: $MODEL_DIR.\n" "yellow"
  fi
fi

# ================================= CenterFace FP16 & FP32 =================================
if array_contains "centerface" "${MODELS_TO_PROCESS[@]}"; then
  display_header "Downloading CenterFace model"
  MODEL_NAME="centerface"
  model_status="ok"
  MODEL_DIR="$MODELS_PATH/public/$MODEL_NAME"
  DST_FILE1="$MODEL_DIR/FP16/$MODEL_NAME.xml"
  DST_FILE2="$MODEL_DIR/FP32/$MODEL_NAME.xml"

  if [[ ! -f "$DST_FILE1" || ! -f "$DST_FILE2" ]]; then
    echo "Downloading and converting: ${MODEL_DIR}"
    mkdir -p "$MODEL_DIR"
    cd "$MODEL_DIR"
    git clone https://github.com/Star-Clouds/CenterFace.git
    cd CenterFace/models/onnx
    ovc centerface.onnx --input "[1,3,768,1280]"
    mv centerface.xml "$MODEL_DIR"
    mv centerface.bin "$MODEL_DIR"
    cd ../../..
    rm -rf CenterFace
    python3 - <<EOF
import openvino
import sys, os

core = openvino.Core()
ov_model = core.read_model(model='centerface.xml')

ov_model.output(0).set_names({"heatmap"})
ov_model.output(1).set_names({"scale"})
ov_model.output(2).set_names({"offset"})
ov_model.output(3).set_names({"landmarks"})

ov_model.set_rt_info("centerface", ['model_info', 'model_type'])
ov_model.set_rt_info("0.55", ['model_info', 'confidence_threshold'])
ov_model.set_rt_info("0.5", ['model_info', 'iou_threshold'])

print(ov_model)

openvino.save_model(ov_model, './FP32/' + 'centerface.xml', compress_to_fp16=False)
openvino.save_model(ov_model, './FP16/' + 'centerface.xml', compress_to_fp16=True)
os.remove('centerface.xml')
os.remove('centerface.bin')
EOF
  else
    model_status="cached"
    echo_color "\nModel already exists: $MODEL_DIR.\n" "yellow"
  fi
fi

# ================================= HSEmotion FP16 =================================
if array_contains "hsemotion" "${MODELS_TO_PROCESS[@]}"; then
  display_header "Downloading HSEmotion model"
  MODEL_NAME="hsemotion"
  model_status="ok"
  MODEL_DIR="$MODELS_PATH/public/$MODEL_NAME"
  DST_FILE="$MODEL_DIR/FP16/$MODEL_NAME.xml"

  if [[ ! -f "$DST_FILE" ]]; then
    echo "Downloading and converting: ${MODEL_DIR}"
    mkdir -p "$MODEL_DIR"
    cd "$MODEL_DIR"
    git clone https://github.com/av-savchenko/face-emotion-recognition.git
    cd face-emotion-recognition/models/affectnet_emotions/onnx

    ovc enet_b0_8_va_mtl.onnx --input "[16,3,224,224]"
    mv enet_b0_8_va_mtl.xml "$MODEL_DIR/$MODEL_NAME.xml"
    mv enet_b0_8_va_mtl.bin "$MODEL_DIR/$MODEL_NAME.bin"
    cd ../../../..
    rm -rf face-emotion-recognition

    python3 - <<EOF
import openvino
import os

core = openvino.Core()
ov_model = core.read_model(model='hsemotion.xml')

ov_model.set_rt_info("anger contempt disgust fear happiness neutral sadness surprise", ['model_info', 'labels'])
ov_model.set_rt_info("label", ['model_info', 'model_type'])
ov_model.set_rt_info("True", ['model_info', 'output_raw_scores'])
ov_model.set_rt_info("fit_to_window_letterbox", ['model_info', 'resize_type'])
ov_model.set_rt_info("255", ['model_info', 'scale_values'])

print(ov_model)

openvino.save_model(ov_model, './FP16/hsemotion.xml')
os.remove('hsemotion.xml')
os.remove('hsemotion.bin')
EOF
  else
    model_status="cached"
    echo_color "\nModel already exists: $MODEL_DIR.\n" "yellow"
  fi
fi

# ================================= DeepLabv3 FP16 & FP32 =================================
if array_contains "deeplabv3" "${MODELS_TO_PROCESS[@]}"; then
  display_header "Downloading DeepLabv3 model"
  MODEL_NAME="deeplabv3"
  model_status="ok"
  MODEL_DIR="$MODELS_PATH/public/$MODEL_NAME"
  DST_FILE1="$MODEL_DIR/FP32/$MODEL_NAME.xml"
  DST_FILE2="$MODEL_DIR/FP16/$MODEL_NAME.xml"

  if [[ ! -f "$DST_FILE1" || ! -f "$DST_FILE2" ]]; then
    cd "$MODELS_PATH"
    echo "Downloading and converting: ${MODEL_DIR}"

    # Create temporary new Python virtual environment for omz tools
    deactivate 2>/dev/null || true
    $PYTHON_CREATE_VENV -m venv "$HOME/.virtualenvs/dlstreamer_openvino_dev" || handle_error $LINENO
    source "$HOME/.virtualenvs/dlstreamer_openvino_dev/bin/activate"
    python -m pip install --upgrade pip                 || handle_error $LINENO
    pip install --no-cache-dir "openvino-dev==2024.6.0" || handle_error $LINENO
    pip install --no-cache-dir tensorflow==2.20.0       || handle_error $LINENO

    omz_downloader --name "$MODEL_NAME"
    omz_converter --name "$MODEL_NAME"
    cd "$MODEL_DIR"
    python3 - <<EOF "$DST_FILE1"
import openvino
import sys, os, shutil

orig_model_path = sys.argv[1]

core = openvino.Core()
ov_model = core.read_model(model=orig_model_path)
ov_model.set_rt_info("semantic_mask", ['model_info', 'model_type'])

print(ov_model)

shutil.rmtree('deeplabv3_mnv2_pascal_train_aug')
shutil.rmtree('FP32')
shutil.rmtree('FP16')
openvino.save_model(ov_model, './FP32/' + 'deeplabv3.xml', compress_to_fp16=False)
openvino.save_model(ov_model, './FP16/' + 'deeplabv3.xml', compress_to_fp16=True)
EOF

    # Cleanup temporary virtual environment
    deactivate 2>/dev/null || true
    rm -rf "$HOME/.virtualenvs/dlstreamer_openvino_dev" 2>/dev/null || true
    source "$VENV_DIR/bin/activate" 
  else
    model_status="cached"
    echo_color "\nModel already exists: $MODEL_DIR.\n" "yellow"
  fi
fi

# ================================= ch_PP-OCRv4_rec_infer FP32 =================================
if array_contains "ch_PP-OCRv4_rec_infer" "${MODELS_TO_PROCESS[@]}"; then
  display_header "Downloading PaddlePaddle OCRv4 model"
  MODEL_NAME="ch_PP-OCRv4_rec_infer"
  model_status="ok"
  MODEL_DIR="$MODELS_PATH/public/$MODEL_NAME"
  DST_FILE1="$MODEL_DIR/FP32/$MODEL_NAME.xml"

  if [[ ! -f "$DST_FILE1" ]]; then
    echo "Downloading and converting: ${MODEL_DIR}"
    mkdir -p "$MODEL_DIR"
    cd "$MODEL_DIR"

    curl -L -k -o "${MODEL_NAME}.zip" 'https://github.com/open-edge-platform/edge-ai-resources/raw/main/models/license-plate-reader.zip'
    python3 -c "
import zipfile
import os
with zipfile.ZipFile('${MODEL_NAME}.zip', 'r') as zip_ref:
    zip_ref.extractall('.')
os.remove('${MODEL_NAME}.zip')
"

    mkdir -p FP32
    cp license-plate-reader/models/ch_PP-OCRv4_rec_infer/ch_PP-OCRv4_rec_infer.bin FP32/${MODEL_NAME}.bin
    cp license-plate-reader/models/ch_PP-OCRv4_rec_infer/ch_PP-OCRv4_rec_infer.xml FP32/${MODEL_NAME}.xml
    chmod -R u+w license-plate-reader
    rm -rf license-plate-reader
    cd ..
  else
    model_status="cached"
    echo_color "\nModel already exists: $MODEL_DIR.\n" "yellow"
  fi
fi

# ================================= Mars-Small128 FP32 & INT8 =================================
if array_contains "mars-small128" "${MODELS_TO_PROCESS[@]}"; then
  display_header "Downloading Mars-Small128 model"
  MODEL_NAME="mars-small128"
  model_status="ok"
  MODEL_DIR="$MODELS_PATH/public/$MODEL_NAME"
  DST_FILE1="$MODEL_DIR/FP32/mars_small128_fp32.xml"
  DST_FILE2="$MODEL_DIR/INT8/mars_small128_int8.xml"

  if [[ ! -f "$DST_FILE1" || ! -f "$DST_FILE2" ]]; then
    echo "Downloading and converting: ${MODEL_DIR}"
    mkdir -p "$MODEL_DIR"

    SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
    REPO_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
    python3 "$REPO_ROOT/samples/models/convert_mars_deepsort.py" --output-dir "$MODEL_DIR" --precision both || handle_error $LINENO

    mkdir -p "$MODEL_DIR/FP32" "$MODEL_DIR/INT8"
    mv "$MODEL_DIR/mars_small128_fp32.xml" "$MODEL_DIR/FP32/mars_small128_fp32.xml"
    mv "$MODEL_DIR/mars_small128_fp32.bin" "$MODEL_DIR/FP32/mars_small128_fp32.bin"
    mv "$MODEL_DIR/mars_small128_int8.xml" "$MODEL_DIR/INT8/mars_small128_int8.xml"
    mv "$MODEL_DIR/mars_small128_int8.bin" "$MODEL_DIR/INT8/mars_small128_int8.bin"
  else
    model_status="cached"
    echo_color "\nModel already exists: $MODEL_DIR.\n" "yellow"
  fi
fi


