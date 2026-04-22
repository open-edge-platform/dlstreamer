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

@REM Default values
set MODEL=mask_rcnn_inception_resnet_v2_atrous_coco
set DEVICE=CPU
set INPUT=https://videos.pexels.com/video-files/1192116/1192116-sd_640_360_30fps.mp4
set OUTPUT=file
set JSON_FILE=output.json
set BENCHMARK_SINK=

@REM Parse arguments
if NOT "%~1"=="" (
    if "%~1"=="--help" goto :show_help
    if "%~1"=="-h" goto :show_help
    set MODEL=%~1
)
if NOT "%~2"=="" set DEVICE=%~2
if NOT "%~3"=="" set INPUT=%~3
if NOT "%~4"=="" set OUTPUT=%~4
if NOT "%~5"=="" set JSON_FILE=%~5
if NOT "%~6"=="" set BENCHMARK_SINK=%~6
goto :skip_help

:show_help
echo Usage: instance_segmentation.bat [MODEL] [DEVICE] [INPUT] [OUTPUT] [JSON_FILE] [BENCHMARK_SINK]
echo.
echo Arguments:
echo   MODEL          - Model to use (default: mask_rcnn_inception_resnet_v2_atrous_coco)
echo                    Supported: mask_rcnn_inception_resnet_v2_atrous_coco, mask_rcnn_resnet50_atrous_coco
echo   DEVICE         - Device (default: CPU). Supported: CPU, GPU, NPU
echo   INPUT          - Input source (default: Pexels video URL)
echo   OUTPUT         - Output type (default: file). Supported: file, display, fps, json, display-and-json, jpeg
echo   JSON_FILE      - JSON output file name (default: output.json)
echo   BENCHMARK_SINK - Optional GStreamer element to add after decode (e.g., " ! identity eos-after=100")
echo.
EXIT /B 0

:skip_help

@REM Validate model
if NOT "%MODEL%"=="mask_rcnn_inception_resnet_v2_atrous_coco" if NOT "%MODEL%"=="mask_rcnn_resnet50_atrous_coco" (
    echo [91mERROR: Unsupported model: %MODEL%[0m
    echo Supported models: mask_rcnn_inception_resnet_v2_atrous_coco, mask_rcnn_resnet50_atrous_coco
    EXIT /B 1
)

@REM Validate device
if NOT "%DEVICE%"=="CPU" if NOT "%DEVICE%"=="GPU" if NOT "%DEVICE%"=="NPU" (
    echo [91mERROR: Unsupported device: %DEVICE%[0m
    echo Supported devices: CPU, GPU, NPU
    EXIT /B 1
)

@REM Set model paths
set MODEL_PATH=%MODELS_PATH%\public\%MODEL%\FP16\%MODEL%.xml
set MODEL_PROC=%~dp0..\..\..\..\gstreamer\model_proc\public\mask-rcnn.json

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
    set "SINK_ELEMENT=d3d11convert ! gvawatermark ! videoconvertscale ! gvafpscounter ! d3d11videosink sync=false"
) else if "%OUTPUT%"=="fps" (
    set "SINK_ELEMENT=gvafpscounter ! fakesink sync=false"
) else if "%OUTPUT%"=="json" (
    if EXIST "%JSON_FILE%" del "%JSON_FILE%"
    set "SINK_ELEMENT=gvametaconvert add-tensor-data=true ! gvametapublish file-format=json-lines file-path=%JSON_FILE% ! fakesink sync=false"
) else if "%OUTPUT%"=="display-and-json" (
    if EXIST "%JSON_FILE%" del "%JSON_FILE%"
    set "SINK_ELEMENT=d3d11convert ! gvawatermark ! gvametaconvert add-tensor-data=true ! gvametapublish file-format=json-lines file-path=%JSON_FILE% ! videoconvert ! gvafpscounter ! d3d11videosink sync=false"
) else if "%OUTPUT%"=="jpeg" (
    call :set_jpeg_sink
) else (
    echo [91mERROR: Invalid OUTPUT parameter[0m
    echo Valid values: file, display, fps, json, display-and-json, jpeg
    EXIT /B 1
)
goto :continue_after_sink

:set_file_sink
setlocal EnableDelayedExpansion
for %%F in ("%INPUT%") do set FILENAME=%%~nF
set OUTPUT_FILE=instance_segmentation_!FILENAME!_%DEVICE%.mp4
if EXIST "!OUTPUT_FILE!" del "!OUTPUT_FILE!"
if "%DEVICE%"=="CPU" (
    endlocal & set "SINK_ELEMENT=videoconvert ! gvawatermark ! gvafpscounter ! openh264enc ! h264parse ! mp4mux ! filesink location=%OUTPUT_FILE%"
) else (
    endlocal & set "SINK_ELEMENT=d3d11convert ! gvawatermark ! gvafpscounter ! d3d11h264enc ! h264parse ! mp4mux ! filesink location=%OUTPUT_FILE%"
)
exit /b

:set_jpeg_sink
setlocal EnableDelayedExpansion
for %%F in ("%INPUT%") do set FILENAME=%%~nF
if "%DEVICE%"=="CPU" (
    endlocal & set "SINK_ELEMENT=videoconvert ! gvawatermark ! jpegenc ! multifilesink location=instance_segmentation_%FILENAME%_%DEVICE%_%%05d.jpeg"
) else (
    endlocal & set "SINK_ELEMENT=d3d11convert ! gvawatermark ! videoconvert ! jpegenc ! multifilesink location=instance_segmentation_%FILENAME%_%DEVICE%_%%05d.jpeg"
)
exit /b

:continue_after_sink

@REM Convert paths to forward slashes for GStreamer
set MODEL_PATH=%MODEL_PATH:\=/%
set MODEL_PROC=%MODEL_PROC:\=/%

@REM Build and run pipeline
echo.
echo Running pipeline:
echo gst-launch-1.0 %SOURCE_ELEMENT% ^! %DECODE_ELEMENT%%BENCHMARK_SINK% ^! gvadetect model=%MODEL_PATH% model-proc=%MODEL_PROC% device=%DEVICE% pre-process-backend=%PREPROC_BACKEND% ^! queue ^! %SINK_ELEMENT%
echo.

gst-launch-1.0 %SOURCE_ELEMENT% ! %DECODE_ELEMENT%%BENCHMARK_SINK% ! gvadetect model=%MODEL_PATH% model-proc=%MODEL_PROC% device=%DEVICE% pre-process-backend=%PREPROC_BACKEND% ! queue ! %SINK_ELEMENT%

EXIT /B %ERRORLEVEL%
