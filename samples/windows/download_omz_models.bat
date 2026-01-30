@REM ==============================================================================
@REM Copyright (C) 2026 Intel Corporation
@REM
@REM SPDX-License-Identifier: MIT
@REM ==============================================================================

@echo off

setlocal

if NOT DEFINED MODELS_PATH (
    echo ERROR: MODELS_PATH not set
    EXIT /B 1
)

echo Downloading models to folder %MODELS_PATH%

pip show -qq openvino-dev
if ERRORLEVEL 1 (
  echo "Script requires Open Model Zoo python modules, please install via 'pip install openvino-dev[onnx]'";
  EXIT /B 1
)

mkdir "%MODELS_PATH%"
omz_downloader --list "%~dp0..\models_omz_samples.lst" -o "%MODELS_PATH%"
omz_converter --list "%~dp0..\models_omz_samples.lst" -o "%MODELS_PATH%" -d "%MODELS_PATH%"
