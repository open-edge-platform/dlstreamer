@REM ==============================================================================
@REM Copyright (C) 2026 Intel Corporation
@REM
@REM SPDX-License-Identifier: MIT
@REM ==============================================================================

@echo off

setlocal

set INPUT=%1
if [%INPUT%]==[] set INPUT=https://github.com/intel-iot-devkit/sample-videos/raw/master/head-pose-face-detection-female-and-male.mp4

set DEVICE=%2
if [%DEVICE%]==[] set DEVICE=CPU

set OUTPUT=%3
if [%OUTPUT%]==[] set OUTPUT=display

set JSON_FILE=%4
if [%JSON_FILE%]==[] set JSON_FILE=output.json

if "%OUTPUT%"=="json" (
    set "SINK_ELEMENT=gvametaconvert ! gvametapublish file-format=json-lines file-path=%JSON_FILE% ! fakesink async=false"
) else if "%OUTPUT%"=="fps" (
    set "SINK_ELEMENT=gvafpscounter ! fakesink async=false"
) else (
    set "SINK_ELEMENT=gvawatermark ! videoconvert ! autovideosink sync=false"
)

setlocal EnableDelayedExpansion
if NOT x%INPUT:?\\usb\#=%==x%INPUT% (
  set SOURCE_ELEMENT=ksvideosrc device-path=%INPUT%
) else (
    if NOT x%INPUT:://=%==x%INPUT% (
      set SOURCE_ELEMENT=urisourcebin buffer-size=4096 uri=%INPUT%
    ) else (
      set INPUT=%INPUT:\=/%
      set SOURCE_ELEMENT=filesrc location=!INPUT!
    )
)

set MODEL1=face-detection-adas-0001
set MODEL2=age-gender-recognition-retail-0013
set MODEL3=emotions-recognition-retail-0003
set MODEL4=landmarks-regression-retail-0009

set DETECT_MODEL_PATH=%MODELS_PATH%\intel\face-detection-adas-0001\FP32\%MODEL1%.xml
set CLASS_MODEL_PATH=%MODELS_PATH%\intel\age-gender-recognition-retail-0013\FP32\%MODEL2%.xml
set CLASS_MODEL_PATH1=%MODELS_PATH%\intel\emotions-recognition-retail-0003\FP32\%MODEL3%.xml
set CLASS_MODEL_PATH2=%MODELS_PATH%\intel\landmarks-regression-retail-0009\FP32\%MODEL4%.xml

set MODEL2_PROC=%~dp0model_proc\%MODEL2%.json
set MODEL3_PROC=%~dp0model_proc\%MODEL3%.json
set MODEL4_PROC=%~dp0model_proc\%MODEL4%.json

@REM correcting paths as in Linux
set DETECT_MODEL_PATH=%DETECT_MODEL_PATH:\=/%
set CLASS_MODEL_PATH=%CLASS_MODEL_PATH:\=/%
set CLASS_MODEL_PATH1=%CLASS_MODEL_PATH1:\=/%
set CLASS_MODEL_PATH2=%CLASS_MODEL_PATH2:\=/%

set MODEL2_PROC=%MODEL2_PROC:\=/%
set MODEL3_PROC=%MODEL3_PROC:\=/%
set MODEL4_PROC=%MODEL4_PROC:\=/%

setlocal DISABLEDELAYEDEXPANSION
set PIPELINE=gst-launch-1.0 -v %SOURCE_ELEMENT% ! decodebin3 ! videoconvert ! ^
gvadetect model="%DETECT_MODEL_PATH%" device=%DEVICE% ! queue ! ^
gvaclassify model="%CLASS_MODEL_PATH%" model-proc="%MODEL2_PROC%" device=%DEVICE% ! queue ! ^
gvaclassify model="%CLASS_MODEL_PATH1%" model-proc="%MODEL3_PROC%" device=%DEVICE% ! queue ! ^
gvaclassify model="%CLASS_MODEL_PATH2%" model-proc="%MODEL4_PROC%" device=%DEVICE% ! queue ! ^
%SINK_ELEMENT%
setlocal ENABLEDELAYEDEXPANSION

echo !PIPELINE!
!PIPELINE!

EXIT /B %ERRORLEVEL%
