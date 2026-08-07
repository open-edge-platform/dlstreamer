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

set MODEL1=centerface
set MODEL2=dima806_facial_age_image_detection
set MODEL3=dima806_fairface_gender_image_detection
set MODEL4=dima806_face_emotions_image_detection

set DETECT_MODEL_PATH=%MODELS_PATH%\public\centerface\FP32\%MODEL1%.xml
set CLASS_MODEL_PATH1=%MODELS_PATH%\public\dima806_facial_age_image_detection\FP32\%MODEL2%.xml
set CLASS_MODEL_PATH2=%MODELS_PATH%\public\dima806_fairface_gender_image_detection\FP32\%MODEL3%.xml
set CLASS_MODEL_PATH3=%MODELS_PATH%\public\dima806_face_emotions_image_detection\FP32\%MODEL4%.xml

set MODEL2_PROC=%~dp0model_proc\%MODEL2%.json
set MODEL3_PROC=%~dp0model_proc\%MODEL3%.json
set MODEL4_PROC=%~dp0model_proc\%MODEL4%.json

@REM correcting paths as in Linux
set DETECT_MODEL_PATH=%DETECT_MODEL_PATH:\=/%
set CLASS_MODEL_PATH=%CLASS_MODEL_PATH:\=/%
set CLASS_MODEL_PATH1=%CLASS_MODEL_PATH1:\=/%
set CLASS_MODEL_PATH2=%CLASS_MODEL_PATH2:\=/%

setlocal DISABLEDELAYEDEXPANSION
set PIPELINE=gst-launch-1.0 -v %SOURCE_ELEMENT% ! decodebin3 ! videoconvert ! ^
gvadetect model="%DETECT_MODEL_PATH%" device=%DEVICE% ! queue ! ^
gvaclassify model="%CLASS_MODEL_PATH1%" device=%DEVICE% ! queue ! ^
gvaclassify model="%CLASS_MODEL_PATH2%" device=%DEVICE% ! queue ! ^
gvaclassify model="%CLASS_MODEL_PATH3%" device=%DEVICE% ! queue ! ^
%SINK_ELEMENT%
setlocal ENABLEDELAYEDEXPANSION

echo !PIPELINE!
!PIPELINE!

EXIT /B %ERRORLEVEL%
