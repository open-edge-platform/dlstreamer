@REM ==============================================================================
@REM Copyright (C) 2026 Intel Corporation
@REM
@REM SPDX-License-Identifier: MIT
@REM ==============================================================================
@echo off
setlocal

set VIDEO_FILE_NAME=%1
if [%VIDEO_FILE_NAME%]==[] (
  echo ERROR: Set path to video.
  exit /B 1
)

set INFERENCE_DEVICE=%2
if [%INFERENCE_DEVICE%]==[] set INFERENCE_DEVICE=CPU

set CHANNELS_COUNT=%3
if [%CHANNELS_COUNT%]==[] set /A CHANNELS_COUNT=1

set OUTPUT_FILE=%4

set MODEL=face-detection-adas-0001
set "DETECT_MODEL_PATH=%MODELS_PATH%\intel\%MODEL%\FP32\%MODEL%.xml"

set VIDEO_FILE_NAME=%VIDEO_FILE_NAME:\=/%
set DETECT_MODEL_PATH=%DETECT_MODEL_PATH:\=/%

setlocal DISABLEDELAYEDEXPANSION
if [%OUTPUT_FILE%]==[] (
    set "SINK_SECTION=gvafpscounter ! fakesink async=false"
) else (
    set "SINK_SECTION=gvametaconvert ! gvametapublish file-format=json-lines file-path=%OUTPUT_FILE% ! fakesink async=false"
)

set PIPELINE=filesrc location=%VIDEO_FILE_NAME% ! decodebin3 ! ^
gvadetect model-instance-id=inf0 model="%DETECT_MODEL_PATH%" device=%INFERENCE_DEVICE% ! queue ! ^
%SINK_SECTION%

setlocal ENABLEDELAYEDEXPANSION
set FINAL_PIPELINE_STR=

for /l %%i in (1, 1, %CHANNELS_COUNT%) do set FINAL_PIPELINE_STR=!FINAL_PIPELINE_STR! !PIPELINE!

echo gst-launch-1.0 -v !FINAL_PIPELINE_STR!
gst-launch-1.0 -v !FINAL_PIPELINE_STR!

EXIT /B %ERRORLEVEL%
