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
@REM If INPUT is empty or just "", use default URL
if "%INPUT%"=="" set INPUT=https://github.com/intel-iot-devkit/sample-videos/raw/master/face-demographics-walking.mp4

set DEVICE=%~2
if "%DEVICE%"=="" set DEVICE=CPU

set OUTPUT=%~3
if "%OUTPUT%"=="" set OUTPUT=display

set JSON_FILE=%~4
if "%JSON_FILE%"=="" set JSON_FILE=output.json

@REM Show help
if "%INPUT%"=="--help" goto :show_help
if "%INPUT%"=="-h" goto :show_help
goto :skip_help

:show_help
echo Usage: human_pose_estimation.bat [INPUT] [DEVICE] [OUTPUT] [JSON_FILE]
echo.
echo Arguments:
echo   INPUT     - Input source (default: GitHub sample video URL)
echo   DEVICE    - Device (default: CPU). Supported: CPU, GPU, NPU
echo   OUTPUT    - Output type (default: display). Supported: file, display, fps, json, display-and-json
echo   JSON_FILE - JSON output file name (default: output.json)
echo.
EXIT /B 0

:skip_help

@REM Set source element based on input type
set SOURCE_ELEMENT=
echo %INPUT% | findstr /C:"://" >nul
if %ERRORLEVEL%==0 (
    set SOURCE_ELEMENT=urisourcebin buffer-size=4096 uri=%INPUT%
) else (
    set "SOURCE_ELEMENT=filesrc location=%INPUT:\=/%"
)

@REM Set preprocessing backend based on device
if "%DEVICE%"=="CPU" (
    set PREPROC_BACKEND=opencv
    set DECODE_ELEMENT=decodebin3
) else (
    set PREPROC_BACKEND=d3d11
    set DECODE_ELEMENT=decodebin3
)

@REM Set sink element based on output type
if "%OUTPUT%"=="file" (
    call :set_file_sink
) else if "%OUTPUT%"=="display" (
    set "SINK_ELEMENT=queue ! d3d11convert ! gvawatermark ! videoconvert ! gvafpscounter ! autovideosink sync=false"
) else if "%OUTPUT%"=="fps" (
    set "SINK_ELEMENT=queue ! gvafpscounter ! fakesink async=false"
) else if "%OUTPUT%"=="json" (
    if EXIST "%JSON_FILE%" del "%JSON_FILE%"
    set "SINK_ELEMENT=queue ! gvametaconvert add-tensor-data=true ! gvametapublish file-format=json-lines file-path=%JSON_FILE% ! fakesink async=false"
) else if "%OUTPUT%"=="display-and-json" (
    if EXIST "%JSON_FILE%" del "%JSON_FILE%"
    set "SINK_ELEMENT=queue ! d3d11convert ! gvawatermark ! gvametaconvert add-tensor-data=true ! gvametapublish file-format=json-lines file-path=%JSON_FILE% ! videoconvert ! gvafpscounter ! autovideosink sync=false"
) else (
    echo [91mERROR: Invalid OUTPUT parameter[0m
    echo Valid values: file, display, fps, json, display-and-json
    EXIT /B 1
)
goto :continue_after_sink

:set_file_sink
setlocal EnableDelayedExpansion
for %%F in ("%INPUT%") do set FILENAME=%%~nF
set OUTPUT_FILE=human_pose_estimation_!FILENAME!_%DEVICE%.mp4
if EXIST "!OUTPUT_FILE!" del "!OUTPUT_FILE!"
endlocal & set "SINK_ELEMENT=queue ! d3d11convert ! gvawatermark ! gvafpscounter ! d3d11h264enc ! h264parse ! mp4mux ! filesink location=%OUTPUT_FILE%"
exit /b

:continue_after_sink

@REM Set model paths
set MODEL=human-pose-estimation-0001
set MODEL_PATH=%MODELS_PATH%\intel\%MODEL%\FP32\%MODEL%.xml
set MODEL_PROC=%~dp0model_proc\%MODEL%.json

@REM Check if model exists
if NOT EXIST "%MODEL_PATH%" (
    echo [91mERROR: Model not found: %MODEL_PATH%[0m
    echo Please run download_omz_models.bat to download the models first.
    EXIT /B 1
)

@REM Convert paths to forward slashes for GStreamer
set MODEL_PATH=%MODEL_PATH:\=/%
set MODEL_PROC=%MODEL_PROC:\=/%

@REM Build and run pipeline
echo.
echo Running pipeline:
echo gst-launch-1.0 %SOURCE_ELEMENT% ^! %DECODE_ELEMENT% ^! gvaclassify model=%MODEL_PATH% model-proc=%MODEL_PROC% device=%DEVICE% inference-region=full-frame pre-process-backend=%PREPROC_BACKEND% ^! %SINK_ELEMENT%
echo.

gst-launch-1.0 %SOURCE_ELEMENT% ! %DECODE_ELEMENT% ! gvaclassify model=%MODEL_PATH% model-proc=%MODEL_PROC% device=%DEVICE% inference-region=full-frame pre-process-backend=%PREPROC_BACKEND% ! %SINK_ELEMENT%

EXIT /B %ERRORLEVEL%
