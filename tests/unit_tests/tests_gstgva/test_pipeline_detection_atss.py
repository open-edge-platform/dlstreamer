# ==============================================================================
# Copyright (C) 2021-2025 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================

import unittest
import os

from pipeline_runner import TestPipelineRunner
from utils import get_model_path, get_model_proc_path, BBox

SCRIPT_DIR = os.path.dirname(os.path.realpath(__file__))
IMAGE_PATH = os.path.join(SCRIPT_DIR, "test_files", "car_detection.png")

d_model_name = "person-vehicle-bike-detection-2004"
d_model_path, d_model_proc_path = get_model_path(
    d_model_name), get_model_proc_path(d_model_name)


PIPELINE_STR = f"""appsrc name=mysrc \
! decodebin ! videoconvert ! video/x-raw,format=BGRA \
! gvadetect model={d_model_path} model-proc={d_model_proc_path} threshold=0.2 \
! gvawatermark \
! appsink name=mysink emit-signals=true sync=false """

GOLD_TRUE = [
    BBox(0.10468322783708572, 0.19903349876403809, 0.32950497418642044, 0.9529740810394287,
         [], class_id=0
         ),
    BBox(0.4107516407966614, 0.28523483872413635, 0.6255635023117065, 0.9386073648929596,
         [],  class_id=0
         ),
    BBox(0.40668997168540955, 0.2672349214553833, 0.6080639958381653, 0.5492938756942749,
         [],  class_id=0)

]


class TestDetectionATSSPipeline(unittest.TestCase):
    def test_detection_atss_pipeline(self):
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
