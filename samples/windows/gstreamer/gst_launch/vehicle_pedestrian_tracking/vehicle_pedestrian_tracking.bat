@REM ==============================================================================
@REM Copyright (C) 2020-2025 Intel Corporation
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
if "%INPUT%"=="" set INPUT=https://github.com/intel-iot-devkit/sample-videos/raw/master/person-bicycle-car-detection.mp4

set DETECTION_INTERVAL=%~2
if "%DETECTION_INTERVAL%"=="" set DETECTION_INTERVAL=3

set DEVICE=%~3
if "%DEVICE%"=="" set DEVICE=AUTO

set OUTPUT=%~4
if "%OUTPUT%"=="" set OUTPUT=display

set TRACKING_TYPE=%~5
if "%TRACKING_TYPE%"=="" set TRACKING_TYPE=short-term-imageless

set JSON_FILE=%~6
if "%JSON_FILE%"=="" set JSON_FILE=output.json

@REM Show help
if "%INPUT%"=="--help" goto :show_help
if "%INPUT%"=="-h" goto :show_help
goto :skip_help

:show_help
echo Usage: vehicle_pedestrian_tracking.bat [INPUT] [DETECTION_INTERVAL] [DEVICE] [OUTPUT] [TRACKING_TYPE] [JSON_FILE]
echo.
echo Arguments:
echo   INPUT               - Input source (default: GitHub sample video URL)
echo   DETECTION_INTERVAL  - Object detection interval (default: 3). 1 means detection every frame, 2 means every second frame, etc.
echo   DEVICE              - Device (default: AUTO). Supported: AUTO, CPU, GPU, GPU.0
echo   OUTPUT              - Output type (default: display). Supported: display, display-async, fps, json, display-and-json, file
echo   TRACKING_TYPE       - Object tracking type (default: short-term-imageless). Supported: short-term-imageless, zero-term, zero-term-imageless
echo   JSON_FILE           - JSON output file name (default: output.json)
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

@REM Set decode element based on device
if "%DEVICE%"=="CPU" (
    set DECODE_ELEMENT=decodebin3
    set PREPROC_BACKEND=opencv
) else if "%DEVICE%"=="GPU" (
    set DECODE_ELEMENT=decodebin3
    set PREPROC_BACKEND=d3d11
) else (
    set DECODE_ELEMENT=decodebin3
    set PREPROC_BACKEND=d3d11
)

@REM Set sink element based on output type
if "%OUTPUT%"=="file" (
    call :set_file_sink
) else if "%OUTPUT%"=="display" (
    set "SINK_ELEMENT=d3d11convert ! gvawatermark ! videoconvert ! gvafpscounter ! autovideosink"
) else if "%OUTPUT%"=="display-async" (
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
    echo Valid values: display, display-async, fps, json, display-and-json, file
    EXIT /B 1
)
goto :continue_after_sink

:set_file_sink
setlocal EnableDelayedExpansion
for %%F in ("%INPUT%") do set FILENAME=%%~nF
set OUTPUT_FILE=vehicle_pedestrian_tracking_!FILENAME!_%DEVICE%.mp4
if EXIST "!OUTPUT_FILE!" del "!OUTPUT_FILE!"
endlocal & set "SINK_ELEMENT=d3d11convert ! gvawatermark ! gvafpscounter ! d3d11h264enc ! h264parse ! mp4mux ! filesink location=%OUTPUT_FILE%"
exit /b

:continue_after_sink

@REM Set model paths
set MODEL_1=person-vehicle-bike-detection-2004
set MODEL_2=person-attributes-recognition-crossroad-0230
set MODEL_3=vehicle-attributes-recognition-barrier-0039

set DETECTION_MODEL=%MODELS_PATH%\intel\%MODEL_1%\FP32\%MODEL_1%.xml
set PERSON_CLASSIFICATION_MODEL=%MODELS_PATH%\intel\%MODEL_2%\FP32\%MODEL_2%.xml
set VEHICLE_CLASSIFICATION_MODEL=%MODELS_PATH%\intel\%MODEL_3%\FP32\%MODEL_3%.xml

set DETECTION_MODEL_PROC=%~dp0model_proc\%MODEL_1%.json
set PERSON_CLASSIFICATION_MODEL_PROC=%~dp0model_proc\%MODEL_2%.json
set VEHICLE_CLASSIFICATION_MODEL_PROC=%~dp0model_proc\%MODEL_3%.json

@REM Check if models exist
if NOT EXIST "%DETECTION_MODEL%" (
    echo [91mERROR: Model not found: %DETECTION_MODEL%[0m
    echo Please run download_omz_models.bat to download the models first.
    EXIT /B 1
)

@REM Convert paths to forward slashes for GStreamer
set DETECTION_MODEL=%DETECTION_MODEL:\=/%
set PERSON_CLASSIFICATION_MODEL=%PERSON_CLASSIFICATION_MODEL:\=/%
set VEHICLE_CLASSIFICATION_MODEL=%VEHICLE_CLASSIFICATION_MODEL:\=/%
set DETECTION_MODEL_PROC=%DETECTION_MODEL_PROC:\=/%
set PERSON_CLASSIFICATION_MODEL_PROC=%PERSON_CLASSIFICATION_MODEL_PROC:\=/%
set VEHICLE_CLASSIFICATION_MODEL_PROC=%VEHICLE_CLASSIFICATION_MODEL_PROC:\=/%

@REM Reclassify interval (run classification every 10th frame)
set RECLASSIFY_INTERVAL=10

@REM Build and run pipeline
echo.
echo Running pipeline...
echo.

gst-launch-1.0 %SOURCE_ELEMENT% ! %DECODE_ELEMENT% ! queue ! gvadetect model=%DETECTION_MODEL% model-proc=%DETECTION_MODEL_PROC% inference-interval=%DETECTION_INTERVAL% threshold=0.4 device=%DEVICE% pre-process-backend=%PREPROC_BACKEND% ! queue ! gvatrack tracking-type=%TRACKING_TYPE% ! queue ! gvaclassify model=%PERSON_CLASSIFICATION_MODEL% model-proc=%PERSON_CLASSIFICATION_MODEL_PROC% reclassify-interval=%RECLASSIFY_INTERVAL% device=%DEVICE% pre-process-backend=%PREPROC_BACKEND% object-class=person ! queue ! gvaclassify model=%VEHICLE_CLASSIFICATION_MODEL% model-proc=%VEHICLE_CLASSIFICATION_MODEL_PROC% reclassify-interval=%RECLASSIFY_INTERVAL% device=%DEVICE% pre-process-backend=%PREPROC_BACKEND% object-class=vehicle ! queue ! %SINK_ELEMENT%

EXIT /B %ERRORLEVEL%
