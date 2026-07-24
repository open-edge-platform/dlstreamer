#!/bin/bash
# ==============================================================================
# Copyright (C) 2018-2025 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================
set -e

INPUT=${1:-https://github.com/intel-iot-devkit/sample-videos/raw/master/head-pose-face-detection-female-and-male.mp4}
OUTPUT=${2:-display} # Supported values: display, fps, json, display-and-json, file

if [[ $INPUT == "/dev/video"* ]]; then
  SOURCE_ELEMENT="v4l2src device=${INPUT}"
elif [[ $INPUT == *"://"* ]]; then
  SOURCE_ELEMENT="urisourcebin buffer-size=4096 uri=${INPUT}"
else
  SOURCE_ELEMENT="filesrc location=${INPUT}"
fi

MODEL1_PATH=${MODELS_PATH}/public/centerface/FP32/centerface.xml
MODEL2_PATH=${MODELS_PATH}/public/dima806_facial_age_image_detection/FP32/dima806_facial_age_image_detection.xml
MODEL3_PATH=${MODELS_PATH}/public/dima806_fairface_gender_image_detection/FP32/dima806_fairface_gender_image_detection.xml
MODEL4_PATH=${MODELS_PATH}/public/dima806_face_emotions_image_detection/FP32/dima806_face_emotions_image_detection.xml

if [[ $OUTPUT == "display" ]] || [[ -z $OUTPUT ]]; then
  SINK_ELEMENT="autovideosink sync=false"
elif [[ $OUTPUT == "fps" ]]; then
  SINK_ELEMENT="gvafpscounter ! fakesink async=false "
elif [[ $OUTPUT == "json" ]]; then
  rm -f output.json
  SINK_ELEMENT="gvametaconvert ! gvametapublish file-format=json-lines file-path=output.json ! fakesink async=false "
elif [[ $OUTPUT == "display-and-json" ]]; then
  rm -f output.json
  SINK_ELEMENT="gvametaconvert ! gvametapublish file-format=json-lines file-path=output.json ! videoconvert ! gvafpscounter ! autovideosink sync=false"
elif [[ $OUTPUT == "file" ]]; then
  FILE="$(basename ${INPUT%.*})"
  rm -f "${FILE}.mp4"
    if [[ $(gst-inspect-1.0 va | grep vah264enc) ]]; then
    ENCODER="vah264enc"
  elif [[ $(gst-inspect-1.0 va | grep vah264lpenc) ]]; then
    ENCODER="vah264lpenc"
  else
    echo "Error - VA-API H.264 encoder not found."
    exit
  fi
  SINK_ELEMENT="gvawatermark ! gvafpscounter ! ${ENCODER} ! avimux name=mux ! filesink location=${FILE}.mp4"
else
  echo Error wrong value for OUTPUT parameter
  echo Valid values: "file" - render to file, "display" - render to screen, "fps" - print FPS, "json" - write to output.json, "display-and-json" - render to screen and write to output.json
  exit
fi

gst-launch-1.0 \
$SOURCE_ELEMENT ! \
decodebin3 ! \
processbin \
    preprocess="videoscale ! videoconvert ! video/x-raw,format=BGRP ! tensor_convert" \
    process="openvino_tensor_inference model=$MODEL1_PATH device=CPU" \
    postprocess="tensor_postproc_detection threshold=0.5" \
    aggregate="meta_aggregate attach-tensor-data=false" ! \
processbin \
    preprocess="roi_split ! videoconvert ! opencv_cropscale ! videoconvert ! video/x-raw,format=BGRP ! tensor_convert" \
    process="openvino_tensor_inference model=$MODEL2_PATH device=CPU" \
    postprocess="tensor_postproc_label layer-name=prob attribute-name=gender labels=<Female,Male> ! tensor_postproc_text layer-name=age_conv3 text-scale=100" \
    aggregate=meta_aggregate ! \
processbin \
    preprocess="roi_split ! videoconvert ! opencv_cropscale ! videoconvert ! video/x-raw,format=BGRP ! tensor_convert" \
    process="openvino_tensor_inference model=$MODEL3_PATH device=CPU" \
    postprocess="tensor_postproc_label labels=<neutral,happy,sad,surprise,anger> attribute-name=emotion method=max" \
    aggregate=meta_aggregate ! \
processbin \
    preprocess="roi_split ! videoconvert ! opencv_cropscale ! videoconvert ! video/x-raw,format=BGRP ! tensor_convert" \
    process="openvino_tensor_inference model=$MODEL4_PATH device=CPU" \
    postprocess="tensor_postproc_add_params format=landmark_points" \
    aggregate=meta_aggregate ! \
meta_overlay ! \
videoconvert ! \
gvafpscounter ! \
$SINK_ELEMENT
