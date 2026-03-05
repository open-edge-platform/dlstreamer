@REM ==============================================================================
@REM Copyright (C) 2026 Intel Corporation
@REM
@REM SPDX-License-Identifier: MIT
@REM ==============================================================================

@echo off

setlocal

set "DEVICE=GPU"
set "SRC=https://videos.pexels.com/video-files/1192116/1192116-sd_640_360_30fps.mp4"
set "MODEL="
set "MODEL_NAME=yolov8n"
set "PRECISION=FP32"
set "MD_OPTS="
set "OUTPUT=json"

:parse_args
if "%~1"=="" goto end_parse
if "%~1"=="--device" (set "DEVICE=%~2" & shift & shift & goto parse_args)
if "%~1"=="--source" (set "SRC=%~2" & shift & shift & goto parse_args)
if "%~1"=="--src"    (set "SRC=%~2" & shift & shift & goto parse_args)
if "%~1"=="--model"  (set "MODEL=%~2" & shift & shift & goto parse_args)
if "%~1"=="--precision" (set "PRECISION=%~2" & shift & shift & goto parse_args)
if "%~1"=="--md-opts" (set "MD_OPTS=%~2" & shift & shift & goto parse_args)
if "%~1"=="--output" (set "OUTPUT=%~2" & shift & shift & goto parse_args)
if "%~1"=="-h" goto usage
if "%~1"=="--help" goto usage
echo Unknown arg: %~1
goto usage
:end_parse

if "%MODEL%"=="" (
    if "%MODELS_PATH%"=="" (
        echo [ERROR] MODELS_PATH not set; export MODELS_PATH or pass --model.
        exit /b 1
    )
    set "MODEL=%MODELS_PATH%/public/%MODEL_NAME%/%PRECISION%/%MODEL_NAME%.xml"
)
set "MODEL=%MODEL:\=/%"

if "%MD_OPTS%"=="" (set "MD_OPTS_CMD=") else (set "MD_OPTS_CMD=%MD_OPTS%")

echo Running gvamotiondetect demo
echo  Device   : %DEVICE%
echo  Source   : %SRC%
echo  Model    : %MODEL%
echo  Precision: %PRECISION%
echo  Output   : %OUTPUT%
echo  MD opts  : %MD_OPTS%
echo.

:: Pipeline
set "BASE_PIPE=urisourcebin uri=%SRC% ! decodebin3"

if /I "%DEVICE%"=="GPU" (
    set "CAPS_STR=video/x-raw(memory:VAMemory)"
    set "DET_OPTS=device=GPU"
) else (
    set "CAPS_STR=video/x-raw"
    set "DET_OPTS=device=CPU pre-process-backend=opencv"
)

if /I "%OUTPUT%"=="json" (
    if exist output.json del output.json
    set "SINK_STR=gvametaconvert format=json ! gvametapublish method=file file-format=json-lines file-path=output.json ! gvafpscounter ! fakesink"
) else (
    set "SINK_STR=gvafpscounter ! gvawatermark ! vapostproc ! d3d11videosink"
)

echo Launching pipeline:
echo gst-launch-1.0 -e %BASE_PIPE% ! %CAPS_STR% ! gvamotiondetect %MD_OPTS_CMD% ! gvadetect model=%MODEL% %DET_OPTS% inference-region=1 ! %SINK_STR%
echo.

gst-launch-1.0 -e %BASE_PIPE% ! %CAPS_STR% ! gvamotiondetect %MD_OPTS_CMD% ! gvadetect model=%MODEL% %DET_OPTS% inference-region=1 ! %SINK_STR%

exit /b %ERRORLEVEL%

:usage
echo Usage: %~nx0 [--device GPU^|CPU] [--source ^<video^|uri^>] [--model ^<xml^>] [--precision FP32^|FP16^|INT8] [--output display^|json] [--md-opts "prop1=val prop2=val"]
exit /b 0