# ==============================================================================
# Copyright (C) 2021-2025 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================

import unittest
import os

from pipeline_runner import TestPipelineRunner
from utils import BBox, get_model_path

SCRIPT_DIR = os.path.dirname(os.path.realpath(__file__))
IMAGE_PATH = os.path.join(SCRIPT_DIR, "test_files", "dog_bike_car.jpg")
MODULE_PATH = os.path.join(SCRIPT_DIR, "test_files",
                           "instance_segmentation_0002_postproc.py")
MODEL_PROC_PATH = os.path.join(
    SCRIPT_DIR, "test_files", "instance-segmentation-security-0002.json")

MODEL_NAME = "instance-segmentation-security-0002"
MODEL_PATH = get_model_path(MODEL_NAME, precision="FP16-INT8")


PIPELINE_STR = f"""appsrc name=mysrc
! decodebin
! gvainference model={MODEL_PATH} model-proc={MODEL_PROC_PATH}
! gvapython module={MODULE_PATH}
! appsink name=mysink emit-signals=true sync=false"""


GOLD_TRUE = [
    BBox(0, 0, 1, 1),
    BBox(0, 0, 1, 1)
]


class TestInferencePythonPipeline(unittest.TestCase):
    def test_gvainference_gvapython_pipeline(self):
        pipeline_runner = TestPipelineRunner()
        pipeline_runner.set_pipeline(
            PIPELINE_STR, IMAGE_PATH, GOLD_TRUE, check_additional_info=False)
        pipeline_runner.run_pipeline()
        for e in pipeline_runner.exceptions:
            print(e)
        pipeline_runner.assertEqual(len(pipeline_runner.exceptions), 0,
                                    "Exceptions have been caught.")


if __name__ == "__main__":
    unittest.main()
