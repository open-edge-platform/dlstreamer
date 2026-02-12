@REM ==============================================================================
@REM Copyright (C) 2026 Intel Corporation
@REM
@REM SPDX-License-Identifier: MIT
@REM ==============================================================================
@REM This sample refers to a video file by Rihard-Clement-Ciprian Diac via Pexels
@REM (https://www.pexels.com)
@REM ==============================================================================

@echo off
setlocal

@REM Check MODELS_PATH
if NOT DEFINED MODELS_PATH (
    echo [91mERROR: MODELS_PATH is not set.[0m
    EXIT /B 1
)
echo MODELS_PATH: %MODELS_PATH%

@REM Parse arguments
set MODEL=%1
if [%MODEL%]==[] set MODEL=yolox_s

set DEVICE=%2
if [%DEVICE%]==[] set DEVICE=GPU

set INPUT=%3
if [%INPUT%]==[] set INPUT=https://videos.pexels.com/video-files/1192116/1192116-sd_640_360_30fps.mp4

set OUTPUT=%4
if [%OUTPUT%]==[] set OUTPUT=display

set PPBKEND=%5
if [%PPBKEND%]==[] set PPBKEND=d3d11

set PRECISION=%6
if [%PRECISION%]==[] set PRECISION=FP16

@REM Show help
if "%MODEL%"=="--help" goto :show_help
if "%MODEL%"=="-h" goto :show_help
goto :skip_help

:show_help
echo Usage: yolo_detect.bat [MODEL] [DEVICE] [INPUT] [OUTPUT] [PPBKEND] [PRECISION]
echo.
echo Arguments:
echo   MODEL     - Model name (default: yolox_s)
echo             Supported: yolox-tiny, yolox_s, yolov5s, yolov5su, yolov7, yolov8s, yolov8n-obb, yolov8n-seg, yolov9c, yolov10s, yolo11s, yolo11s-obb, yolo11s-seg, yolo11s-pose
echo   DEVICE    - Device (default: GPU). Supported: CPU, GPU, NPU
echo   INPUT     - Input source (default: Pexels video URL)
echo   OUTPUT    - Output type (default: display). Supported: file, display, fps, json, display-and-json
echo   PPBKEND   - Preprocessing backend (default: auto). Supported: ie, opencv for CPU, d3d11 for GPU/NPU
echo   PRECISION - Model precision (default: INT8). Supported: INT8, FP32, FP16
echo.
EXIT /B 0

:skip_help

@REM Validate model
set VALID_MODEL=0
for %%m in (yolox-tiny yolox_s yolov5s yolov5su yolov7 yolov8s yolov8n-obb yolov8n-seg yolov9c yolov10s yolo11s yolo11s-seg yolo11s-obb yolo11s-pose) do (
    if "%MODEL%"=="%%m" set VALID_MODEL=1
)
if %VALID_MODEL%==0 (
    echo [91mERROR: Unsupported model: %MODEL%[0m
    EXIT /B 1
)

@REM Check for yolov10s NPU restriction
if "%MODEL%"=="yolov10s" if "%DEVICE%"=="NPU" (
    echo [91mERROR: No support of Yolov10s for NPU.[0m
    EXIT /B 1
)

@REM Validate precision
if NOT "%PRECISION%"=="INT8" if NOT "%PRECISION%"=="FP32" if NOT "%PRECISION%"=="FP16" (
    echo [91mERROR: Unsupported model precision: %PRECISION%[0m
    EXIT /B 1
)

@REM Set model-proc file based on model
set MODEL_PROC=
if "%MODEL%"=="yolox-tiny" set MODEL_PROC=%~dp0..\..\..\..\gstreamer\model_proc\public\yolo-x.json
if "%MODEL%"=="yolox_s" set MODEL_PROC=%~dp0..\..\..\..\gstreamer\model_proc\public\yolo-x.json
if "%MODEL%"=="yolov5s" set MODEL_PROC=%~dp0..\..\..\..\gstreamer\model_proc\public\yolo-v7.json
if "%MODEL%"=="yolov5su" set MODEL_PROC=%~dp0..\..\..\..\gstreamer\model_proc\public\yolo-v8.json
if "%MODEL%"=="yolov7" set MODEL_PROC=%~dp0..\..\..\..\gstreamer\model_proc\public\yolo-v7.json
@REM Set model path (strip quotes from MODELS_PATH)
set MODEL_PATH=%MODELS_PATH:"=%\public\%MODEL%\%PRECISION%\%MODEL%.xml

@REM Check if model exists
if NOT EXIST "%MODEL_PATH%" (
    echo [91mERROR: Model not found: %MODEL_PATH%[0m
    echo Please run download_public_models.bat to download the models first.
    EXIT /B 1
)

@REM Set source element based on input type
set SOURCE_ELEMENT=
echo %INPUT% | findstr /C:"://" >nul
if %ERRORLEVEL%==0 (
    set SOURCE_ELEMENT=urisourcebin uri=%INPUT%
) else (
    set "SOURCE_ELEMENT=filesrc location=%INPUT:\=/%"
)

@REM Set pre-process backend based on device
@REM On Windows: use d3d11 for GPU/NPU, ie for CPU
if [%PPBKEND%]==[] (
    if "%DEVICE%"=="CPU" (
        set PREPROC_BACKEND=ie
    ) else (
        set PREPROC_BACKEND=d3d11
    )
) else (
    if "%PPBKEND%"=="ie" set PREPROC_BACKEND=ie
    if "%PPBKEND%"=="opencv" set PREPROC_BACKEND=opencv
    if "%PPBKEND%"=="d3d11" set PREPROC_BACKEND=d3d11
    if NOT DEFINED PREPROC_BACKEND (
        echo [91mERROR: Invalid PREPROC_BACKEND parameter. Supported values: ie, opencv, d3d11[0m
        EXIT /B 1
    )
)

@REM Set IE config for yolov10s on GPU
set IE_CONFIG=
if "%MODEL%"=="yolov10s" if "%DEVICE%"=="GPU" (
    set IE_CONFIG=ie-config=GPU_DISABLE_WINOGRAD_CONVOLUTION=YES
)

@REM Set sink element based on output type
if "%OUTPUT%"=="file" (
    for %%F in ("%INPUT%") do set FILENAME=%%~nF
    set OUTPUT_FILE=yolo_%FILENAME%_%MODEL%_%PRECISION%_%DEVICE%.mp4
    if EXIST "%OUTPUT_FILE%" del "%OUTPUT_FILE%"
    set SINK_ELEMENT=! queue ! d3d11convert ! gvawatermark ! gvafpscounter ! d3d11h264enc ! h264parse ! mp4mux ! filesink location=%OUTPUT_FILE%
) else if "%OUTPUT%"=="display" (
    set SINK_ELEMENT=! queue ! d3d11convert ! gvawatermark ! videoconvert ! gvafpscounter ! d3d11videosink sync=false
) else if "%OUTPUT%"=="fps" (
    set SINK_ELEMENT=! queue ! gvafpscounter ! fakesink async=false
) else if "%OUTPUT%"=="json" (
    if EXIST "output.json" del "output.json"
    set SINK_ELEMENT=! queue ! gvametaconvert add-tensor-data=true ! gvametapublish file-format=json-lines file-path=output.json ! fakesink async=false
) else if "%OUTPUT%"=="display-and-json" (
    if EXIST "output.json" del "output.json"
    set SINK_ELEMENT=! queue ! d3d11convert ! gvawatermark ! gvametaconvert add-tensor-data=true ! gvametapublish file-format=json-lines file-path=output.json ! videoconvert ! gvafpscounter ! d3d11videosink sync=false
) else (
    echo [91mERROR: Invalid OUTPUT parameter[0m
    echo Valid values: file, display, fps, json, display-and-json
    EXIT /B 1
)

@REM Convert paths to forward slashes for GStreamer
set MODEL_PATH=%MODEL_PATH:\=/%
if DEFINED MODEL_PROC set MODEL_PROC=%MODEL_PROC:\=/%

@REM Build pipeline
set MODEL_PROC_PART=
if DEFINED MODEL_PROC set MODEL_PROC_PART= model-proc=%MODEL_PROC%

set IE_CONFIG_PART=
if DEFINED IE_CONFIG set IE_CONFIG_PART= %IE_CONFIG%

@REM Build complete pipeline in one line
set PIPELINE=gst-launch-1.0 %SOURCE_ELEMENT% ! decodebin3 ! gvadetect model=%MODEL_PATH% %MODEL_PROC_PART% device=%DEVICE% pre-process-backend=%PREPROC_BACKEND% %IE_CONFIG_PART% %SINK_ELEMENT%

@REM Run pipeline
echo.
echo Running pipeline:
echo %PIPELINE%
echo.

%PIPELINE%

EXIT /B %ERRORLEVEL%
