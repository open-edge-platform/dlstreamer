# ==============================================================================
# Copyright (C) 2021-2025 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================

import os
import unittest

# pylint: disable=import-error
from pipeline_runner import TestPipelineRunner
from utils import BBox, get_model_path, get_model_proc_path
# pylint: enable=import-error

SCRIPT_DIR = os.path.dirname(os.path.realpath(__file__))
IMAGE_PATH = os.path.join(SCRIPT_DIR, "test_files", "people_detection.png")
DET_MODEL_NAME = "person-detection-retail-0013"
CLS_MODEL_NAME = "single-human-pose-estimation-0001"
MODEL_PROC_NAME = "single-human-pose-estimation-0001"

PIPELINE_STR = f"""appsrc name=mysrc \
! decodebin ! videoconvert ! video/x-raw,format=BGRA \
! gvadetect model={get_model_path(DET_MODEL_NAME)} threshold=0.8 \
! gvaclassify model={get_model_path(CLS_MODEL_NAME)} model-proc={get_model_proc_path(MODEL_PROC_NAME)} pre-process-backend=opencv \
! gvawatermark \
! appsink name=mysink emit-signals=true sync=false """

GOLD_TRUE = [
    BBox(0.7052448987960815, 0.3016299605369568,
         0.8172802925109863, 0.9893561005592346,
         [
             {'format': 'keypoints',
              'layer_name': 'heatmaps',
              'data': [
                  0.72745574, 0.06521739, 0.65284485, 0.06521739, 0.65284485,
                  0.04347826, 0.3544015, 0.10869565, 0.578234, 0.08695652,
                  0.27979067, 0.2173913, 0.72745574, 0.26086956, 0.20517981,
                  0.3478261, 0.80206656, 0.45652175, 0.27979067, 0.47826087,
                  0.9512882, 0.6086956, 0.27979067, 0.5217391, 0.65284485,
                  0.54347825, 0.20517981, 0.7173913, 0.578234, 0.76086956,
                  0.20517981, 0.9130435, 0.42901236, 0.95652175
              ]
              }
         ],
         class_id=0
         ),
    BBox(0.4834686815738678, 0.24317866563796997,
         0.667809247970581, 0.9917208552360535,
         [
             {
                 'format': 'keypoints',
                 'layer_name': 'heatmaps',
                 'data': [
                     0.49979568, 0.13043478, 0.5491582, 0.10869565, 0.49979568,
                     0.08695652, 0.6478833, 0.10869565, 0.7466084, 0.10869565,
                     0.59852076, 0.26086956, 0.84533346, 0.19565217, 0.5491582,
                     0.4347826, 0.49979568, 0.2826087, 0.49979568, 0.5869565,
                     0.20362046, 0.3043478, 0.69724584, 0.54347825, 0.894696,
                     0.5217391, 0.69724584, 0.73913044, 0.84533346, 0.7173913,
                     0.7466084, 0.9130435, 0.84533346, 0.8913044
                 ]
             }
         ],
         class_id=0
         ),
]


class TestSingleHumanPoseEstimation(unittest.TestCase):
    def test_single_human_pose_estimation(self):
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
