# ==============================================================================
# Copyright (C) 2020-2025 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================

import unittest
import os

# pylint: disable=import-error
from pipeline_runner import TestPipelineRunner
from utils import *
# pylint: enable=import-error

SCRIPT_DIR = os.path.dirname(os.path.realpath(__file__))
IMAGE_PATH = os.path.join(SCRIPT_DIR, "test_files", "face_detection.png")

d_model_name = "face-detection-adas-0001"
d_model_path = get_model_path(d_model_name)
c_model_name = "emotions-recognition-retail-0003"
c_model_path = get_model_path(c_model_name)
c_model_proc_name = "emotions-recognition-retail-0003"
c_model_proc_path = get_model_proc_path(c_model_proc_name)


PIPELINE_STR = """appsrc name=mysrc \
! decodebin ! videoconvert ! video/x-raw,format=BGRA \
! gvadetect model={} ! queue \
! gvaclassify model={} model-proc={} pre-process-backend=opencv \
! gvawatermark \
! appsink name=mysink emit-signals=true sync=false """.format(d_model_path, c_model_path, c_model_proc_path)

GOLD_TRUE = [
    BBox(0.6926184892654419, 0.18179790675640106, 0.822385311126709, 0.5055984705686569,
         [
             {
                 'label': "surprise",
                 'layer_name': "prob_emotion",
                 'name': "detection"
             }
         ], class_id=0
         ),
    BBox(0.18315134942531586, 0.19866728782653809, 0.30272287130355835, 0.5095890760421753,
         [
             {
                 'label': "happy",
                 'layer_name': "prob_emotion",
                 'name': "emotion"
             }
         ], class_id=0
         )
]


class TestDetectionClassificationPipeline(unittest.TestCase):
    def test_pipeline_face_detection_and_emotions_recognition_retail_0003(self):
        pipeline_runner = TestPipelineRunner()
        pipeline_runner.set_pipeline(PIPELINE_STR,
                                     IMAGE_PATH,
                                     GOLD_TRUE)
        pipeline_runner.run_pipeline()
        for e in pipeline_runner.exceptions:
            print(e)
        pipeline_runner.assertEqual(len(pipeline_runner.exceptions), 0,
                                    "Exceptions have been caught.")


if __name__ == "__main__":
    unittest.main()
