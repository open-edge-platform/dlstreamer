@REM ==============================================================================
@REM Copyright (C) 2021-2025 Intel Corporation
@REM
@REM SPDX-License-Identifier: MIT
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
set INPUT=%~1
if "%INPUT%"=="" set INPUT=https://videos.pexels.com/video-files/5144823/5144823-uhd_3840_2160_25fps.mp4

set DEVICE=%~2
if "%DEVICE%"=="" set DEVICE=CPU

set OUTPUT=%~3
if "%OUTPUT%"=="" set OUTPUT=file

set JSON_FILE=%~4
if "%JSON_FILE%"=="" set JSON_FILE=output.json

@REM Show help
if "%INPUT%"=="--help" goto :show_help
if "%INPUT%"=="-h" goto :show_help
goto :skip_help

:show_help
echo Usage: action_recognition.bat [INPUT] [DEVICE] [OUTPUT] [JSON_FILE]
echo.
echo Arguments:
echo   INPUT     - Input source (default: Pexels video URL)
echo   DEVICE    - Device (default: CPU). Supported: CPU, GPU, NPU
echo   OUTPUT    - Output type (default: file). Supported: file, display, fps, json, display-and-json
echo   JSON_FILE - JSON output file name (default: output.json)
echo.
EXIT /B 0

:skip_help

@REM Input validation removed - empty string will use default URL

@REM Set model paths
set MODEL_ENCODER=%MODELS_PATH%\intel\action-recognition-0001\action-recognition-0001-encoder\FP32\action-recognition-0001-encoder.xml
set MODEL_DECODER=%MODELS_PATH%\intel\action-recognition-0001\action-recognition-0001-decoder\FP32\action-recognition-0001-decoder.xml
set LABELS_FILE=%~dp0kinetics_400.txt

@REM Check if models exist
if NOT EXIST "%MODEL_ENCODER%" (
    echo [91mERROR: Model not found: %MODEL_ENCODER%[0m
    echo Please run download_omz_models.bat to download the models first.
    EXIT /B 1
)

if NOT EXIST "%MODEL_DECODER%" (
    echo [91mERROR: Model not found: %MODEL_DECODER%[0m
    echo Please run download_omz_models.bat to download the models first.
    EXIT /B 1
)

@REM Set source element based on input type
set SOURCE_ELEMENT=
echo "%INPUT%" | findstr /C:"://" >nul
if %ERRORLEVEL%==0 (
    set "SOURCE_ELEMENT=urisourcebin buffer-size=4096 uri=%INPUT%"
) else (
    set "SOURCE_ELEMENT=filesrc location=%INPUT:\=/%"
)

@REM Set sink element based on output type
if "%OUTPUT%"=="file" (
    call :set_file_sink
) else if "%OUTPUT%"=="display" (
    set "SINK_ELEMENT=d3d11convert ! gvawatermark ! videoconvert ! gvafpscounter ! autovideosink sync=false"
) else if "%OUTPUT%"=="fps" (
    set "SINK_ELEMENT=gvafpscounter ! fakesink async=false"
) else if "%OUTPUT%"=="json" (
    if EXIST "%JSON_FILE%" del "%JSON_FILE%"
    set "SINK_ELEMENT=gvametaconvert ! gvametapublish file-format=json-lines file-path=%JSON_FILE% ! fakesink async=false"
) else if "%OUTPUT%"=="display-and-json" (
    if EXIST "%JSON_FILE%" del "%JSON_FILE%"
    set "SINK_ELEMENT=d3d11convert ! gvawatermark ! gvametaconvert ! gvametapublish file-format=json-lines file-path=%JSON_FILE% ! videoconvert ! gvafpscounter ! autovideosink sync=false"
) else (
    echo [91mERROR: Invalid OUTPUT parameter[0m
    echo Valid values: display, file, fps, json, display-and-json
    EXIT /B 1
)
goto :continue_after_sink

:set_file_sink
setlocal EnableDelayedExpansion
for %%F in ("%INPUT%") do set FILENAME=%%~nF
set OUTPUT_FILE=action_recognition_!FILENAME!_%DEVICE%.mp4
if EXIST "!OUTPUT_FILE!" del "!OUTPUT_FILE!"
endlocal & set "SINK_ELEMENT=d3d11convert ! gvawatermark ! gvafpscounter ! d3d11h264enc ! h264parse ! mp4mux ! filesink location=%OUTPUT_FILE%"
exit /b

:continue_after_sink

@REM Convert paths to forward slashes for GStreamer
set MODEL_ENCODER=%MODEL_ENCODER:\=/%
set MODEL_DECODER=%MODEL_DECODER:\=/%
set LABELS_FILE=%LABELS_FILE:\=/%

@REM Build and run pipeline
echo.
echo Running pipeline:
echo gst-launch-1.0 %SOURCE_ELEMENT% ^! decodebin3 ^! video_inference process="openvino_tensor_inference model=%MODEL_ENCODER% device=%DEVICE% ^! tensor_sliding_window ^! openvino_tensor_inference model=%MODEL_DECODER% device=%DEVICE%" postprocess="tensor_postproc_label labels-file=%LABELS_FILE% method=softmax" ^! %SINK_ELEMENT%
echo.

gst-launch-1.0 %SOURCE_ELEMENT% ! decodebin3 ! video_inference process="openvino_tensor_inference model=%MODEL_ENCODER% device=%DEVICE% ! tensor_sliding_window ! openvino_tensor_inference model=%MODEL_DECODER% device=%DEVICE%" postprocess="tensor_postproc_label labels-file=%LABELS_FILE% method=softmax" ! %SINK_ELEMENT%

EXIT /B %ERRORLEVEL%
