@echo off
setlocal

@REM ==============================================================================
@REM Copyright (C) 2021-2026 Intel Corporation
@REM
@REM SPDX-License-Identifier: MIT
@REM
@REM Windows version of CLIP inference script with Precision support
@REM ==============================================================================

set "DEFAULT_SOURCE=https://videos.pexels.com/video-files/1192116/1192116-sd_640_360_30fps.mp4"
set "DEFAULT_DEVICE=CPU"
set "DEFAULT_OUTPUT=json"
set "DEFAULT_MODEL=clip-vit-large-patch14"
set "DEFAULT_PPBKEND=opencv"
set "DEFAULT_PRECISION=FP32"

@REM Check MODELS_PATH
if "%MODELS_PATH%"=="" (
    echo ERROR - MODELS_PATH is not set. >&2
    exit /B 1
)

@REM Help
if "%1"=="--help" goto :show_help
if "%1"=="-h" goto :show_help
goto :parse_args

:show_help
echo Usage: %~nx0 [SOURCE] [DEVICE] [PRECISION] [MODEL] [PPBKEND] [OUTPUT]
echo.
echo Arguments:
echo   SOURCE      - Input source (default: Pexels video URL)
echo   DEVICE      - Device (default: CPU). Supported: CPU, GPU, NPU
echo   PRECISION   - Model precision (default: FP32). Supported: FP32, FP16, INT8
echo   MODEL       - Model name (default: clip-vit-large-patch14)
echo   PPBKEND     - Preprocessing backend (default: opencv)
echo   OUTPUT      - Output type (default: json). Supported: json, fps
echo.
exit /B 0

:parse_args
set "SOURCE_FILE=%~1"
if "%SOURCE_FILE%"=="" set "SOURCE_FILE=%DEFAULT_SOURCE%"
set "DEVICE=%~2"
if "%DEVICE%"=="" set "DEVICE=%DEFAULT_DEVICE%"
set "PRECISION=%~3"
if "%PRECISION%"=="" set "PRECISION=%DEFAULT_PRECISION%"
set "MODEL=%~4"
if "%MODEL%"=="" set "MODEL=%DEFAULT_MODEL%"
set "PPBKEND=%~5"
if "%PPBKEND%"=="" set "PPBKEND=%DEFAULT_PPBKEND%"
set "OUTPUT=%~6"
if "%OUTPUT%"=="" set "OUTPUT=%DEFAULT_OUTPUT%"

echo MODELS_PATH: %MODELS_PATH%

set "SAFE_MODELS_PATH=%MODELS_PATH:"=%"
set "MODEL_PATH=%SAFE_MODELS_PATH%\public\%MODEL%\%PRECISION%\%MODEL%.xml"

if NOT EXIST "%MODEL_PATH%" (
    echo ERROR - model not found: %MODEL_PATH% >&2
    echo Please check if the precision %PRECISION% folder exists.
    exit /B 1
)

set "MODEL_PATH_GS=%MODEL_PATH:\=/%"

echo %SOURCE_FILE% | findstr /C:"://" >nul
if %ERRORLEVEL%==0 (
    set "SOURCE_ELEMENT=urisourcebin buffer-size=4096 uri=%SOURCE_FILE%"
) else (
    set "SOURCE_ELEMENT=filesrc location="%SOURCE_FILE:\=/%""
)

if /I "%DEVICE%"=="CPU" (
    set "PROC_ELEMENT=videoconvert ! videoscale"
    set "PPBKEND=opencv"
) else if /I "%DEVICE%"=="GPU" (
    if /I "%PPBKEND%"=="opencv" set "PPBKEND=d3d11"
    set "PROC_ELEMENT=d3d11convert"
) else if /I "%DEVICE%"=="NPU" (
    if /I "%PPBKEND%"=="opencv" set "PPBKEND=d3d11"
    set "PROC_ELEMENT=d3d11convert"
) else (
    echo [ERROR] Unsupported device: %DEVICE%
    echo Supported devices: CPU, GPU, NPU
    exit /B 1
)

if /I "%OUTPUT%"=="json" (
    set "PIPELINE=gst-launch-1.0 %SOURCE_ELEMENT% ! decodebin3 ! %PROC_ELEMENT% ! gvainference model=%MODEL_PATH_GS% device=%DEVICE% pre-process-backend=%PPBKEND% ! gvametaconvert format=json add-tensor-data=true ! gvametapublish method=file file-path=output.json ! fakesink"
) else (
    set "PIPELINE=gst-launch-1.0 %SOURCE_ELEMENT% ! decodebin3 ! %PROC_ELEMENT% ! gvainference model=%MODEL_PATH_GS% device=%DEVICE% pre-process-backend=%PPBKEND% ! gvafpscounter ! fakesink"
)

echo Pipeline: %PIPELINE%

%PIPELINE%

endlocal