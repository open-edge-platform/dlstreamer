# ==============================================================================
# Copyright (C) 2025-2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================

import unittest
import os

from pipeline_runner import TestPipelineRunner
from utils import get_model_path, BBox

SCRIPT_DIR = os.path.dirname(os.path.realpath(__file__))
IMAGE_PATH = os.path.join(SCRIPT_DIR, "test_files", "dog_bike_car.jpg")

def make_pipeline_str(model_name):
    PIPELINE_STR = """appsrc name=mysrc \
! decodebin ! videoconvert ! video/x-raw,format=BGRA \
! gvadetect pre-process-backend=ie model={} \
! gvawatermark \
! appsink name=mysink emit-signals=true sync=false """.format(get_model_path(model_name))
    return PIPELINE_STR


GOLD_TRUE = [
    BBox(0.16600936461207816, 0.38890058405662487,
         0.4078545726244531, 0.9406926827498019, [], class_id=16),
    BBox(0.6055093107665357, 0.13401658383886872,
         0.8919420810698284, 0.2956839696427691, [], class_id=7),
    BBox(0.1501398097734139, 0.21889342922873212,
         0.7452847887655665, 0.7433104570456628, [], class_id=1)
]

GOLD_TRUE_SEG = [
    BBox(0.17104654567013, 0.37789393034419305,
         0.40481482155345105, 0.939848055539251, [], class_id=16),
    BBox(0.1644484544463225, 0.22790937763317487,
         0.7408722987901015, 0.728528415091759, [], class_id=1),
    BBox(0.6058582396188115, 0.12965262129385202,
         0.9041038647905566, 0.2973447724781151, [], class_id=2)
]

EMPTY_GOLD_TRUE = []


class TestDetectionYoloV11sPipeline(unittest.TestCase):
    def test_detection_yolo_v11s_pipeline(self):
        pipeline_runner = TestPipelineRunner()
        pipeline_runner.set_pipeline(make_pipeline_str("yolo11s"), IMAGE_PATH, GOLD_TRUE)
        pipeline_runner.run_pipeline()
        for e in pipeline_runner.exceptions:
            print(e)
        pipeline_runner.assertEqual(len(pipeline_runner.exceptions), 0,
                                    "Exceptions have been caught.")

    def test_detection_yolo_v11s_obb_pipeline(self):
        pipeline_runner = TestPipelineRunner()
        pipeline_runner.set_pipeline(make_pipeline_str("yolo11s-obb"), IMAGE_PATH, EMPTY_GOLD_TRUE)
        pipeline_runner.run_pipeline()
        for e in pipeline_runner.exceptions:
            print(e)
        pipeline_runner.assertEqual(len(pipeline_runner.exceptions), 0,
                                    "Exceptions have been caught.")

    def test_detection_yolo_v11s_pose_pipeline(self):
        pipeline_runner = TestPipelineRunner()
        pipeline_runner.set_pipeline(make_pipeline_str("yolo11s-pose"), IMAGE_PATH, EMPTY_GOLD_TRUE)
        pipeline_runner.run_pipeline()
        for e in pipeline_runner.exceptions:
            print(e)
        pipeline_runner.assertEqual(len(pipeline_runner.exceptions), 0,
                                    "Exceptions have been caught.")

    def test_detection_yolo_v11s_seg_pipeline(self):
        pipeline_runner = TestPipelineRunner()
        pipeline_runner.set_pipeline(make_pipeline_str("yolo11s-seg"), IMAGE_PATH, GOLD_TRUE_SEG)
        pipeline_runner.run_pipeline()
        for e in pipeline_runner.exceptions:
            print(e)
        pipeline_runner.assertEqual(len(pipeline_runner.exceptions), 0,
                                    "Exceptions have been caught.")


if __name__ == "__main__":
    unittest.main()


