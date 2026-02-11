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
CLS_MODEL_NAME = "human-pose-estimation-0001"
MODEL_PROC_NAME = "human-pose-estimation-0001"

PIPELINE_STR = f"""appsrc name=mysrc \
! decodebin ! videoconvert ! video/x-raw,format=BGRA \
! gvaclassify model={get_model_path(CLS_MODEL_NAME)} model-proc={get_model_proc_path(MODEL_PROC_NAME)} \
    pre-process-backend=opencv inference-region=full-frame \
! gvawatermark \
! appsink name=mysink emit-signals=true sync=false """

GOLD_TRUE = [
    BBox(0, 0, 1, 1,
         [
             {'format': 'keypoints',
              'layer_name': 'Mconv7_stage2_L2\\Mconv7_stage2_L1',
              'data': [0.79256946, 0.3625, 0.7645139, 0.45416668, 0.79256946,
                        0.47916666, 0.7972454, 0.6041667, 0.80659723, 0.6958333,
                        0.7411343, 0.42916667, 0.7785417, 0.3125, 0.8159491,
                        0.24583334, 0.77386576, 0.67083335, 0.77386576, 0.8208333,
                        0.75048614, 0.95416665, 0.73645836, 0.6625, 0.73178244,
                        0.79583335, 0.7224305, 0.9291667, 0.7785417, 0.3375,
                        -1., -1., 0.76918983, 0.35416666, -1.,
                        -1.]
              }
         ]
         ),
    BBox(0, 0, 1, 1,
         [
             {'format': 'keypoints',
              'layer_name': 'Mconv7_stage2_L2\\Mconv7_stage2_L1',
              'data': [0.57747686, 0.32083333, 0.6195602, 0.4125, 0.6382639,
                        0.39583334, 0.57280093, 0.4625, 0.52136576, 0.47916666,
                        0.6008565, 0.42916667, 0.5868287, 0.5625, 0.57280093,
                        0.65416664, 0.64761573, 0.62916666, 0.6382639, 0.77916664,
                        0.6382639, 0.90416664, 0.61488426, 0.6375, 0.6055324,
                        0.79583335, 0.6055324, 0.9291667, -1., -1.,
                        0.5821528, 0.3125, -1., -1., 0.6008565,
                        0.3125]
              }
         ]
         )
]


class TestMultiHumanPoseEstimation(unittest.TestCase):
    def test_multi_human_pose_estimation(self):
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
