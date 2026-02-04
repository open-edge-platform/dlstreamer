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
  echo Usage : benchmark.bat VIDEO_FILE [INFERENCE_DEVICE] [CHANNELS_COUNT]
  echo You can download video from
  echo https://raw.githubusercontent.com/intel-iot-devkit/sample-videos/master/head-pose-face-detection-female-and-male.mp4
  echo and run sample benchmark.bat path\to\your\video\head-pose-face-detection-female-and-male.mp4
  exit /B 1
)

set INFERENCE_DEVICE=%2
if [%INFERENCE_DEVICE%]==[] set INFERENCE_DEVICE=CPU

set CHANNELS_COUNT=%3
if [%CHANNELS_COUNT%]==[] set /A CHANNELS_COUNT=1

set MODEL=face-detection-adas-0001
set "DETECT_MODEL_PATH=%MODELS_PATH%\intel\%MODEL%\FP32\%MODEL%.xml"

@REM correcting paths as in Linux
set VIDEO_FILE_NAME=%VIDEO_FILE_NAME:\=/%
set DETECT_MODEL_PATH=%DETECT_MODEL_PATH:\=/%

setlocal DISABLEDELAYEDEXPANSION
set PIPELINE=filesrc location=%VIDEO_FILE_NAME% ! decodebin3 ! ^
gvadetect model-instance-id=inf0 model="%DETECT_MODEL_PATH%" device=%INFERENCE_DEVICE% ! queue ! ^
gvafpscounter ! fakesink async=false

setlocal ENABLEDELAYEDEXPANSION
set FINAL_PIPELINE_STR=

for /l %%i in (1, 1, %CHANNELS_COUNT%) do set FINAL_PIPELINE_STR=!FINAL_PIPELINE_STR! !PIPELINE!

echo gst-launch-1.0 -v !FINAL_PIPELINE_STR!
gst-launch-1.0 -v !FINAL_PIPELINE_STR!

EXIT /B %ERRORLEVEL%
