@REM ==============================================================================
@REM Copyright (C) 2026 Intel Corporation
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
if "%INPUT%"=="" set INPUT=https://github.com/open-edge-platform/edge-ai-resources/raw/main/videos/ParkingVideo.mp4

set DEVICE=%~2
if "%DEVICE%"=="" set DEVICE=GPU

set OUTPUT=%~3
if "%OUTPUT%"=="" set OUTPUT=fps

set JSON_FILE=%~4
if "%JSON_FILE%"=="" set JSON_FILE=output.json

@REM Show help
if "%INPUT%"=="--help" goto :show_help
if "%INPUT%"=="-h" goto :show_help
goto :skip_help

:show_help
echo Usage: license_plate_recognition.bat [INPUT] [DEVICE] [OUTPUT] [JSON_FILE]
echo.
echo Arguments:
echo   INPUT     - Input source (default: GitHub parking video URL)
echo   DEVICE    - Device (default: GPU). Supported: CPU, GPU, NPU
echo   OUTPUT    - Output type (default: fps). Supported: display, display-async, fps, json, display-and-json, file
echo   JSON_FILE - JSON output file name (default: output.json)
echo.
EXIT /B 0

:skip_help

@REM Set model paths
set DETECTION_MODEL=%MODELS_PATH%\public\yolov8_license_plate_detector\FP32\yolov8_license_plate_detector.xml
set OCR_CLASSIFICATION_MODEL=%MODELS_PATH%\public\ch_PP-OCRv4_rec_infer\FP32\ch_PP-OCRv4_rec_infer.xml

@REM Check if models exist
if NOT EXIST "%DETECTION_MODEL%" (
    echo [91mERROR: Model not found: %DETECTION_MODEL%[0m
    echo Please run download_public_models.bat to download the models first.
    EXIT /B 1
)

if NOT EXIST "%OCR_CLASSIFICATION_MODEL%" (
    echo [91mERROR: Model not found: %OCR_CLASSIFICATION_MODEL%[0m
    echo Please run download_public_models.bat to download the models first.
    EXIT /B 1
)

@REM Set source element based on input type
set SOURCE_ELEMENT=
echo %INPUT% | findstr /C:"://" >nul
if %ERRORLEVEL%==0 (
    set SOURCE_ELEMENT=urisourcebin buffer-size=4096 uri=%INPUT%
) else (
    set "SOURCE_ELEMENT=filesrc location=%INPUT:\=/%"
)

@REM Set decode and preprocessing based on device
if "%DEVICE%"=="CPU" (
    set DECODE_ELEMENT=decodebin3
    set PREPROC=pre-process-backend=opencv
) else if "%DEVICE%"=="GPU" (
    set DECODE_ELEMENT=decodebin3
    set PREPROC=pre-process-backend=d3d11
) else if "%DEVICE%"=="NPU" (
    set DECODE_ELEMENT=decodebin3
    set PREPROC=pre-process-backend=d3d11
) else (
    echo [91mERROR: Unsupported DEVICE value: %DEVICE%[0m
    echo Supported DEVICE values: CPU, GPU, NPU
    EXIT /B 1
)

@REM Set sink element based on output type
if "%OUTPUT%"=="file" (
    call :set_file_sink
) else if "%OUTPUT%"=="display" (
    if "%DEVICE%"=="CPU" (
        set "SINK_ELEMENT=gvawatermark ! videoconvert ! gvafpscounter ! autovideosink"
    ) else (
        set "SINK_ELEMENT=d3d11convert ! gvawatermark ! videoconvert ! gvafpscounter ! d3d11videosink"
    )
) else if "%OUTPUT%"=="display-async" (
    if "%DEVICE%"=="CPU" (
        set "SINK_ELEMENT=gvawatermark ! videoconvert ! gvafpscounter ! autovideosink sync=false"
    ) else (
        set "SINK_ELEMENT=d3d11convert ! gvawatermark ! videoconvert ! gvafpscounter ! d3d11videosink sync=false"
    )
) else if "%OUTPUT%"=="fps" (
    set "SINK_ELEMENT=gvafpscounter ! fakesink async=false"
) else if "%OUTPUT%"=="json" (
    if EXIST "%JSON_FILE%" del "%JSON_FILE%"
    set "SINK_ELEMENT=gvametaconvert ! gvametapublish file-format=json-lines file-path=%JSON_FILE% ! fakesink async=false"
) else if "%OUTPUT%"=="display-and-json" (
    if EXIST "%JSON_FILE%" del "%JSON_FILE%"
    if "%DEVICE%"=="CPU" (
        set "SINK_ELEMENT=gvawatermark ! gvametaconvert ! gvametapublish file-format=json-lines file-path=%JSON_FILE% ! videoconvert ! gvafpscounter ! autovideosink sync=false"
    ) else (
        set "SINK_ELEMENT=d3d11convert ! gvawatermark ! gvametaconvert ! gvametapublish file-format=json-lines file-path=%JSON_FILE% ! videoconvert ! gvafpscounter ! d3d11videosink sync=false"
    )
) else (
    echo [91mERROR: Invalid OUTPUT parameter[0m
    echo Valid values: display, display-async, fps, json, display-and-json, file
    EXIT /B 1
)
goto :continue_after_sink

:set_file_sink
setlocal EnableDelayedExpansion
for %%F in ("%INPUT%") do set FILENAME=%%~nF
set OUTPUT_FILE=lpr_!FILENAME!_%DEVICE%.mp4
if EXIST "!OUTPUT_FILE!" del "!OUTPUT_FILE!"
if "%DEVICE%"=="CPU" (
    endlocal & set "SINK_ELEMENT=videoconvert ! gvawatermark ! gvafpscounter ! openh264enc ! h264parse ! mp4mux ! filesink location=%OUTPUT_FILE%"
) else (
    endlocal & set "SINK_ELEMENT=d3d11convert ! gvawatermark ! gvafpscounter ! d3d11h264enc ! h264parse ! mp4mux ! filesink location=%OUTPUT_FILE%"
)
exit /b

:continue_after_sink

@REM Convert paths to forward slashes for GStreamer
set DETECTION_MODEL=%DETECTION_MODEL:\=/%
set OCR_CLASSIFICATION_MODEL=%OCR_CLASSIFICATION_MODEL:\=/%

@REM Build and run pipeline
echo.
echo Running pipeline:
echo gst-launch-1.0 %SOURCE_ELEMENT% ^! %DECODE_ELEMENT% ^! queue ^! gvadetect model=%DETECTION_MODEL% device=%DEVICE% %PREPROC% ^! queue ^! videoconvert ^! gvaclassify model=%OCR_CLASSIFICATION_MODEL% device=%DEVICE% %PREPROC% ^! queue ^! %SINK_ELEMENT%
echo.

gst-launch-1.0 %SOURCE_ELEMENT% ! %DECODE_ELEMENT% ! queue ! gvadetect model=%DETECTION_MODEL% device=%DEVICE% %PREPROC% ! queue ! videoconvert ! gvaclassify model=%OCR_CLASSIFICATION_MODEL% device=%DEVICE% %PREPROC% ! queue ! %SINK_ELEMENT%

EXIT /B %ERRORLEVEL%