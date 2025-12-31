#!/bin/bash
# ==============================================================================
# Copyright (C) 2021-2025 Intel Corporation
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
  "yolo_all"
  "yolox-tiny"
  "yolox_s"
  "yolov5n"
  "yolov5s"
  "yolov5m"
  "yolov5l"
  "yolov5x"
  "yolov5n6"
  "yolov5s6"
  "yolov5m6"
  "yolov5l6"
  "yolov5x6"
  "yolov5nu"
  "yolov5su"
  "yolov5mu"
  "yolov5lu"
  "yolov5xu"
  "yolov5n6u"
  "yolov5s6u"
  "yolov5m6u"
  "yolov5l6u"
  "yolov5x6u"
  "yolov7"
  "yolov8n"
  "yolov8s"
  "yolov8m"
  "yolov8l"
  "yolov8x"
  "yolov8n-obb"
  "yolov8s-obb"
  "yolov8m-obb"
  "yolov8l-obb"
  "yolov8x-obb"
  "yolov8n-seg"
  "yolov8s-seg"
  "yolov8m-seg"
  "yolov8l-seg"
  "yolov8x-seg"
  "yolov8n-pose"
  "yolov8s-pose"
  "yolov8m-pose"
  "yolov8l-pose"
  "yolov8x-pose"
  "yolov8_license_plate_detector"
  "yolov9t"
  "yolov9s"
  "yolov9m"
  "yolov9c"
  "yolov9e"
  "yolov10n"
  "yolov10s"
  "yolov10m"
  "yolov10b"
  "yolov10l"
  "yolov10x"
  "yolo11n"
  "yolo11s"
  "yolo11m"
  "yolo11l"
  "yolo11x"
  "yolo11n-obb"
  "yolo11s-obb"
  "yolo11m-obb"
  "yolo11l-obb"
  "yolo11x-obb"
  "yolo11n-seg"
  "yolo11s-seg"
  "yolo11m-seg"
  "yolo11l-seg"
  "yolo11x-seg"
  "yolo11n-pose"
  "yolo11s-pose"
  "yolo11m-pose"
  "yolo11l-pose"
  "yolo11x-pose"
  "centerface"
  "hsemotion"
  "deeplabv3"
  "clip-vit-large-patch14"
  "clip-vit-base-patch16"
  "clip-vit-base-patch32"
  "ch_PP-OCRv4_rec_infer" # PaddlePaddle OCRv4 multilingual model
  "mars-small128" # DeepSORT person re-identification model (uses convert_mars_deepsort.py)
)

# Corresponds to files in 'datasets' directory
declare -A SUPPORTED_QUANTIZATION_DATASETS
SUPPORTED_QUANTIZATION_DATASETS=(
  ["coco"]="https://raw.githubusercontent.com/ultralytics/ultralytics/v8.1.0/ultralytics/cfg/datasets/coco.yaml"
  ["coco128"]="https://raw.githubusercontent.com/ultralytics/ultralytics/v8.1.0/ultralytics/cfg/datasets/coco128.yaml"
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

prepare_models_list() {
    local models_input="$1"
    local models_array
    # Split input by comma into array
    IFS=',' read -ra models_array <<< "$models_input"
    # Validate each model
    for model in "${models_array[@]}"; do
        model=$(echo "$model" | xargs)  # Trim whitespace

        if ! [[ " ${SUPPORTED_MODELS[*]} " =~ " $model " ]]; then
            echo "Unsupported model: $model" >&2
            exit 1
        fi
    done
    # Return models (space-separated)
    echo "${models_array[@]}"
}

# Trap errors and call handle_error
trap 'handle_error "- line $LINENO"' ERR

# Prepare models list
MODELS_TO_PROCESS=($(prepare_models_list "$MODEL"))
echo "Models to process: ${MODELS_TO_PROCESS[@]}"

if ! [[ "${!SUPPORTED_QUANTIZATION_DATASETS[*]}" =~ $QUANTIZE ]]; then
  echo "Unsupported quantization dataset: $QUANTIZE" >&2
  exit 1
fi

set +u  # Disable nounset option: treat any unset variable as an empty string
if [ -z "$MODELS_PATH" ]; then
  echo "MODELS_PATH is not specified"
  echo "Please set MODELS_PATH env variable with target path to download models"
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

# Set the name of the virtual environment directory
VENV_DIR_QUANT="$HOME/.virtualenvs/dlstreamer-quantization"

# Create a Python virtual environment if it doesn't exist
if [ ! -d "$VENV_DIR_QUANT" ]; then
  echo "Creating virtual environment in $VENV_DIR_QUANT..."
  $PYTHON_CREATE_VENV -m venv "$VENV_DIR_QUANT" || handle_error $VENV_DIR_QUANT
fi

# Activate the virtual environment
echo "Activating virtual environment in $VENV_DIR_QUANT..."
source "$VENV_DIR_QUANT/bin/activate"

# Upgrade pip in the virtual environment
pip install --no-cache-dir --upgrade pip

# Install OpenVINO module with compatible numpy version
pip install --no-cache-dir "numpy<2.5.0,>=1.16.6" || handle_error $LINENO
pip install --no-cache-dir openvino==2025.4.0 || handle_error $LINENO

pip install --no-cache-dir onnx || handle_error $LINENO
pip install --no-cache-dir seaborn || handle_error $LINENO
# Install compatible NNCF version for OpenVINO 2025.4.0
pip install --no-cache-dir "nncf>=2.14.0,<3.0.0" || handle_error $LINENO

# Check and upgrade ultralytics if necessary
if [[ "${MODEL:-}" =~ yolo.* || "${MODEL:-}" == "all" ]]; then
  pip install --no-cache-dir --upgrade --extra-index-url https://download.pytorch.org/whl/cpu "ultralytics==8.3.153" "numpy<2.5.0" || handle_error $LINENO
fi

# Set the name of the virtual environment directory
VENV_DIR="$HOME/.virtualenvs/dlstreamer"

# Create a Python virtual environment if it doesn't exist
if [ ! -d "$VENV_DIR" ]; then
  echo "Creating virtual environment in $VENV_DIR..."
  $PYTHON_CREATE_VENV -m venv "$VENV_DIR" || handle_error $LINENO
fi

# Activate the virtual environment
echo "Activating virtual environment in $VENV_DIR..."
source "$VENV_DIR/bin/activate"

# Upgrade pip in the virtual environment
pip install --no-cache-dir --upgrade pip

# Install OpenVINO module with compatible numpy version
pip install --no-cache-dir "numpy<2.0.0,>=1.16.6" || handle_error $LINENO
pip install --no-cache-dir openvino==2024.6.0 || handle_error $LINENO
pip install --no-cache-dir openvino-dev==2024.6.0 || handle_error $LINENO

pip install --no-cache-dir onnx || handle_error $LINENO
pip install --no-cache-dir seaborn || handle_error $LINENO
# Install compatible NNCF version for OpenVINO 2024.6.0
pip install --no-cache-dir "nncf>=2.12.0,<2.14.0" || handle_error $LINENO

# Check and upgrade ultralytics if necessary
if [[ "${MODEL:-}" =~ yolo.* || "${MODEL:-}" == "all" ]]; then
  pip install --no-cache-dir --upgrade --extra-index-url https://download.pytorch.org/whl/cpu "ultralytics==8.3.153" "numpy<2.0.0" || handle_error $LINENO
fi

# Install dependencies for CLIP models
if [[ "${MODEL:-}" =~ clip.* || "${MODEL:-}" == "all" ]]; then
  pip install --no-cache-dir --upgrade torch==2.8.0 torchaudio==2.8.0 torchvision==0.23.0 || handle_error $LINENO
  pip install --no-cache-dir transformers || handle_error $LINENO
  pip install --no-cache-dir pillow || handle_error $LINENO
fi

echo Downloading models to folder "$MODELS_PATH".

set -euo pipefail
# -------------- YOLOx


# Function for quantization of YOLO models
quantize_yolo_model() {
  local MODEL_NAME=$1
  MODEL_DIR="$MODELS_PATH/public/$MODEL_NAME"
  DST_FILE="$MODEL_DIR/INT8/$MODEL_NAME.xml"


  if [[ ! -f "$DST_FILE" ]]; then
    YOLO_CONFIG_DIR=$QUANTIZE_CONFIG_DIR
    export YOLO_CONFIG_DIR

    mkdir -p "$MODELS_PATH/datasets"
    local DATASET_MANIFEST="$MODELS_PATH/datasets/$QUANTIZE.yaml"

    curl -L -o "$DATASET_MANIFEST" ${SUPPORTED_QUANTIZATION_DATASETS[$QUANTIZE]}
    echo "Quantizing: ${MODEL_DIR}"
    mkdir -p "$MODEL_DIR"

    source "$VENV_DIR_QUANT/bin/activate"
    cd "$MODELS_PATH"
    python3 - <<EOF "$MODEL_NAME" "$DATASET_MANIFEST"
import openvino as ov
import nncf
import torch
import sys
from rich.progress import track
from ultralytics.cfg import get_cfg
from ultralytics.models.yolo.detect import DetectionValidator
from ultralytics.data.converter import coco80_to_coco91_class
from ultralytics.data.utils import check_det_dataset
from ultralytics.utils import DATASETS_DIR
from ultralytics.utils import DEFAULT_CFG
from ultralytics.utils.metrics import ConfusionMatrix

def validate(
    model: ov.Model, data_loader: torch.utils.data.DataLoader, validator: DetectionValidator, num_samples: int = None
) -> tuple[dict, int, int]:
    validator.seen = 0
    validator.jdict = []
    validator.stats = dict(tp=[], conf=[], pred_cls=[], target_cls=[], target_img=[])
    validator.end2end = False
    validator.confusion_matrix = ConfusionMatrix(validator.data["nc"])
    compiled_model = ov.compile_model(model, device_name="CPU")
    output_layer = compiled_model.output(0)
    for batch_i, batch in enumerate(track(data_loader, description="Validating")):
        if num_samples is not None and batch_i == num_samples:
            break
        batch = validator.preprocess(batch)
        preds = torch.from_numpy(compiled_model(batch["img"])[output_layer])
        preds = validator.postprocess(preds)
        validator.update_metrics(preds, batch)
    stats = validator.get_stats()
    return stats, validator.seen, validator.nt_per_class.sum()

def print_statistics(stats: dict[str, float], total_images: int, total_objects: int) -> None:
    mp, mr, map50, mean_ap = (
        stats["metrics/precision(B)"],
        stats["metrics/recall(B)"],
        stats["metrics/mAP50(B)"],
        stats["metrics/mAP50-95(B)"],
    )
    s = ("%20s" + "%12s" * 6) % ("Class", "Images", "Labels", "Precision", "Recall", "mAP@.5", "mAP@.5:.95")
    print(s)
    pf = "%20s" + "%12i" * 2 + "%12.3g" * 4  # print format
    print(pf % ("all", total_images, total_objects, mp, mr, map50, mean_ap))

model_name = sys.argv[1]
dataset_file = sys.argv[2]


validator = DetectionValidator()
validator.data = check_det_dataset(dataset_file)
validator.stride = 32
validator.is_coco = True
validator.class_map = coco80_to_coco91_class

data_loader = validator.get_dataloader(validator.data["path"], 1)

def transform_fn(data_item: dict):
    input_tensor = validator.preprocess(data_item)["img"].numpy()
    return input_tensor
    # images, _ = data_item
    # return images.numpy()

calibration_dataset = nncf.Dataset(data_loader, transform_fn)

model = ov.Core().read_model("./public/" + model_name + "/FP32/" + model_name + ".xml")
quantized_model = nncf.quantize(model, calibration_dataset, subset_size = len(data_loader))

# Validate FP32 model
fp_stats, total_images, total_objects = validate(model, data_loader, validator)
print("Floating-point model validation results:")
print_statistics(fp_stats, total_images, total_objects)

# Validate quantized model
q_stats, total_images, total_objects = validate(quantized_model, data_loader, validator)
print("Quantized model validation results:")
print_statistics(q_stats, total_images, total_objects)

quantized_model.set_rt_info(ov.get_version(), "Runtime_version")
ov.save_model(quantized_model, "./public/" + model_name + "/INT8/" + model_name + ".xml", compress_to_fp16=False)
EOF

  source "$VENV_DIR/bin/activate"
  YOLO_CONFIG_DIR=$DOWNLOAD_CONFIG_DIR
  else
    echo_color "\nModel already quantized: $MODEL_DIR.\n" "yellow"
  fi
}

# check if model exists in local directory, download as needed
if [[ " ${MODELS_TO_PROCESS[@]} " =~ " yolox-tiny " ]] || [[ " ${MODELS_TO_PROCESS[@]} " =~ " yolo_all " ]] || [[ " ${MODELS_TO_PROCESS[@]} " =~ " all " ]]; then  MODEL_NAME="yolox-tiny"
  MODEL_DIR="$MODELS_PATH/public/$MODEL_NAME"
  DST_FILE1="$MODEL_DIR/FP16/$MODEL_NAME.xml"
  DST_FILE2="$MODEL_DIR/FP32/$MODEL_NAME.xml"

  if [[ ! -f "$DST_FILE1" || ! -f "$DST_FILE2" ]]; then
    cd "$MODELS_PATH"
    echo "Downloading and converting: ${MODEL_DIR}"
    source "$VENV_DIR/bin/activate"
    omz_downloader --name "$MODEL_NAME"
    omz_converter --name "$MODEL_NAME"
    cd "$MODEL_DIR"
    rm -rf yolox*
    rm -rf models
    rm -rf utils
  else
    echo_color "\nModel already exists: $MODEL_DIR.\n" "yellow"
  fi
fi

if [[ " ${MODELS_TO_PROCESS[@]} " =~ " yolox_s " ]] || [[ " ${MODELS_TO_PROCESS[@]} " =~ " yolo_all " ]] || [[ " ${MODELS_TO_PROCESS[@]} " =~ " all " ]]; then  MODEL_NAME="yolox_s"
  MODEL_DIR="$MODELS_PATH/public/$MODEL_NAME"
  DST_FILE1="$MODEL_DIR/FP16/$MODEL_NAME.xml"
  DST_FILE2="$MODEL_DIR/FP32/$MODEL_NAME.xml"

  if [[ ! -f "$DST_FILE1" || ! -f "$DST_FILE2" ]]; then
    mkdir -p "$MODEL_DIR"
    mkdir -p "$MODEL_DIR/FP16"
    mkdir -p "$MODEL_DIR/FP32"
    cd "$MODEL_DIR"
    curl -O -L https://github.com/Megvii-BaseDetection/YOLOX/releases/download/0.1.1rc0/yolox_s.onnx
    source "$VENV_DIR/bin/activate"
    ovc yolox_s.onnx --compress_to_fp16=True
    mv yolox_s.xml "$MODEL_DIR/FP16"
    mv yolox_s.bin "$MODEL_DIR/FP16"
    ovc yolox_s.onnx --compress_to_fp16=False
    mv yolox_s.xml "$MODEL_DIR/FP32"
    mv yolox_s.bin "$MODEL_DIR/FP32"
    rm -rf yolox_s.onnx
  else
    echo_color "\nModel already exists: $MODEL_DIR.\n" "yellow"
  fi
fi

# -------------- YOLOv5 (ULTRALYTICS) FP32 & INT8

# Function to export YOLOv5 model
export_yolov5_model() {
  local model_name=$1
  local model_path="$MODELS_PATH/public/$model_name"
  local weights="${model_name::-1}.pt"  # Remove the last character from the model name to construct the weights filename

  if [ ! -f "$model_path/FP32/$model_name.xml" ] || [ ! -f "$model_path/FP16/$model_name.xml" ]; then
    echo "Downloading and converting: ${model_path}"
    mkdir -p "$model_path"
    cd "$model_path"
    source "$VENV_DIR/bin/activate"

    python3 - <<EOF
import os
from ultralytics import YOLO
from openvino.runtime import Core, save_model

model_name = "$model_name"
weights = "$weights"
output_dir = f"{model_name}_openvino_model"

def export_model(weights, half, output_dir, model_name):
    model = YOLO(weights)
    model.info()
    model.export(format='openvino', half=half, dynamic=True)
    core = Core()
    model = core.read_model(f"{output_dir}/{model_name}.xml")
    model.reshape([-1, 3, 640, 640])
    save_model(model, f"{output_dir}/{model_name}D.xml")

# Export FP32 model
export_model(weights, half=False, output_dir=output_dir, model_name=model_name)
# Move FP32 model to the appropriate directory
os.makedirs("FP32", exist_ok=True)
os.rename(f"{output_dir}/{model_name}D.xml", f"FP32/{model_name}.xml")
os.rename(f"{output_dir}/{model_name}D.bin", f"FP32/{model_name}.bin")

# Export FP16 model
export_model(weights, half=True, output_dir=output_dir, model_name=model_name)
# Move FP16 model to the appropriate directory
os.makedirs("FP16", exist_ok=True)
os.rename(f"{output_dir}/{model_name}D.xml", f"FP16/{model_name}.xml")
os.rename(f"{output_dir}/{model_name}D.bin", f"FP16/{model_name}.bin")

# Clean up
import shutil
shutil.rmtree(output_dir)
os.remove(f"{model_name}.pt")
EOF
    cd ../..
  else
    echo_color "\nModel already exists: $model_path.\n" "yellow"
  fi

  if [[ $QUANTIZE != "" ]]; then
    quantize_yolo_model "$MODEL_NAME"
  fi
}

# Model yolov5 FP32 & FP16
YOLOv5u_MODELS=("yolov5nu" "yolov5su" "yolov5mu" "yolov5lu" "yolov5xu" "yolov5n6u" "yolov5s6u" "yolov5m6u" "yolov5l6u" "yolov5x6u")

for MODEL_NAME in "${YOLOv5u_MODELS[@]}"; do
  if [[ " ${MODELS_TO_PROCESS[@]} " =~ " $MODEL_NAME " ]] || [[ " ${MODELS_TO_PROCESS[@]} " =~ " yolo_all " ]] || [[ " ${MODELS_TO_PROCESS[@]} " =~ " all " ]]; then
    export_yolov5_model "$MODEL_NAME"
  fi
done

# -------------- YOLOv5 (LEGACY) FP32 & INT8
YOLOv5_MODELS=("yolov5n" "yolov5s" "yolov5m" "yolov5l" "yolov5x" "yolov5n6" "yolov5s6" "yolov5m6" "yolov5l6" "yolov5x6")

# Check if the model is in the list
MODEL_IN_LISTv5=false
for MODEL_NAME in "${YOLOv5_MODELS[@]}"; do
  if [[ " ${MODELS_TO_PROCESS[@]} " =~ " $MODEL_NAME " ]] || [[ " ${MODELS_TO_PROCESS[@]} " =~ " yolo_all " ]] || [[ " ${MODELS_TO_PROCESS[@]} " =~ " all " ]]; then
    MODEL_IN_LISTv5=true
    break
  fi
done

# Clone the repository if the model is in the list
REPO_DIR="$MODELS_PATH/yolov5_repo"
if [ "$MODEL_IN_LISTv5" = true ] && [ ! -d "$REPO_DIR" ]; then
  git clone https://github.com/ultralytics/yolov5 "$REPO_DIR"
  pip install --no-cache-dir --index-url https://download.pytorch.org/whl/cpu torch==2.8.0 torchaudio==2.8.0 torchvision==0.23.0
  pip install --no-cache-dir -r "$REPO_DIR"/requirements.txt
fi

for MODEL_NAME in "${YOLOv5_MODELS[@]}"; do
  if [[ " ${MODELS_TO_PROCESS[@]} " =~ " $MODEL_NAME " ]] || [[ " ${MODELS_TO_PROCESS[@]} " =~ " yolo_all " ]] || [[ " ${MODELS_TO_PROCESS[@]} " =~ " all " ]]; then
    MODEL_DIR="$MODELS_PATH/public/$MODEL_NAME"
    if [ ! -d "$MODEL_DIR" ]; then
      echo "Downloading and converting: ${MODEL_DIR}"
      mkdir -p "$MODEL_DIR"
      cd "$MODEL_DIR"
      cp -r "$REPO_DIR" yolov5
      cd yolov5
      curl -L -O "https://github.com/ultralytics/yolov5/releases/download/v7.0/${MODEL_NAME}.pt"
      source "$VENV_DIR/bin/activate"

      python3 export.py --weights "${MODEL_NAME}.pt" --include openvino --img-size 640 --dynamic
      python3 - <<EOF "${MODEL_NAME}"
import sys, os
from openvino.runtime import Core
from openvino.runtime import save_model
model_name = sys.argv[1]
core = Core()
os.rename(f"{model_name}_openvino_model", f"{model_name}_openvino_modelD")
model = core.read_model(f"{model_name}_openvino_modelD/{model_name}.xml")
model.reshape([-1, 3, 640, 640])
save_model(model, f"{model_name}_openvino_model/{model_name}.xml")
EOF

      mkdir -p "$MODEL_DIR/FP32"
      mv "${MODEL_NAME}_openvino_model/${MODEL_NAME}.xml" "$MODEL_DIR/FP32/${MODEL_NAME}.xml"
      mv "${MODEL_NAME}_openvino_model/${MODEL_NAME}.bin" "$MODEL_DIR/FP32/${MODEL_NAME}.bin"

# # Quantization to INT8 temporarily disabled - causes error which breaks execution
#       mkdir -p "$MODEL_DIR/INT8"
#       python3 export.py --weights "${MODEL_NAME}.pt" --include openvino --img-size 640 --dynamic --int8
#       python3 - <<EOF "${MODEL_NAME}"
# import sys, os
# from openvino.runtime import Core
# from openvino.runtime import save_model
# model_name = sys.argv[1]
# core = Core()
# os.rename(f"{model_name}_int8_openvino_model", f"{model_name}_int8_openvino_modelD")
# model = core.read_model(f"{model_name}_int8_openvino_modelD/{model_name}.xml")
# model.reshape([-1, 3, 640, 640])
# save_model(model, f"{model_name}_int8_openvino_model/{model_name}.xml")
# EOF


#       mv "${MODEL_NAME}_int8_openvino_model/${MODEL_NAME}.xml" "$MODEL_DIR/INT8/${MODEL_NAME}.xml"
#       mv "${MODEL_NAME}_int8_openvino_model/${MODEL_NAME}.bin" "$MODEL_DIR/INT8/${MODEL_NAME}.bin"

      cd ..
      rm -rf yolov5
    else
      echo_color "\nModel already exists: $MODEL_DIR.\n" "yellow"
    fi
  fi
done

# Clean up the repository if it was cloned
if [ "$MODEL_IN_LISTv5" = true ]; then
  rm -rf "$REPO_DIR"
fi


# -------------- YOLOv7 FP32 & FP16
if [[ " ${MODELS_TO_PROCESS[@]} " =~ " yolov7 " ]] || [[ " ${MODELS_TO_PROCESS[@]} " =~ " yolo_all " ]] || [[ " ${MODELS_TO_PROCESS[@]} " =~ " all " ]]; then  MODEL_NAME="yolov7"
  MODEL_DIR="$MODELS_PATH/public/$MODEL_NAME"
  DST_FILE1="$MODEL_DIR/FP16/$MODEL_NAME.xml"
  DST_FILE2="$MODEL_DIR/FP32/$MODEL_NAME.xml"

  if [[ ! -f "$DST_FILE1" || ! -f "$DST_FILE2" ]]; then
    source "$VENV_DIR/bin/activate"
    pip install --no-cache-dir onnx
    pip install --no-cache-dir --index-url https://download.pytorch.org/whl/cpu torch==2.5.1 torchvision==0.20.1 torchaudio==2.5.1  || handle_error $LINENO
    mkdir -p "$MODEL_DIR"
    mkdir -p "$MODEL_DIR/FP16"
    mkdir -p "$MODEL_DIR/FP32"
    cd "$MODEL_DIR"
    echo "Downloading and converting: ${MODEL_DIR}"
    git clone https://github.com/WongKinYiu/yolov7.git
    cd yolov7
    python3 export.py --weights  yolov7.pt  --grid --dynamic-batch
    ovc yolov7.onnx --compress_to_fp16=True
    mv yolov7.xml "$MODEL_DIR/FP16"
    mv yolov7.bin "$MODEL_DIR/FP16"
    ovc yolov7.onnx --compress_to_fp16=False
    mv yolov7.xml "$MODEL_DIR/FP32"
    mv yolov7.bin "$MODEL_DIR/FP32"
    cd ..
    rm -rf yolov7
    pip install --no-cache-dir --upgrade torch==2.8.0 torchaudio==2.8.0 torchvision==0.23.0 || handle_error $LINENO
  else
    echo_color "\nModel already exists: $MODEL_DIR.\n" "yellow"
  fi
fi

# Function to export YOLO model
export_yolo_model() {
  local MODEL_NAME=$1
  local MODEL_TYPE=$2
  local QUANTIZE=$3
  MODEL_DIR="$MODELS_PATH/public/$MODEL_NAME"
  DST_FILE1="$MODEL_DIR/FP16/$MODEL_NAME.xml"
  DST_FILE2="$MODEL_DIR/FP32/$MODEL_NAME.xml"

  if [[ ! -f "$DST_FILE1" || ! -f "$DST_FILE2" ]]; then
    echo "Downloading and converting: ${MODEL_DIR}"
    mkdir -p "$MODEL_DIR"
    cd "$MODEL_DIR"
    source "$VENV_DIR/bin/activate"

    python3 - <<EOF "$MODEL_NAME" "$MODEL_TYPE"
from ultralytics import YOLO
import openvino, sys, shutil, os

model_name = sys.argv[1]
model_type = sys.argv[2]
weights = model_name + '.pt'

model = YOLO(weights)
model.info()
converted_path = model.export(format='openvino')
converted_model = converted_path + '/' + model_name + '.xml'
core = openvino.Core()
ov_model = core.read_model(model=converted_model)

if model_type in ["yolo_v8_seg", "yolo_v11_seg"]:
    ov_model.output(0).set_names({"boxes"})
    ov_model.output(1).set_names({"masks"})

ov_model.set_rt_info(model_type, ['model_info', 'model_type'])

openvino.save_model(ov_model, './FP32/' + model_name + '.xml', compress_to_fp16=False)
openvino.save_model(ov_model, './FP16/' + model_name + '.xml', compress_to_fp16=True)
shutil.rmtree(converted_path)
os.remove(f"{model_name}.pt")
EOF

    cd ../..
  else
    echo_color "\nModel already exists: $MODEL_DIR.\n" "yellow"
  fi

  if [[ $QUANTIZE != "" ]]; then
    quantize_yolo_model "$MODEL_NAME"
  fi
}

# List of models and their types
declare -A YOLO_MODELS
YOLO_MODELS=(
  ["yolov8n"]="yolo_v8"
  ["yolov8s"]="yolo_v8"
  ["yolov8m"]="yolo_v8"
  ["yolov8l"]="yolo_v8"
  ["yolov8x"]="yolo_v8"
  ["yolov8n-obb"]="yolo_v8_obb"
  ["yolov8s-obb"]="yolo_v8_obb"
  ["yolov8m-obb"]="yolo_v8_obb"
  ["yolov8l-obb"]="yolo_v8_obb"
  ["yolov8x-obb"]="yolo_v8_obb"
  ["yolov8n-seg"]="yolo_v8_seg"
  ["yolov8s-seg"]="yolo_v8_seg"
  ["yolov8m-seg"]="yolo_v8_seg"
  ["yolov8l-seg"]="yolo_v8_seg"
  ["yolov8x-seg"]="yolo_v8_seg"
  ["yolov8n-pose"]="yolo_v8_pose"
  ["yolov8s-pose"]="yolo_v8_pose"
  ["yolov8m-pose"]="yolo_v8_pose"
  ["yolov8l-pose"]="yolo_v8_pose"
  ["yolov8x-pose"]="yolo_v8_pose"
  ["yolov9t"]="yolo_v8"
  ["yolov9s"]="yolo_v8"
  ["yolov9m"]="yolo_v8"
  ["yolov9c"]="yolo_v8"
  ["yolov9e"]="yolo_v8"
  ["yolov10n"]="yolo_v10"
  ["yolov10s"]="yolo_v10"
  ["yolov10m"]="yolo_v10"
  ["yolov10b"]="yolo_v10"
  ["yolov10l"]="yolo_v10"
  ["yolov10x"]="yolo_v10"
  ["yolo11n"]="yolo_v11"
  ["yolo11s"]="yolo_v11"
  ["yolo11m"]="yolo_v11"
  ["yolo11l"]="yolo_v11"
  ["yolo11x"]="yolo_v11"
  ["yolo11n-obb"]="yolo_v11_obb"
  ["yolo11s-obb"]="yolo_v11_obb"
  ["yolo11m-obb"]="yolo_v11_obb"
  ["yolo11l-obb"]="yolo_v11_obb"
  ["yolo11x-obb"]="yolo_v11_obb"
  ["yolo11n-seg"]="yolo_v11_seg"
  ["yolo11s-seg"]="yolo_v11_seg"
  ["yolo11m-seg"]="yolo_v11_seg"
  ["yolo11l-seg"]="yolo_v11_seg"
  ["yolo11x-seg"]="yolo_v11_seg"
  ["yolo11n-pose"]="yolo_v11_pose"
  ["yolo11s-pose"]="yolo_v11_pose"
  ["yolo11m-pose"]="yolo_v11_pose"
  ["yolo11l-pose"]="yolo_v11_pose"
  ["yolo11x-pose"]="yolo_v11_pose"
)

# Iterate over the models and export them
for MODEL_NAME in "${!YOLO_MODELS[@]}"; do
  if [[ " ${MODELS_TO_PROCESS[@]} " =~ " $MODEL_NAME " ]] || [[ " ${MODELS_TO_PROCESS[@]} " =~ " yolo_all " ]] || [[ " ${MODELS_TO_PROCESS[@]} " =~ " all " ]]; then
    MODEL_NAME_UPPER=$(echo "$MODEL_NAME" | tr '[:lower:]' '[:upper:]')
    if [[ $MODEL_NAME_UPPER == *"OBB"* || $MODEL_NAME_UPPER == *"POSE"* || $MODEL_NAME_UPPER == *"SEG"* ]]; then
      export_yolo_model "$MODEL_NAME" "${YOLO_MODELS[$MODEL_NAME]}" ""
    else
      export_yolo_model "$MODEL_NAME" "${YOLO_MODELS[$MODEL_NAME]}" "$QUANTIZE"
    fi
  fi
done


if [[ " ${MODELS_TO_PROCESS[@]} " =~ " yolov8_license_plate_detector " ]] || [[ " ${MODELS_TO_PROCESS[@]} " =~ " all " ]]; then
  MODEL_NAME="yolov8_license_plate_detector"
  MODEL_DIR="$MODELS_PATH/public/$MODEL_NAME"
  DST_FILE1="$MODEL_DIR/FP32/$MODEL_NAME.xml"

  if [[ ! -f "$DST_FILE1" ]]; then
    echo "Downloading and converting: ${MODEL_DIR}"
    mkdir -p "$MODEL_DIR"
    cd "$MODEL_DIR"

    curl -L -k -o ${MODEL_NAME}.zip 'https://github.com/open-edge-platform/edge-ai-resources/raw/main/models/license-plate-reader.zip'
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
    echo_color "\nModel already exists: $MODEL_DIR.\n" "yellow"
  fi
fi

if [[ " ${MODELS_TO_PROCESS[@]} " =~ " centerface " ]] || [[ " ${MODELS_TO_PROCESS[@]} " =~ " all " ]]; then
  MODEL_NAME="centerface"
  MODEL_DIR="$MODELS_PATH/public/$MODEL_NAME"
  DST_FILE1="$MODEL_DIR/FP16/$MODEL_NAME.xml"
  DST_FILE2="$MODEL_DIR/FP32/$MODEL_NAME.xml"

  if [[ ! -f "$DST_FILE1" || ! -f "$DST_FILE2" ]]; then
    echo "Downloading and converting: ${MODEL_DIR}"
    mkdir -p "$MODEL_DIR"
    cd "$MODEL_DIR"
    git clone https://github.com/Star-Clouds/CenterFace.git
    cd CenterFace/models/onnx
    source "$VENV_DIR/bin/activate"
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
    echo_color "\nModel already exists: $MODEL_DIR.\n" "yellow"
  fi
fi

#enet_b0_8_va_mtl
if [[ " ${MODELS_TO_PROCESS[@]} " =~ " hsemotion " ]] || [[ " ${MODELS_TO_PROCESS[@]} " =~ " all " ]]; then
  MODEL_NAME="hsemotion"
  MODEL_DIR="$MODELS_PATH/public/$MODEL_NAME"
  DST_FILE="$MODEL_DIR/FP16/$MODEL_NAME.xml"

  if [ ! -f "$DST_FILE" ]; then
    echo "Downloading and converting: ${MODEL_DIR}"
    mkdir -p "$MODEL_DIR"
    cd "$MODEL_DIR"
    git clone https://github.com/av-savchenko/face-emotion-recognition.git
    cd face-emotion-recognition/models/affectnet_emotions/onnx
    source "$VENV_DIR/bin/activate"

    ovc enet_b0_8_va_mtl.onnx --input "[16,3,224,224]"
    mkdir "$MODEL_DIR/FP16/"
    mv enet_b0_8_va_mtl.xml "$MODEL_DIR/$MODEL_NAME.xml"
    mv enet_b0_8_va_mtl.bin "$MODEL_DIR/$MODEL_NAME.bin"
    cd ../../../..
    rm -rf face-emotion-recognition
    python3 - <<EOF
import openvino
import sys, os

core = openvino.Core()
ov_model = core.read_model(model='hsemotion.xml')

ov_model.set_rt_info("anger contempt disgust fear happiness neutral sadness surprise", ['model_info', 'labels'])
ov_model.set_rt_info("label", ['model_info', 'model_type'])
ov_model.set_rt_info("True", ['model_info', 'output_raw_scores'])
ov_model.set_rt_info("fit_to_window_letterbox", ['model_info', 'resize_type'])
ov_model.set_rt_info("255", ['model_info', 'scale_values'])

print(ov_model)

openvino.save_model(ov_model, './FP16/' + 'hsemotion.xml')
os.remove('hsemotion.xml')
os.remove('hsemotion.bin')
EOF
  else
    echo_color "\nModel already exists: $MODEL_DIR.\n" "yellow"
  fi
fi

mapfile -t CLIP_MODELS < <(printf "%s\n" "${SUPPORTED_MODELS[@]}" | grep '^clip-vit-')
for MODEL_NAME in "${CLIP_MODELS[@]}"; do
  if [ "$MODEL" == "$MODEL_NAME" ] || [ "$MODEL" == "all" ]; then
    MODEL_DIR="$MODELS_PATH/public/$MODEL_NAME"
    DST_FILE="$MODEL_DIR/FP32/$MODEL_NAME.xml"

    if [ ! -f "$DST_FILE" ]; then
      echo "Downloading and converting: ${MODEL_DIR}"
      mkdir -p "$MODEL_DIR/FP32"
      cd "$MODEL_DIR/FP32"
      IMAGE_URL="https://storage.openvinotoolkit.org/data/test_data/images/car.png"
      IMAGE_PATH=car.png
      curl -L -o $IMAGE_PATH $IMAGE_URL
      echo "Image downloaded to $IMAGE_PATH"
      source "$VENV_DIR/bin/activate"
      python3 - <<EOF "$MODEL_NAME" "$IMAGE_PATH"
from transformers import CLIPProcessor, CLIPVisionModel
import PIL
import openvino as ov
from openvino.runtime import PartialShape, Type
import sys
import os

MODEL=sys.argv[1]
img_path = sys.argv[2]

img = PIL.Image.open(img_path)
vision_model = CLIPVisionModel.from_pretrained('openai/'+MODEL)
vision_model.eval()
processor = CLIPProcessor.from_pretrained('openai/'+MODEL)
batch = processor.image_processor(images=img, return_tensors='pt')["pixel_values"]

print("Conversion starting...")
ov_model = ov.convert_model(vision_model, example_input=batch)
print("Conversion finished.")

# Define the input shape explicitly
input_shape = PartialShape([-1, batch.shape[1], batch.shape[2], batch.shape[3]])

# Set the input shape and type explicitly
for input in ov_model.inputs:
    input.get_node().set_partial_shape(PartialShape(input_shape))
    input.get_node().set_element_type(Type.f32)

ov_model.set_rt_info("clip_token", ['model_info', 'model_type'])
ov_model.set_rt_info("68.500,66.632,70.323", ['model_info', 'scale_values'])
ov_model.set_rt_info("122.771,116.746,104.094", ['model_info', 'mean_values'])
ov_model.set_rt_info("RGB", ['model_info', 'color_space'])
ov_model.set_rt_info("crop", ['model_info', 'resize_type'])

ov.save_model(ov_model, MODEL + ".xml")

os.remove(img_path)
EOF
    else
      echo_color "\nModel already exists: $MODEL_DIR.\n" "yellow"
    fi
  fi
done

if [[ " ${MODELS_TO_PROCESS[@]} " =~ " deeplabv3 " ]] || [[ " ${MODELS_TO_PROCESS[@]} " =~ " all " ]]; then
  MODEL_NAME="deeplabv3"
  MODEL_DIR="$MODELS_PATH/public/$MODEL_NAME"
  DST_FILE1="$MODEL_DIR/FP32/$MODEL_NAME.xml"
  DST_FILE2="$MODEL_DIR/FP16/$MODEL_NAME.xml"

  pip install --no-cache-dir tensorflow || handle_error $LINENO

  if [[ ! -f "$DST_FILE1" || ! -f "$DST_FILE2" ]]; then
    cd "$MODELS_PATH"
    echo "Downloading and converting: ${MODEL_DIR}"
    source "$VENV_DIR/bin/activate"
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
  else
    echo_color "\nModel already exists: $MODEL_DIR.\n" "yellow"
  fi
fi

# PaddlePaddle OCRv4 multilingual model
if [[ " ${MODELS_TO_PROCESS[@]} " =~ " ch_PP-OCRv4_rec_infer " ]] || [[ " ${MODELS_TO_PROCESS[@]} " =~ " all " ]]; then
  MODEL_NAME="ch_PP-OCRv4_rec_infer"
  MODEL_DIR="$MODELS_PATH/public/$MODEL_NAME"
  DST_FILE1="$MODEL_DIR/FP16/$MODEL_NAME.xml"

  if [[ ! -f "$DST_FILE1" ]]; then
    echo "Downloading and converting: ${MODEL_DIR}"
    mkdir -p "$MODEL_DIR"
    cd "$MODEL_DIR"

    curl -L -k -o ${MODEL_NAME}.zip 'https://github.com/open-edge-platform/edge-ai-resources/raw/main/models/license-plate-reader.zip'
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
    cd -
  else
    echo_color "\nModel already exists: $MODEL_DIR.\n" "yellow"
  fi
fi

# Mars-Small128 DeepSORT Person Re-ID Model
if [[ " ${MODELS_TO_PROCESS[@]} " =~ " mars-small128 " ]] || [[ " ${MODELS_TO_PROCESS[@]} " =~ " all " ]]; then
  MODEL_NAME="mars-small128"
  MODEL_DIR="$MODELS_PATH/public/$MODEL_NAME"

  if [[ ! -f "$MODEL_DIR/mars_small128_fp32.xml" ]]; then
    echo_color "Converting Mars-Small128 model for DeepSORT tracking..." "blue"

    # Get the script directory (samples directory) using absolute path
    cd "$LAUNCH_DIR"
    echo "Current directory: $(pwd)"
    SCRIPT_DIR="$(dirname "$(realpath "${BASH_SOURCE[0]}")")"
    echo "Script directory: $SCRIPT_DIR"
    CONVERTER_SCRIPT="$SCRIPT_DIR/models/convert_mars_deepsort.py"

    if [[ ! -f "$CONVERTER_SCRIPT" ]]; then
      echo_color "ERROR: Converter script not found: $CONVERTER_SCRIPT" "red"
      handle_error $LINENO
    fi

    mkdir -p "$MODEL_DIR"
    cd "$MODEL_DIR"

    # Activate virtual environment
    source "$VENV_DIR/bin/activate"

    # Install dependencies for converter script
    pip install --no-cache-dir torch openvino nncf gdown || handle_error $LINENO

    echo_color "Running Mars-Small128 converter..." "blue"
    python3 "$CONVERTER_SCRIPT" --output-dir "$MODEL_DIR" --precision both || handle_error $LINENO

    echo_color "✅ Mars-Small128 conversion completed" "green"
    echo_color "═══════════════════════════════════════════════════" "cyan"
    echo_color "📁 Output directory: $MODEL_DIR" "blue"
    echo_color "📏 Models: mars_small128_fp32.xml, mars_small128_int8.xml" "blue"
    echo_color "🎯 Usage: DeepSORT person re-identification tracking" "blue"
    echo_color "═══════════════════════════════════════════════════" "cyan"

    cd ../..
  else
    echo_color "\nModel already exists: $MODEL_DIR.\n" "yellow"
  fi
fi

# Deactivate and remove venvs
echo "Removing Python virtual environments..."
deactivate
rm -r $VENV_DIR
rm -r $VENV_DIR_QUANT
echo "Removed"
