# ==============================================================================
# Copyright (C) 2021-2025 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================

import unittest
import os

# pylint: disable=import-error
from pipeline_runner import TestPipelineRunner
from utils import BBox, get_model_path
# pylint: enable=import-error

SCRIPT_DIR = os.path.dirname(os.path.realpath(__file__))
IMAGE_PATH = os.path.join(SCRIPT_DIR, "test_files", "dog_bike_car.jpg")

d_model_name = "yolo11s"
color_formats = ["BGR", "BGRx", "BGRA", "I420", "NV12"]

d_model_path = get_model_path(d_model_name)

# jpegdec fails to decode+convert to BGR/BGRx directly upon update to GStreamer 1.20
PIPELINE_STR = """appsrc name=mysrc
! jpegdec ! video/x-raw,format=I420 ! videoconvert ! video/x-raw,format={}
! gvadetect model={} threshold=0.2 pre-process-backend=opencv
! appsink name=mysink emit-signals=true sync=false """

GOLD_TRUE = [
    BBox(0.16715965520083387, 0.22123040141508574, 0.7384700885187101, 0.7272685343116798,
         [], class_id=1
         ),
    BBox(0.17164345043366325, 0.38195868355967616, 0.40469565994584045, 0.939372365266666,
         [], class_id=16
         ),
    BBox(0.6076358885710818, 0.129305732686903, 0.9001748219158472, 0.295689319506625,
         [], class_id=2
         )
]


class TestColorFormatsPipeline(unittest.TestCase):
    def test_color_formats_pipeline(self):
        pipeline_runner = TestPipelineRunner()
        for color_format in color_formats:
            pipeline = PIPELINE_STR.format(
                color_format, d_model_path)
            pipeline_runner.set_pipeline(pipeline,
                                         IMAGE_PATH,
                                         GOLD_TRUE)
            pipeline_runner.run_pipeline()
        for e in pipeline_runner.exceptions:
            print(e)
        pipeline_runner.assertEqual(len(pipeline_runner.exceptions), 0,
                                    "Exceptions have been caught.")


if __name__ == "__main__":
    unittest.main()

