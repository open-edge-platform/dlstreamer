# ==============================================================================
# Copyright (C) 2025 Intel Corporation
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
IMAGE_PATH = os.path.join(SCRIPT_DIR, "test_files", "dog_bike_car.jpg")

D_MODEL_NAME = "yolov10s"
D_MODEL_PATH = get_model_path(D_MODEL_NAME)


PIPELINE_STR = """appsrc name=mysrc \
! decodebin ! videoconvert ! video/x-raw,format=BGRA \
! gvadetect pre-process-backend=ie model={} \
! gvawatermark \
! appsink name=mysink emit-signals=true sync=false """.format(D_MODEL_PATH)


GOLD_TRUE = [
    BBox(0.16600936461207816, 0.38890058405662487,
         0.4078545726244531, 0.9406926827498019, [], class_id=16),
    BBox(0.6055093107665357, 0.13401658383886872,
         0.8919420810698284, 0.2956839696427691, [], class_id=7),
    BBox(0.1501398097734139, 0.21889342922873212,
         0.7452847887655665, 0.7433104570456628, [], class_id=1)
]


class TestDetectionYoloV10sPipeline(unittest.TestCase):
    def test_detection_yolo_v10s_pipeline(self):
        pipeline_runner = TestPipelineRunner()
        pipeline_runner.set_pipeline(PIPELINE_STR, IMAGE_PATH, GOLD_TRUE)
        pipeline_runner.run_pipeline()
        for e in pipeline_runner.exceptions:
            print(e)
        pipeline_runner.assertEqual(len(pipeline_runner.exceptions), 0,
                                    "Exceptions have been caught.")


if __name__ == "__main__":
    unittest.main()

