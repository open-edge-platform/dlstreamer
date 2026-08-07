# ==============================================================================
# Copyright (C) 2021-2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================

import unittest
import os

from pipeline_runner import TestPipelineRunner
from tests_gstgva.utils import get_model_path, BBox

SCRIPT_DIR = os.path.dirname(os.path.realpath(__file__))
IMAGE_PATH = os.path.join(SCRIPT_DIR, "test_files", "car_detection.png")

d_model_name = "yolo26s"
d_model_path = get_model_path(d_model_name)


PIPELINE_STR = f"""appsrc name=mysrc \
! decodebin ! videoconvert ! video/x-raw,format=BGRA \
! gvadetect model={d_model_path} threshold=0.2 \
! gvawatermark \
! appsink name=mysink emit-signals=true sync=false """

GOLD_TRUE = [
    BBox(0.40796313893740077, 0.2725078257464209, 0.625750694062571, 0.9360087274248043,
        [], class_id=2
        ),
    BBox(0.10680976073160409, 0.20307228122759272, 0.3276979972079097, 0.9512519188411817,
        [], class_id=2
        )

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
