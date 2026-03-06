@REM ==============================================================================
@REM Copyright (C) 2026 Intel Corporation
@REM
@REM SPDX-License-Identifier: MIT
@REM ==============================================================================

@echo off
setlocal

set "DEVICE=%~1"
set "SRC=%~2"
set "MODEL=%~3"
set "PRECISION=%~4"
set "BACKEND=%~5"
set "OUTPUT=%~6"
set "MD_OPTS=%~7"

if "%DEVICE%"==""    set "DEVICE=CPU"
if "%DEVICE%"=="."   set "DEVICE=CPU"

if "%SRC%"==""       set "SRC=DEFAULT"
if "%SRC%"=="."       set "SRC=DEFAULT"
if /I "%SRC%"=="DEFAULT" (
    set "SRC=https://videos.pexels.com/video-files/1192116/1192116-sd_640_360_30fps.mp4"
)

if "%PRECISION%"=="" set "PRECISION=FP32"
if "%PRECISION%"=="." set "PRECISION=FP32"

if "%BACKEND%"==""   set "BACKEND=opencv"
if "%BACKEND%"=="."  set "BACKEND=opencv"

if "%OUTPUT%"==""    set "OUTPUT=display"
if "%OUTPUT%"=="."   set "OUTPUT=display"

set "MODEL_NAME=yolov8n"
set "USE_DEFAULT_MODEL=0"
if "%MODEL%"=="" set "USE_DEFAULT_MODEL=1"
if "%MODEL%"=="." set "USE_DEFAULT_MODEL=1"
if "%USE_DEFAULT_MODEL%"=="1" (
    set "MODEL_RAW=%MODELS_PATH%\public\%MODEL_NAME%\%PRECISION%\%MODEL_NAME%.xml"
) else (
    set "MODEL_RAW=%MODEL%"
)
set "MODEL_FINAL=%MODEL_RAW:\=/%"

set "SRC_FIXED=%SRC:\=/%"
echo %SRC% | findstr /C:"://" >nul
if errorlevel 1 (
    set "SOURCE_ELEMENT=filesrc location=%SRC_FIXED%"
) else (
    set "SOURCE_ELEMENT=urisourcebin uri=%SRC%"
)

if /I "%OUTPUT%"=="json" (
    if exist output.json del output.json
    set "SINK_STR=gvametaconvert format=json ! gvametapublish method=file file-format=json-lines file-path=output.json ! gvafpscounter ! fakesink"
) else (
    set "SINK_STR=gvawatermark ! videoconvert ! gvafpscounter ! autovideosink"
)

if /I NOT "%DEVICE%"=="CPU" (
    echo ============================================================
    echo [ERROR] Invalid Device: "%DEVICE%"
    echo This specific demo script is configured for "CPU" ONLY.
    exit /b 1
)

set "GVADET=gvadetect model=%MODEL_FINAL% device=CPU pre-process-backend=opencv inference-region=1"
set "CAPS_PART=! video/x-raw(memory:SystemMemory) "

set "PIPELINE=gst-launch-1.0 -e %SOURCE_ELEMENT% ! decodebin3 %CAPS_PART% ! gvamotiondetect %MD_OPTS% ! %GVADET% ! %SINK_STR%"

echo ==============================================================================
echo Running Pipeline:
echo %PIPELINE%
echo ==============================================================================

%PIPELINE%

if %errorlevel% neq 0 (
    echo [ERROR] Pipeline failed with exit code %errorlevel%
    exit /b %errorlevel%
)
endlocal
