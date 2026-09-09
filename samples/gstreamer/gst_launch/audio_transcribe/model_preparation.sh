#!/bin/bash
# ==============================================================================
# Copyright (C) 2018-2025 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================


# temperary deactivate OPENVINO_DIR LD_LIBRARY_PATH PYTHONPATH envs
deactivate 2>/dev/null || true
unset OPENVINO_DIR LD_LIBRARY_PATH PYTHONPATH

# Create workspace
mkdir -p ~/ov-model-prep
cd ~/ov-model-prep || exit

# Download Whisper model requirements files
wget https://raw.githubusercontent.com/openvinotoolkit/openvino.genai/refs/heads/releases/2025/3/samples/requirements.txt
wget https://raw.githubusercontent.com/openvinotoolkit/openvino.genai/refs/heads/releases/2025/3/samples/deployment-requirements.txt
wget https://raw.githubusercontent.com/openvinotoolkit/openvino.genai/refs/heads/releases/2025/3/samples/export-requirements.txt

# Create and activate Python virtual environment
uv venv ~/ov-whisper-env
# shellcheck source=/dev/null
source ~/ov-whisper-env/bin/activate

# Install requirements
uv pip install -r requirements.txt

# Download & convert the Whisper model
optimum-cli export openvino --trust-remote-code --model openai/whisper-base whisper-base

deactivate
# Clean up
rm -rf ./*.txt __pycache__ ~/ov-whisper-env

echo "Model preparation complete."

