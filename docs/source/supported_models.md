# Supported Models

This page lists models supported by Intel® DL Streamer.

The following models used in DL Streamer demos can be conveniently downloaded and converted using the [`download_public_models.sh`]() script:

<table>
  <tr>
    <th>Category</th>
    <th>Model Name</th>
    <th>Demo App</th>
  </tr>
  <tr>
    <td>Detection</td>
    <td><a href="https://github.com/Star-Clouds/CenterFace/tree/master">centerface</a></td>
    <td rowspan="2"><a href="https://github.com/open-edge-platform/dlstreamer/tree/main/samples/gstreamer/gst_launch/custom_postproc/classify">Custom Post-Processing Library Sample - Classification</a></td>
  </tr>
  <tr>
    <td>Emotion Recognition</td>
    <td><a href="https://github.com/av-savchenko/face-emotion-recognition/tree/main">hsemotion</a></td>
  </tr>
  <tr>
    <td>Feature Extraction</td>
    <td><a href="https://github.com/ZQPei/deep_sort_pytorch">mars-small128</a></td>
    <td><a href="https://github.com/open-edge-platform/dlstreamer/blob/main/docs/source/dev_guide/object_tracking.md#deep-sort-tracking">Deep SORT Tracking</a></td>
  </tr>
  <tr>
    <td>Optical Character Recognition</td>
    <td><a href="https://github.com/PaddlePaddle/PaddleOCR">ch_PP-OCRv4_rec_infer</a></td>
    <td><a href="https://github.com/open-edge-platform/dlstreamer/tree/main/samples/gstreamer/gst_launch/license_plate_recognition">License Plate Recognition Sample</a></td>
  </tr>
</table>

<table>
  <tr>
    <th>Category</th>
    <th>Model Name</th>
    <th>labels-file</th>
    <th>model-proc</th>
    <th>Demo App</th>
  </tr>
  <tr>
    <td style="vertical-align:top;" rowspan="3">Action Recognition</td>
    <td><a href="https://github.com/openvinotoolkit/open_model_zoo/tree/master/models/intel/action-recognition-0001">action-recognition-0001</a></td>
    <td><a href="https://github.com/open-edge-platform/dlstreamer/blob/main/samples/labels/kinetics_400.txt">kinetics_400.txt</a></td>
    <td>&nbsp;</td>
    <td rowspan="3"><a href="https://github.com/openvinotoolkit/open_model_zoo/tree/master/demos/action_recognition_demo/python">Action Recognition Demo</a></td>
  </tr>
  <tr>
    <td><a href="https://github.com/openvinotoolkit/open_model_zoo/tree/master/models/intel/driver-action-recognition-adas-0002">driver-action-recognition-adas-0002</a></td>
    <td><a href="https://github.com/open-edge-platform/dlstreamer/blob/main/samples/labels/driver_actions.txt">driver_actions.txt</a></td>
    <td>&nbsp;</td>
  </tr>
  <tr>
    <td><a href="https://github.com/openvinotoolkit/open_model_zoo/tree/master/models/intel/weld-porosity-detection-0001">weld-porosity-detection-0001</a></td>
    <td>&nbsp;</td>
    <td><a href="https://github.com/open-edge-platform/dlstreamer/blob/main/samples/gstreamer/model_proc/intel/weld-porosity-detection-0001.json">weld-porosity-detection-0001.json</a></td>
  </tr>
  <tr>
    <td style="vertical-align:top;" rowspan="41">Classification</td>
    <td><a href="https://github.com/openvinotoolkit/open_model_zoo/tree/master/models/public/anti-spoof-mn3">anti-spoof-mn3</a></td>
    <td>&nbsp;</td>
    <td><a href="https://github.com/open-edge-platform/dlstreamer/blob/main/samples/gstreamer/model_proc/public/anti-spoof-mn3.json">anti-spoof-mn3.json</a></td>
    <td><a href="https://github.com/openvinotoolkit/open_model_zoo/tree/master/demos/interactive_face_detection_demo/cpp_gapi">Interactive Face Detection Demo</a></td>
  </tr>
  <tr>
    <td><a href="https://github.com/openvinotoolkit/open_model_zoo/tree/master/models/public/densenet-121-tf">densenet-121-tf</a></td>
    <td rowspan="6"><a href="https://github.com/open-edge-platform/dlstreamer/blob/main/samples/labels/imagenet_2012.txt">imagenet_2012.txt</a></td>
    <td rowspan="6"><a href="https://github.com/open-edge-platform/dlstreamer/blob/main/samples/gstreamer/model_proc/public/preproc-aspect-ratio.json">preproc-aspect-ratio.json</a></td>
    <td rowspan="6"><a href="https://github.com/openvinotoolkit/open_model_zoo/tree/master/demos/classification_demo/python">Classification Demo</a></td>
  </tr>
  <tr>
    <td><a href="https://github.com/openvinotoolkit/open_model_zoo/tree/master/models/public/dla-34">dla-34</a></td>
  </tr>
  <tr>
    <td><a href="https://github.com/openvinotoolkit/open_model_zoo/tree/master/models/public/efficientnet-b0">efficientnet-b0</a></td>
  </tr>
  <tr>
    <td><a href="https://github.com/openvinotoolkit/open_model_zoo/tree/master/models/public/efficientnet-b0-pytorch">efficientnet-b0-pytorch</a></td>
  </tr>
  <tr>
    <td><a href="https://github.com/openvinotoolkit/open_model_zoo/tree/master/models/public/efficientnet-v2-b0">efficientnet-v2-b0</a></td>
  </tr>
  <tr>
    <td><a href="https://github.com/openvinotoolkit/open_model_zoo/tree/master/models/public/efficientnet-v2-s">efficientnet-v2-s</a></td>
  </tr>
  <tr>
    <td><a href="https://github.com/onnx/models/tree/main/validated/vision/body_analysis/emotion_ferplus">emotion-ferplus-8</a></td>
    <td>&nbsp;</td>
    <td><a href="https://github.com/open-edge-platform/dlstreamer/blob/main/samples/gstreamer/model_proc/onnx/emotion-ferplus-8.json">emotion-ferplus-8.json</a></td>
    <td>&nbsp;</td>
  </tr>
  <tr>
    <td><a href="https://github.com/openvinotoolkit/open_model_zoo/tree/master/models/public/googlenet-v1-tf">googlenet-v1-tf</a></td>
    <td><a href="https://github.com/open-edge-platform/dlstreamer/blob/main/samples/labels/imagenet_2012.txt">imagenet_2012.txt</a></td>
    <td rowspan="16"><a href="https://github.com/open-edge-platform/dlstreamer/blob/main/samples/gstreamer/model_proc/public/preproc-aspect-ratio.json">preproc-aspect-ratio.json</a></td>
    <td rowspan="16"><a href="https://github.com/openvinotoolkit/open_model_zoo/tree/master/demos/classification_demo/python">Classification Demo</a></td>
  </tr>
  <tr>
    <td><a href="https://github.com/openvinotoolkit/open_model_zoo/tree/master/models/public/googlenet-v2-tf">googlenet-v2-tf</a></td>
    <td rowspan="2"><a href="https://github.com/open-edge-platform/dlstreamer/blob/main/samples/labels/imagenet_2012_bkgr.txt">imagenet_2012_bkgr.txt</a></td>
  </tr>
  <tr>
    <td><a href="https://github.com/openvinotoolkit/open_model_zoo/tree/master/models/public/googlenet-v3">googlenet-v3</a></td>
  </tr>
  <tr>
    <td><a href="https://github.com/openvinotoolkit/open_model_zoo/tree/master/models/public/googlenet-v3-pytorch">googlenet-v3-pytorch</a></td>
    <td><a href="https://github.com/open-edge-platform/dlstreamer/blob/main/samples/labels/imagenet_2012.txt">imagenet_2012.txt</a></td>
  </tr>
  <tr>
    <td><a href="https://github.com/openvinotoolkit/open_model_zoo/tree/master/models/public/googlenet-v4-tf">googlenet-v4-tf</a></td>
    <td><a href="https://github.com/open-edge-platform/dlstreamer/blob/main/samples/labels/imagenet_2012_bkgr.txt">imagenet_2012_bkgr.txt</a></td>
  </tr>
  <tr>
    <td><a href="https://github.com/openvinotoolkit/open_model_zoo/tree/master/models/public/hbonet-0.25">hbonet-0.25</a></td>
    <td rowspan="2"><a href="https://github.com/open-edge-platform/dlstreamer/blob/main/samples/labels/imagenet_2012.txt">imagenet_2012.txt</a></td>
  </tr>
  <tr>
    <td><a href="https://github.com/openvinotoolkit/open_model_zoo/tree/master/models/public/hbonet-1.0">hbonet-1.0</a></td>
  </tr>
  <tr>
    <td><a href="https://github.com/openvinotoolkit/open_model_zoo/tree/master/models/public/inception-resnet-v2-tf">inception-resnet-v2-tf</a></td>
    <td><a href="https://github.com/open-edge-platform/dlstreamer/blob/main/samples/labels/imagenet_2012_bkgr.txt">imagenet_2012_bkgr.txt</a></td>
  </tr>
  <tr>
    <td><a href="https://github.com/openvinotoolkit/open_model_zoo/tree/master/models/public/mixnet-l">mixnet-l</a></td>
    <td><a href="https://github.com/open-edge-platform/dlstreamer/blob/main/samples/labels/imagenet_2012.txt">imagenet_2012.txt</a></td>
  </tr>
  <tr>
    <td><a href="https://github.com/openvinotoolkit/open_model_zoo/tree/master/models/public/mobilenet-v1-0.25-128">mobilenet-v1-0.25-128</a></td>
    <td rowspan="4"><a href="https://github.com/open-edge-platform/dlstreamer/blob/main/samples/labels/imagenet_2012_bkgr.txt">imagenet_2012_bkgr.txt</a></td>
  </tr>
  <tr>
    <td><a href="https://github.com/openvinotoolkit/open_model_zoo/tree/master/models/public/mobilenet-v1-1.0-224-tf">mobilenet-v1-1.0-224-tf</a></td>
  </tr>
  <tr>
    <td><a href="https://github.com/openvinotoolkit/open_model_zoo/tree/master/models/public/mobilenet-v2-1.0-224">mobilenet-v2-1.0-224</a></td>
  </tr>
  <tr>
    <td><a href="https://github.com/openvinotoolkit/open_model_zoo/tree/master/models/public/mobilenet-v2-1.4-224">mobilenet-v2-1.4-224</a></td>
  </tr>
  <tr>
    <td><a href="https://github.com/openvinotoolkit/open_model_zoo/tree/master/models/public/mobilenet-v2-pytorch">mobilenet-v2-pytorch</a></td>
    <td rowspan="3"><a href="https://github.com/open-edge-platform/dlstreamer/blob/main/samples/labels/imagenet_2012.txt">imagenet_2012.txt</a></td>
  </tr>
  <tr>
    <td><a href="https://github.com/openvinotoolkit/open_model_zoo/tree/master/models/public/mobilenet-v3-large-1.0-224-tf">mobilenet-v3-large-1.0-224-tf</a></td>
  </tr>
  <tr>
    <td><a href="https://github.com/openvinotoolkit/open_model_zoo/tree/master/models/public/mobilenet-v3-small-1.0-224-tf">mobilenet-v3-small-1.0-224-tf</a></td>
  </tr>
  <tr>
    <td><a href="https://github.com/onnx/models/tree/main/validated/vision/classification/mobilenet">mobilenetv2-7</a></td>
    <td>&nbsp;</td>
    <td><a href="https://github.com/open-edge-platform/dlstreamer/blob/main/samples/gstreamer/model_proc/onnx/mobilenetv2-7.json">mobilenetv2-7.json</a></td>
    <td>&nbsp;</td>
  </tr>
  <tr>
    <td><a href="https://github.com/openvinotoolkit/open_model_zoo/tree/master/models/public/nfnet-f0">nfnet-f0</a></td>
    <td><a href="https://github.com/open-edge-platform/dlstreamer/blob/main/samples/labels/imagenet_2012.txt">imagenet_2012.txt</a></td>
    <td><a href="https://github.com/open-edge-platform/dlstreamer/blob/main/samples/gstreamer/model_proc/public/preproc-aspect-ratio.json">preproc-aspect-ratio.json</a></td>
    <td><a href="https://github.com/openvinotoolkit/open_model_zoo/tree/master/demos/classification_demo/python">Classification Demo</a></td>
  </tr>
  <tr>
    <td><a href="https://github.com/openvinotoolkit/open_model_zoo/tree/master/models/public/open-closed-eye-0001">open-closed-eye-0001</a></td>
    <td>&nbsp;</td>
    <td><a href="https://github.com/open-edge-platform/dlstreamer/blob/main/samples/gstreamer/model_proc/public/open-closed-eye-0001.json">open-closed-eye-0001.json</a></td>
    <td><a href="https://github.com/openvinotoolkit/open_model_zoo/tree/master/demos/gaze_estimation_demo/cpp_gapi">Gaze Estimation Demo</a></td>
  </tr>
  <tr>
    <td><a href="https://github.com/openvinotoolkit/open_model_zoo/tree/master/models/public/regnetx-3.2gf">regnetx-3.2gf</a></td>
    <td rowspan="8"><a href="https://github.com/open-edge-platform/dlstreamer/blob/main/samples/labels/imagenet_2012.txt">imagenet_2012.txt</a></td>
    <td rowspan="9"><a href="https://github.com/open-edge-platform/dlstreamer/blob/main/samples/gstreamer/model_proc/public/preproc-aspect-ratio.json">preproc-aspect-ratio.json</a></td>
    <td rowspan="14"><a href="https://github.com/openvinotoolkit/open_model_zoo/tree/master/demos/classification_demo/python">Classification Demo</a></td>
  </tr>
  <tr>
    <td><a href="https://github.com/openvinotoolkit/open_model_zoo/tree/master/models/public/repvgg-a0">repvgg-a0</a></td>
  </tr>
  <tr>
    <td><a href="https://github.com/openvinotoolkit/open_model_zoo/tree/master/models/public/repvgg-b1">repvgg-b1</a></td>
  </tr>
  <tr>
    <td><a href="https://github.com/openvinotoolkit/open_model_zoo/tree/master/models/public/repvgg-b3">repvgg-b3</a></td>
  </tr>
  <tr>
    <td><a href="https://github.com/openvinotoolkit/open_model_zoo/tree/master/models/public/resnest-50-pytorch">resnest-50-pytorch</a></td>
  </tr>
  <tr>
    <td><a href="https://github.com/openvinotoolkit/open_model_zoo/tree/master/models/public/resnet-18-pytorch">resnet-18-pytorch</a></td>
  </tr>
  <tr>
    <td><a href="https://github.com/openvinotoolkit/open_model_zoo/tree/master/models/public/resnet-34-pytorch">resnet-34-pytorch</a></td>
  </tr>
  <tr>
    <td><a href="https://github.com/openvinotoolkit/open_model_zoo/tree/master/models/public/resnet-50-pytorch">resnet-50-pytorch</a></td>
  </tr>
  <tr>
    <td><a href="https://github.com/openvinotoolkit/open_model_zoo/tree/master/models/public/resnet-50-tf">resnet-50-tf</a></td>
    <td><a href="https://github.com/open-edge-platform/dlstreamer/blob/main/samples/labels/imagenet_2012_bkgr.txt">imagenet_2012_bkgr.txt</a></td>
  </tr>
  <tr>
    <td><a href="https://github.com/openvinotoolkit/open_model_zoo/tree/master/models/intel/resnet18-xnor-binary-onnx-0001">resnet18-xnor-binary-onnx-0001</a></td>
    <td>&nbsp;</td>
    <td><a href="https://github.com/open-edge-platform/dlstreamer/blob/main/samples/gstreamer/model_proc/intel/resnet18-xnor-binary-onnx-0001.json">resnet18-xnor-binary-onnx-0001.json</a></td>
  </tr>
  <tr>
    <td><a href="https://github.com/openvinotoolkit/open_model_zoo/tree/master/models/intel/resnet50-binary-0001">resnet50-binary-0001</a></td>
    <td>&nbsp;</td>
    <td><a href="https://github.com/open-edge-platform/dlstreamer/blob/main/samples/gstreamer/model_proc/intel/resnet50-binary-0001.json">resnet50-binary-0001.json</a></td>
  </tr>
  <tr>
    <td><a href="https://github.com/openvinotoolkit/open_model_zoo/tree/master/models/public/rexnet-v1-x1.0">rexnet-v1-x1.0</a></td>
    <td rowspan="3"><a href="https://github.com/open-edge-platform/dlstreamer/blob/main/samples/labels/imagenet_2012.txt">imagenet_2012.txt</a></td>
    <td rowspan="3"><a href="https://github.com/open-edge-platform/dlstreamer/blob/main/samples/gstreamer/model_proc/public/preproc-aspect-ratio.json">preproc-aspect-ratio.json</a></td>
  </tr>
  <tr>
    <td><a href="https://github.com/openvinotoolkit/open_model_zoo/tree/master/models/public/shufflenet-v2-x1.0">shufflenet-v2-x1.0</a></td>
  </tr>
  <tr>
    <td><a href="https://github.com/openvinotoolkit/open_model_zoo/tree/master/models/public/swin-tiny-patch4-window7-224">swin-tiny-patch4-window7-224</a></td>
  </tr>
  <tr>
    <td style="vertical-align:top;" rowspan="47">Detection</td>
    <td><a href="https://github.com/openvinotoolkit/open_model_zoo/tree/master/models/public/efficientdet-d0-tf">efficientdet-d0-tf</a></td>
    <td rowspan="2"><a href="https://github.com/open-edge-platform/dlstreamer/blob/main/samples/labels/coco_91cl.txt">coco_91cl.txt</a></td>
    <td>&nbsp;</td>
    <td rowspan="12"><a href="https://github.com/openvinotoolkit/open_model_zoo/tree/master/demos/object_detection_demo/cpp">Object Detection Demo</a></td>
  </tr>
  <tr>
    <td><a href="https://github.com/openvinotoolkit/open_model_zoo/tree/master/models/public/efficientdet-d1-tf">efficientdet-d1-tf</a></td>
    <td>&nbsp;</td>
  </tr>
  <tr>
    <td><a href="https://github.com/openvinotoolkit/open_model_zoo/tree/master/models/intel/face-detection-0200">face-detection-0200</a></td>
    <td>&nbsp;</td>
    <td><a href="https://github.com/open-edge-platform/dlstreamer/blob/main/samples/gstreamer/model_proc/intel/face-detection-0200.json">face-detection-0200.json</a></td>
  </tr>
  <tr>
    <td><a href="https://github.com/openvinotoolkit/open_model_zoo/tree/master/models/intel/face-detection-0202">face-detection-0202</a></td>
    <td>&nbsp;</td>
    <td><a href="https://github.com/open-edge-platform/dlstreamer/blob/main/samples/gstreamer/model_proc/intel/face-detection-0202.json">face-detection-0202.json</a></td>
  </tr>
  <tr>
    <td><a href="https://github.com/openvinotoolkit/open_model_zoo/tree/master/models/intel/face-detection-0204">face-detection-0204</a></td>
    <td>&nbsp;</td>
    <td><a href="https://github.com/open-edge-platform/dlstreamer/blob/main/samples/gstreamer/model_proc/intel/face-detection-0204.json">face-detection-0204.json</a></td>
  </tr>
  <tr>
    <td><a href="https://github.com/openvinotoolkit/open_model_zoo/tree/master/models/intel/face-detection-0205">face-detection-0205</a></td>
    <td>&nbsp;</td>
    <td><a href="https://github.com/open-edge-platform/dlstreamer/blob/main/samples/gstreamer/model_proc/intel/face-detection-0205.json">face-detection-0205.json</a></td>
  </tr>
  <tr>
    <td><a href="https://github.com/openvinotoolkit/open_model_zoo/tree/master/models/intel/face-detection-0206">face-detection-0206</a></td>
    <td>&nbsp;</td>
    <td><a href="https://github.com/open-edge-platform/dlstreamer/blob/main/samples/gstreamer/model_proc/intel/face-detection-0206.json">face-detection-0206.json</a></td>
  </tr>
  <tr>
    <td><a href="https://github.com/openvinotoolkit/open_model_zoo/tree/master/models/intel/face-detection-adas-0001">face-detection-adas-0001</a></td>
    <td>&nbsp;</td>
    <td><a href="https://github.com/open-edge-platform/dlstreamer/blob/main/samples/gstreamer/model_proc/intel/face-detection-adas-0001.json">face-detection-adas-0001.json</a></td>
  </tr>
  <tr>
    <td><a href="https://github.com/openvinotoolkit/open_model_zoo/tree/master/models/intel/face-detection-retail-0004">face-detection-retail-0004</a></td>
    <td>&nbsp;</td>
    <td><a href="https://github.com/open-edge-platform/dlstreamer/blob/main/samples/gstreamer/model_proc/intel/face-detection-retail-0004.json">face-detection-retail-0004.json</a></td>
  </tr>
  <tr>
    <td><a href="https://github.com/openvinotoolkit/open_model_zoo/tree/master/models/intel/face-detection-retail-0005">face-detection-retail-0005</a></td>
    <td>&nbsp;</td>
    <td><a href="https://github.com/open-edge-platform/dlstreamer/blob/main/samples/gstreamer/model_proc/intel/face-detection-retail-0005.json">face-detection-retail-0005.json</a></td>
  </tr>
  <tr>
    <td><a href="https://github.com/openvinotoolkit/open_model_zoo/tree/master/models/public/faster_rcnn_inception_resnet_v2_atrous_coco">faster_rcnn_inception_resnet_v2_atrous_coco</a></td>
    <td rowspan="2"><a href="https://github.com/open-edge-platform/dlstreamer/blob/main/samples/labels/coco_91cl_bkgr.txt">coco_91cl_bkgr.txt</a></td>
    <td rowspan="2"><a href="https://github.com/open-edge-platform/dlstreamer/blob/main/samples/gstreamer/model_proc/public/preproc-image-info.json">preproc-image-info.json</a></td>
  </tr>
  <tr>
    <td><a href="https://github.com/openvinotoolkit/open_model_zoo/tree/master/models/public/faster_rcnn_resnet50_coco">faster_rcnn_resnet50_coco</a></td>
  </tr>
  <tr>
    <td><a href="https://github.com/openvinotoolkit/open_model_zoo/tree/master/models/intel/horizontal-text-detection-0001">horizontal-text-detection-0001</a></td>
    <td>&nbsp;</td>
    <td><a href="https://github.com/open-edge-platform/dlstreamer/blob/main/samples/gstreamer/model_proc/intel/horizontal-text-detection-0001.json">horizontal-text-detection-0001.json</a></td>
    <td><a href="https://github.com/openvinotoolkit/open_model_zoo/tree/master/demos/text_detection_demo/cpp">Text Detection Demo</a></td>
  </tr>
  <tr>
    <td><a href="https://github.com/openvinotoolkit/open_model_zoo/tree/master/models/public/mobilenet-yolo-v4-syg">mobilenet-yolo-v4-syg</a></td>
    <td>&nbsp;</td>
    <td><a href="https://github.com/open-edge-platform/dlstreamer/blob/main/samples/gstreamer/model_proc/public/mobilenet-yolo-v4-syg.json">mobilenet-yolo-v4-syg.json</a></td>
    <td rowspan="23"><a href="https://github.com/openvinotoolkit/open_model_zoo/tree/master/demos/object_detection_demo/cpp">Object Detection Demo</a></td>
  </tr>
  <tr>
    <td><a href="https://github.com/openvinotoolkit/open_model_zoo/tree/master/models/intel/pedestrian-and-vehicle-detector-adas-0001">pedestrian-and-vehicle-detector-adas-0001</a></td>
    <td>&nbsp;</td>
    <td><a href="https://github.com/open-edge-platform/dlstreamer/blob/main/samples/gstreamer/model_proc/intel/pedestrian-and-vehicle-detector-adas-0001.json">pedestrian-and-vehicle-detector-adas-0001.json</a></td>
  </tr>
  <tr>
    <td><a href="https://github.com/openvinotoolkit/open_model_zoo/tree/master/models/intel/pedestrian-detection-adas-0002">pedestrian-detection-adas-0002</a></td>
    <td>&nbsp;</td>
    <td><a href="https://github.com/open-edge-platform/dlstreamer/blob/main/samples/gstreamer/model_proc/intel/pedestrian-detection-adas-0002.json">pedestrian-detection-adas-0002.json</a></td>
  </tr>
  <tr>
    <td><a href="https://github.com/openvinotoolkit/open_model_zoo/tree/master/models/intel/person-detection-0200">person-detection-0200</a></td>
    <td>&nbsp;</td>
    <td><a href="https://github.com/open-edge-platform/dlstreamer/blob/main/samples/gstreamer/model_proc/intel/person-detection-0200.json">person-detection-0200.json</a></td>
  </tr>
  <tr>
    <td><a href="https://github.com/openvinotoolkit/open_model_zoo/tree/master/models/intel/person-detection-0201">person-detection-0201</a></td>
    <td>&nbsp;</td>
    <td><a href="https://github.com/open-edge-platform/dlstreamer/blob/main/samples/gstreamer/model_proc/intel/person-detection-0201.json">person-detection-0201.json</a></td>
  </tr>
  <tr>
    <td><a href="https://github.com/openvinotoolkit/open_model_zoo/tree/master/models/intel/person-detection-0202">person-detection-0202</a></td>
    <td>&nbsp;</td>
    <td><a href="https://github.com/open-edge-platform/dlstreamer/blob/main/samples/gstreamer/model_proc/intel/person-detection-0202.json">person-detection-0202.json</a></td>
  </tr>
  <tr>
    <td><a href="https://github.com/openvinotoolkit/open_model_zoo/tree/master/models/intel/person-detection-0203">person-detection-0203</a></td>
    <td>&nbsp;</td>
    <td rowspan="2"><a href="https://github.com/open-edge-platform/dlstreamer/blob/main/samples/gstreamer/model_proc/intel/person-detection-0203.json">person-detection-0203.json</a></td>
  </tr>
  <tr>
    <td><a href="https://github.com/openvinotoolkit/open_model_zoo/tree/master/models/intel/person-detection-asl-0001">person-detection-asl-0001</a></td>
    <td>&nbsp;</td>
  </tr>
  <tr>
    <td><a href="https://github.com/openvinotoolkit/open_model_zoo/tree/master/models/intel/person-detection-retail-0013">person-detection-retail-0013</a></td>
    <td>&nbsp;</td>
    <td><a href="https://github.com/open-edge-platform/dlstreamer/blob/main/samples/gstreamer/model_proc/intel/person-detection-retail-0013.json">person-detection-retail-0013.json</a></td>
  </tr>
  <tr>
    <td><a href="https://github.com/openvinotoolkit/open_model_zoo/tree/master/models/intel/person-vehicle-bike-detection-2000">person-vehicle-bike-detection-2000</a></td>
    <td>&nbsp;</td>
    <td><a href="https://github.com/open-edge-platform/dlstreamer/blob/main/samples/gstreamer/model_proc/intel/person-vehicle-bike-detection-2000.json">person-vehicle-bike-detection-2000.json</a></td>
  </tr>
  <tr>
    <td><a href="https://github.com/openvinotoolkit/open_model_zoo/tree/master/models/intel/person-vehicle-bike-detection-2001">person-vehicle-bike-detection-2001</a></td>
    <td>&nbsp;</td>
    <td><a href="https://github.com/open-edge-platform/dlstreamer/blob/main/samples/gstreamer/model_proc/intel/person-vehicle-bike-detection-2001.json">person-vehicle-bike-detection-2001.json</a></td>
  </tr>
  <tr>
    <td><a href="https://github.com/openvinotoolkit/open_model_zoo/tree/master/models/intel/person-vehicle-bike-detection-2002">person-vehicle-bike-detection-2002</a></td>
    <td>&nbsp;</td>
    <td><a href="https://github.com/open-edge-platform/dlstreamer/blob/main/samples/gstreamer/model_proc/intel/person-vehicle-bike-detection-2002.json">person-vehicle-bike-detection-2002.json</a></td>
  </tr>
  <tr>
    <td><a href="https://github.com/openvinotoolkit/open_model_zoo/tree/master/models/intel/person-vehicle-bike-detection-2003">person-vehicle-bike-detection-2003</a></td>
    <td>&nbsp;</td>
    <td><a href="https://github.com/open-edge-platform/dlstreamer/blob/main/samples/gstreamer/model_proc/intel/person-vehicle-bike-detection-2003.json">person-vehicle-bike-detection-2003.json</a></td>
  </tr>
  <tr>
    <td><a href="https://github.com/openvinotoolkit/open_model_zoo/tree/master/models/intel/person-vehicle-bike-detection-2004">person-vehicle-bike-detection-2004</a></td>
    <td>&nbsp;</td>
    <td><a href="https://github.com/open-edge-platform/dlstreamer/blob/main/samples/gstreamer/model_proc/intel/person-vehicle-bike-detection-2004.json">person-vehicle-bike-detection-2004.json</a></td>
  </tr>
  <tr>
    <td><a href="https://github.com/openvinotoolkit/open_model_zoo/tree/master/models/intel/person-vehicle-bike-detection-crossroad-0078">person-vehicle-bike-detection-crossroad-0078</a></td>
    <td>&nbsp;</td>
    <td><a href="https://github.com/open-edge-platform/dlstreamer/blob/main/samples/gstreamer/model_proc/intel/person-vehicle-bike-detection-crossroad-0078.json">person-vehicle-bike-detection-crossroad-0078.json</a></td>
  </tr>
  <tr>
    <td><a href="https://github.com/openvinotoolkit/open_model_zoo/tree/master/models/intel/person-vehicle-bike-detection-crossroad-1016">person-vehicle-bike-detection-crossroad-1016</a></td>
    <td>&nbsp;</td>
    <td><a href="https://github.com/open-edge-platform/dlstreamer/blob/main/samples/gstreamer/model_proc/intel/person-vehicle-bike-detection-crossroad-1016.json">person-vehicle-bike-detection-crossroad-1016.json</a></td>
  </tr>
  <tr>
    <td><a href="https://github.com/openvinotoolkit/open_model_zoo/tree/master/models/intel/person-vehicle-bike-detection-crossroad-yolov3-1020">person-vehicle-bike-detection-crossroad-yolov3-1020</a></td>
    <td>&nbsp;</td>
    <td><a href="https://github.com/open-edge-platform/dlstreamer/blob/main/samples/gstreamer/model_proc/intel/person-vehicle-bike-detection-crossroad-yolov3-1020.json">person-vehicle-bike-detection-crossroad-yolov3-1020.json</a></td>
  </tr>
  <tr>
    <td><a href="https://github.com/openvinotoolkit/open_model_zoo/tree/master/models/intel/product-detection-0001">product-detection-0001</a></td>
    <td>&nbsp;</td>
    <td><a href="https://github.com/open-edge-platform/dlstreamer/blob/main/samples/gstreamer/model_proc/intel/product-detection-0001.json">product-detection-0001.json</a></td>
  </tr>
  <tr>
    <td><a href="https://github.com/openvinotoolkit/open_model_zoo/tree/master/models/public/retinanet-tf">retinanet-tf</a></td>
    <td><a href="https://github.com/open-edge-platform/dlstreamer/blob/main/samples/labels/coco_80cl.txt">coco_80cl.txt</a></td>
    <td>&nbsp;</td>
  </tr>
  <tr>
    <td><a href="https://github.com/openvinotoolkit/open_model_zoo/tree/master/models/public/rfcn-resnet101-coco-tf">rfcn-resnet101-coco-tf</a></td>
    <td rowspan="4"><a href="https://github.com/open-edge-platform/dlstreamer/blob/main/samples/labels/coco_91cl_bkgr.txt">coco_91cl_bkgr.txt</a></td>
    <td><a href="https://github.com/open-edge-platform/dlstreamer/blob/main/samples/gstreamer/model_proc/public/preproc-image-info.json">preproc-image-info.json</a></td>
  </tr>
  <tr>
    <td><a href="https://github.com/openvinotoolkit/open_model_zoo/tree/master/models/public/ssd_mobilenet_v1_coco">ssd_mobilenet_v1_coco</a></td>
    <td>&nbsp;</td>
  </tr>
  <tr>
    <td><a href="https://github.com/openvinotoolkit/open_model_zoo/tree/master/models/public/ssd_mobilenet_v1_fpn_coco">ssd_mobilenet_v1_fpn_coco</a></td>
    <td>&nbsp;</td>
  </tr>
  <tr>
    <td><a href="https://github.com/openvinotoolkit/open_model_zoo/tree/master/models/public/ssdlite_mobilenet_v2">ssdlite_mobilenet_v2</a></td>
    <td>&nbsp;</td>
  </tr>
  <tr>
    <td><a href="https://pytorch.org/vision/main/models/generated/torchvision.models.detection.ssdlite320_mobilenet_v3_large.html">torchvision.models.detection. ssdlite320_mobilenet_v3_large</a></td>
    <td><a href="https://github.com/open-edge-platform/dlstreamer/blob/main/samples/labels/coco_80cl.txt">coco_80cl.txt</a></td>
    <td>&nbsp;</td>
    <td>&nbsp;</td>
  </tr>
  <tr>
    <td><a href="https://github.com/openvinotoolkit/open_model_zoo/tree/master/models/intel/vehicle-detection-0200">vehicle-detection-0200</a></td>
    <td>&nbsp;</td>
    <td><a href="https://github.com/open-edge-platform/dlstreamer/blob/main/samples/gstreamer/model_proc/intel/vehicle-detection-0200.json">vehicle-detection-0200.json</a></td>
    <td rowspan="4"><a href="https://github.com/openvinotoolkit/open_model_zoo/tree/master/demos/object_detection_demo/cpp">Object Detection Demo</a></td>
  </tr>
  <tr>
    <td><a href="https://github.com/openvinotoolkit/open_model_zoo/tree/master/models/intel/vehicle-detection-0201">vehicle-detection-0201</a></td>
    <td>&nbsp;</td>
    <td><a href="https://github.com/open-edge-platform/dlstreamer/blob/main/samples/gstreamer/model_proc/intel/vehicle-detection-0201.json">vehicle-detection-0201.json</a></td>
  </tr>
  <tr>
    <td><a href="https://github.com/openvinotoolkit/open_model_zoo/tree/master/models/intel/vehicle-detection-0202">vehicle-detection-0202</a></td>
    <td>&nbsp;</td>
    <td><a href="https://github.com/open-edge-platform/dlstreamer/blob/main/samples/gstreamer/model_proc/intel/vehicle-detection-0202.json">vehicle-detection-0202.json</a></td>
  </tr>
  <tr>
    <td><a href="https://github.com/openvinotoolkit/open_model_zoo/tree/master/models/intel/vehicle-detection-adas-0002">vehicle-detection-adas-0002</a></td>
    <td>&nbsp;</td>
    <td><a href="https://github.com/open-edge-platform/dlstreamer/blob/main/samples/gstreamer/model_proc/intel/vehicle-detection-adas-0002.json">vehicle-detection-adas-0002.json</a></td>
  </tr>
  <tr>
    <td><a href="https://github.com/openvinotoolkit/open_model_zoo/tree/master/models/intel/vehicle-license-plate-detection-barrier-0106">vehicle-license-plate-detection-barrier-0106</a></td>
    <td>&nbsp;</td>
    <td><a href="https://github.com/open-edge-platform/dlstreamer/blob/main/samples/gstreamer/model_proc/intel/vehicle-license-plate-detection-barrier-0106.json">vehicle-license-plate-detection-barrier-0106.json</a></td>
    <td rowspan="2"><a href="https://github.com/openvinotoolkit/open_model_zoo/tree/master/demos/security_barrier_camera_demo/cpp">Security Barrier Camera Demo</a></td>
  </tr>
  <tr>
    <td><a href="https://github.com/openvinotoolkit/open_model_zoo/tree/master/models/public/vehicle-license-plate-detection-barrier-0123">vehicle-license-plate-detection-barrier-0123</a></td>
    <td>&nbsp;</td>
    <td><a href="https://github.com/open-edge-platform/dlstreamer/blob/main/samples/gstreamer/model_proc/public/vehicle-license-plate-detection-barrier-0123.json">vehicle-license-plate-detection-barrier-0123.json</a></td>
  </tr>
  <tr>
    <td><a href="https://github.com/openvinotoolkit/open_model_zoo/tree/master/models/public/yolo-v3-tf">yolo-v3-tf</a></td>
    <td rowspan="4"><a href="https://github.com/open-edge-platform/dlstreamer/blob/main/samples/labels/coco_80cl.txt">coco_80cl.txt</a></td>
    <td><a href="https://github.com/open-edge-platform/dlstreamer/blob/main/samples/gstreamer/model_proc/public/yolo-v3-tf.json">yolo-v3-tf.json</a></td>
    <td rowspan="4"><a href="https://github.com/openvinotoolkit/open_model_zoo/tree/master/demos/object_detection_demo/cpp">Object Detection Demo</a></td>
  </tr>
  <tr>
    <td><a href="https://github.com/openvinotoolkit/open_model_zoo/tree/master/models/public/yolo-v3-tiny-tf">yolo-v3-tiny-tf</a></td>
    <td><a href="https://github.com/open-edge-platform/dlstreamer/blob/main/samples/gstreamer/model_proc/public/yolo-v3-tiny-tf.json">yolo-v3-tiny-tf.json</a></td>
  </tr>
  <tr>
    <td><a href="https://github.com/openvinotoolkit/open_model_zoo/tree/master/models/public/yolo-v4-tf">yolo-v4-tf</a></td>
    <td><a href="https://github.com/open-edge-platform/dlstreamer/blob/main/samples/gstreamer/model_proc/public/yolo-v4-tf.json">yolo-v4-tf.json</a></td>
  </tr>
  <tr>
    <td><a href="https://github.com/openvinotoolkit/open_model_zoo/tree/master/models/public/yolo-v4-tiny-tf">yolo-v4-tiny-tf</a></td>
    <td><a href="https://github.com/open-edge-platform/dlstreamer/blob/main/samples/gstreamer/model_proc/public/yolo-v4-tiny-tf.json">yolo-v4-tiny-tf.json</a></td>
  </tr>
  <tr>
    <td>Head Pose Estimation</td>
    <td><a href="https://github.com/openvinotoolkit/open_model_zoo/tree/master/models/intel/head-pose-estimation-adas-0001">head-pose-estimation-adas-0001</a></td>
    <td>&nbsp;</td>
    <td>&nbsp;</td>
    <td><a href="https://github.com/openvinotoolkit/open_model_zoo/tree/master/demos/gaze_estimation_demo/cpp_gapi">Gaze Estimation Demo</a></td>
  </tr>
  <tr>
    <td style="vertical-align:top;" rowspan="2">Human Pose Estimation</td>
    <td><a href="https://github.com/openvinotoolkit/open_model_zoo/tree/master/models/intel/human-pose-estimation-0001">human-pose-estimation-0001</a></td>
    <td>&nbsp;</td>
    <td><a href="https://github.com/open-edge-platform/dlstreamer/blob/main/samples/gstreamer/model_proc/intel/human-pose-estimation-0001.json">human-pose-estimation-0001.json</a></td>
    <td><a href="https://github.com/openvinotoolkit/open_model_zoo/tree/master/demos/multi_channel_human_pose_estimation_demo/cpp">Multi Channel Human Pose Estimation Demo</a></td>
  </tr>
  <tr>
    <td><a href="https://github.com/openvinotoolkit/open_model_zoo/tree/master/models/public/single-human-pose-estimation-0001">single-human-pose-estimation-0001</a></td>
    <td>&nbsp;</td>
    <td><a href="https://github.com/open-edge-platform/dlstreamer/blob/main/samples/gstreamer/model_proc/public/single-human-pose-estimation-0001.json">single-human-pose-estimation-0001.json</a></td>
    <td><a href="https://github.com/openvinotoolkit/open_model_zoo/tree/master/demos/single_human_pose_estimation_demo/python">Single Human Pose Estimation Demo</a></td>
  </tr>
  <tr>
    <td style="vertical-align:top;" rowspan="8">Instance Segmentation</td>
    <td><a href="https://github.com/openvinotoolkit/open_model_zoo/tree/master/models/intel/instance-segmentation-person-0007">instance-segmentation-person-0007</a></td>
    <td>&nbsp;</td>
    <td>&nbsp;</td>
    <td rowspan="6"><a href="https://github.com/openvinotoolkit/open_model_zoo/tree/master/demos/background_subtraction_demo/cpp_gapi">Background Subtraction Demo</a></td>
  </tr>
  <tr>
    <td><a href="https://github.com/openvinotoolkit/open_model_zoo/tree/master/models/intel/instance-segmentation-security-0002">instance-segmentation-security-0002</a></td>
    <td rowspan="5"><a href="https://github.com/open-edge-platform/dlstreamer/blob/main/samples/labels/coco_80cl.txt">coco_80cl.txt</a></td>
    <td>&nbsp;</td>
  </tr>
  <tr>
    <td><a href="https://github.com/openvinotoolkit/open_model_zoo/tree/master/models/intel/instance-segmentation-security-0091">instance-segmentation-security-0091</a></td>
    <td>&nbsp;</td>
  </tr>
  <tr>
    <td><a href="https://github.com/openvinotoolkit/open_model_zoo/tree/master/models/intel/instance-segmentation-security-0228">instance-segmentation-security-0228</a></td>
    <td>&nbsp;</td>
  </tr>
  <tr>
    <td><a href="https://github.com/openvinotoolkit/open_model_zoo/tree/master/models/intel/instance-segmentation-security-1039">instance-segmentation-security-1039</a></td>
    <td>&nbsp;</td>
  </tr>
  <tr>
    <td><a href="https://github.com/openvinotoolkit/open_model_zoo/tree/master/models/intel/instance-segmentation-security-1040">instance-segmentation-security-1040</a></td>
    <td>&nbsp;</td>
  </tr>
  <tr>
    <td><a href="https://github.com/openvinotoolkit/open_model_zoo/tree/master/models/public/mask_rcnn_inception_resnet_v2_atrous_coco">mask_rcnn_inception_resnet_v2_atrous_coco</a></td>
    <td>&nbsp;</td>
    <td rowspan="2"><a href="https://github.com/open-edge-platform/dlstreamer/blob/main/samples/gstreamer/model_proc/public/mask-rcnn.json">mask-rcnn.json</a></td>
    <td rowspan="2"><a href="https://github.com/openvinotoolkit/open_model_zoo/tree/master/demos/mask_rcnn_demo/cpp">Mask RCNN Demo</a></td>
  </tr>
  <tr>
    <td><a href="https://github.com/openvinotoolkit/open_model_zoo/tree/master/models/public/mask_rcnn_resnet50_atrous_coco">mask_rcnn_resnet50_atrous_coco</a></td>
    <td>&nbsp;</td>
  </tr>
  <tr>
    <td style="vertical-align:top;" rowspan="10">Object Attributes</td>
    <td><a href="https://github.com/openvinotoolkit/open_model_zoo/tree/master/models/intel/age-gender-recognition-retail-0013">age-gender-recognition-retail-0013</a></td>
    <td>&nbsp;</td>
    <td><a href="https://github.com/open-edge-platform/dlstreamer/blob/main/samples/gstreamer/model_proc/intel/age-gender-recognition-retail-0013.json">age-gender-recognition-retail-0013.json</a></td>
    <td rowspan="2"><a href="https://github.com/openvinotoolkit/open_model_zoo/tree/master/demos/interactive_face_detection_demo/cpp_gapi">Interactive Face Detection Demo</a></td>
  </tr>
  <tr>
    <td><a href="https://github.com/openvinotoolkit/open_model_zoo/tree/master/models/intel/emotions-recognition-retail-0003">emotions-recognition-retail-0003</a></td>
    <td>&nbsp;</td>
    <td><a href="https://github.com/open-edge-platform/dlstreamer/blob/main/samples/gstreamer/model_proc/intel/emotions-recognition-retail-0003.json">emotions-recognition-retail-0003.json</a></td>
  </tr>
  <tr>
    <td><a href="https://github.com/openvinotoolkit/open_model_zoo/tree/master/models/intel/facial-landmarks-35-adas-0002">facial-landmarks-35-adas-0002</a></td>
    <td>&nbsp;</td>
    <td><a href="https://github.com/open-edge-platform/dlstreamer/blob/main/samples/gstreamer/model_proc/intel/facial-landmarks-35-adas-0002.json">facial-landmarks-35-adas-0002.json</a></td>
    <td><a href="https://github.com/openvinotoolkit/open_model_zoo/tree/master/demos/gaze_estimation_demo/cpp_gapi">Gaze Estimation Demo</a></td>
  </tr>
  <tr>
    <td><a href="https://github.com/openvinotoolkit/open_model_zoo/tree/master/models/intel/facial-landmarks-98-detection-0001">facial-landmarks-98-detection-0001</a></td>
    <td>&nbsp;</td>
    <td><a href="https://github.com/open-edge-platform/dlstreamer/blob/main/samples/gstreamer/model_proc/intel/facial-landmarks-98-detection-0001.json">facial-landmarks-98-detection-0001.json</a></td>
    <td><a href="https://github.com/openvinotoolkit/open_model_zoo/tree/master/demos/gaze_estimation_demo/cpp">Gaze Estimation Demo</a></td>
  </tr>
  <tr>
    <td><a href="https://github.com/openvinotoolkit/open_model_zoo/tree/master/models/intel/landmarks-regression-retail-0009">landmarks-regression-retail-0009</a></td>
    <td>&nbsp;</td>
    <td><a href="https://github.com/open-edge-platform/dlstreamer/blob/main/samples/gstreamer/model_proc/intel/landmarks-regression-retail-0009.json">landmarks-regression-retail-0009.json</a></td>
    <td><a href="https://github.com/openvinotoolkit/open_model_zoo/tree/master/demos/face_recognition_demo/python">Face Recognition Demo</a></td>
  </tr>
  <tr>
    <td><a href="https://github.com/openvinotoolkit/open_model_zoo/tree/master/models/intel/person-attributes-recognition-crossroad-0230">person-attributes-recognition-crossroad-0230</a></td>
    <td>&nbsp;</td>
    <td><a href="https://github.com/open-edge-platform/dlstreamer/blob/main/samples/gstreamer/model_proc/intel/person-attributes-recognition-crossroad-0230.json">person-attributes-recognition-crossroad-0230.json</a></td>
    <td rowspan="3"><a href="https://github.com/openvinotoolkit/open_model_zoo/tree/master/demos/crossroad_camera_demo/cpp">Crossroad Camera Demo</a></td>
  </tr>
  <tr>
    <td><a href="https://github.com/openvinotoolkit/open_model_zoo/tree/master/models/intel/person-attributes-recognition-crossroad-0234">person-attributes-recognition-crossroad-0234</a></td>
    <td>&nbsp;</td>
    <td><a href="https://github.com/open-edge-platform/dlstreamer/blob/main/samples/gstreamer/model_proc/intel/person-attributes-recognition-crossroad-0234.json">person-attributes-recognition-crossroad-0234.json</a></td>
  </tr>
  <tr>
    <td><a href="https://github.com/openvinotoolkit/open_model_zoo/tree/master/models/intel/person-attributes-recognition-crossroad-0238">person-attributes-recognition-crossroad-0238</a></td>
    <td>&nbsp;</td>
    <td><a href="https://github.com/open-edge-platform/dlstreamer/blob/main/samples/gstreamer/model_proc/intel/person-attributes-recognition-crossroad-0238.json">person-attributes-recognition-crossroad-0238.json</a></td>
  </tr>
  <tr>
    <td><a href="https://github.com/openvinotoolkit/open_model_zoo/tree/master/models/intel/vehicle-attributes-recognition-barrier-0039">vehicle-attributes-recognition-barrier-0039</a></td>
    <td>&nbsp;</td>
    <td><a href="https://github.com/open-edge-platform/dlstreamer/blob/main/samples/gstreamer/model_proc/intel/vehicle-attributes-recognition-barrier-0039.json">vehicle-attributes-recognition-barrier-0039.json</a></td>
    <td rowspan="2"><a href="https://github.com/openvinotoolkit/open_model_zoo/tree/master/demos/security_barrier_camera_demo/cpp">Security Barrier Camera Demo</a></td>
  </tr>
  <tr>
    <td><a href="https://github.com/openvinotoolkit/open_model_zoo/tree/master/models/intel/vehicle-attributes-recognition-barrier-0042">vehicle-attributes-recognition-barrier-0042</a></td>
    <td>&nbsp;</td>
    <td><a href="https://github.com/open-edge-platform/dlstreamer/blob/main/samples/gstreamer/model_proc/intel/vehicle-attributes-recognition-barrier-0042.json">vehicle-attributes-recognition-barrier-0042.json</a></td>
  </tr>
  <tr>
    <td>Optical Character Recognition</td>
    <td><a href="https://github.com/openvinotoolkit/open_model_zoo/tree/master/models/public/license-plate-recognition-barrier-0007">license-plate-recognition-barrier-0007</a></td>
    <td>&nbsp;</td>
    <td><a href="https://github.com/open-edge-platform/dlstreamer/blob/main/samples/gstreamer/model_proc/intel/license-plate-recognition-barrier-0007.json">license-plate-recognition-barrier-0007.json</a></td>
    <td><a href="https://github.com/openvinotoolkit/open_model_zoo/tree/master/demos/security_barrier_camera_demo/cpp">Security Barrier Camera Demo</a></td>
  </tr>
  <tr>
    <td>Sound Classification</td>
    <td><a href="https://github.com/openvinotoolkit/open_model_zoo/tree/master/models/public/aclnet">aclnet</a></td>
    <td>&nbsp;</td>
    <td><a href="https://github.com/open-edge-platform/dlstreamer/blob/main/samples/gstreamer/model_proc/public/aclnet.json">aclnet.json</a></td>
    <td><a href="https://github.com/openvinotoolkit/open_model_zoo/tree/master/demos/sound_classification_demo/python">Sound Classification Demo</a></td>
  </tr>
</table>

## Legal Information

PyTorch, TensorFlow, Caffe, Keras, and MXNet are trademarks or brand names of their respective owners.
All company, product, and service names used on this website are for identification purposes only.
Use of these names, trademarks, and brands does not imply endorsement.
