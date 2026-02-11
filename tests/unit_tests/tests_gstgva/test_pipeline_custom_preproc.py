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
IMAGE_PATH = os.path.join(SCRIPT_DIR, "test_files", "cup.jpg")

D_MODEL_NAME = "yolo11s"
D_MODEL_PATH = get_model_path(D_MODEL_NAME)

D_OPENCV_PIPELINE_STR = f"""
appsrc name=mysrc
! jpegparse ! jpegdec
! gvadetect pre-process-backend=opencv device=CPU model={D_MODEL_PATH} threshold=0.9
! appsink name=mysink emit-signals=true sync=false
"""

D_VAAPI_PIPELINE_STR = f"""
appsrc name=mysrc
! jpegparse ! vaapijpegdec ! video/x-raw(memory:VASurface)
! gvadetect pre-process-backend=vaapi device=GPU model={D_MODEL_PATH} threshold=0.9
! appsink name=mysink emit-signals=true sync=false
"""

D_VAAPI_SURFACE_SHARING_PIPELINE_STR = f"""
appsrc name=mysrc
! jpegparse ! vaapijpegdec ! video/x-raw(memory:VASurface)
! gvadetect pre-process-backend=vaapi-surface-sharing device=GPU model={D_MODEL_PATH} threshold=0.9
! appsink name=mysink emit-signals=true sync=false
"""

D_GOLD_TRUE = [
    BBox(0.582991799458739, 0.3142750898057349, 0.9671368743052611, 0.710030757159208, [], class_id=41, tracker_id=None)
]

C_MODEL_NAME = "resnest-50-pytorch"
C_MODEL_PATH = get_model_path(C_MODEL_NAME)
C_MODEL_PROC_PATH = os.path.join(
    SCRIPT_DIR, "test_files", "imagenet_custom_pre_proc_resnet.json")


C_OPENCV_PIPELINE_STR = f"""
appsrc name=mysrc
! jpegparse ! jpegdec
! gvaclassify inference-region=full-frame pre-process-backend=opencv device=CPU model={C_MODEL_PATH} model-proc={C_MODEL_PROC_PATH}
! appsink name=mysink emit-signals=true sync=false
"""

C_VAAPI_PIPELINE_STR = f"""
appsrc name=mysrc
! jpegparse ! vaapijpegdec ! video/x-raw(memory:VASurface)
! gvaclassify inference-region=full-frame pre-process-backend=vaapi device=GPU model={C_MODEL_PATH} model-proc={C_MODEL_PROC_PATH}
! appsink name=mysink emit-signals=true sync=false
"""

C_VAAPI_SURFACE_SHARING_PIPELINE_STR = f"""
appsrc name=mysrc
! jpegparse ! vaapijpegdec ! video/x-raw(memory:VASurface)
! gvaclassify inference-region=full-frame pre-process-backend=vaapi-surface-sharing device=GPU model={C_MODEL_PATH} model-proc={C_MODEL_PROC_PATH}
! appsink name=mysink emit-signals=true sync=false
"""

C_GOLD_TRUE = [BBox(0, 0, 1, 1,
                    [
                        {
                            'label': "espresso",
                            'layer_name': "prob",
                            'name': "ANY"
                        }
                    ]
                    )
               ]


class TestCustomPreProcPipeline(unittest.TestCase):
    def test_custom_opencv_yolo_11_pipeline(self):
        pipeline_runner = TestPipelineRunner()
        pipeline_runner.set_pipeline(
            D_OPENCV_PIPELINE_STR, IMAGE_PATH, D_GOLD_TRUE)
        pipeline_runner.run_pipeline()
        for e in pipeline_runner.exceptions:
            print(e)
        pipeline_runner.assertEqual(len(pipeline_runner.exceptions), 0,
                                    "Exceptions have been caught.")

    def test_custom_vaapi_yolo_11_pipeline(self):
        pipeline_runner = TestPipelineRunner()
        pipeline_runner.set_pipeline(
            D_VAAPI_PIPELINE_STR, IMAGE_PATH, D_GOLD_TRUE)
        pipeline_runner.run_pipeline()
        for e in pipeline_runner.exceptions:
            print(e)
        pipeline_runner.assertEqual(len(pipeline_runner.exceptions), 0,
                                    "Exceptions have been caught.")

    def test_custom_vaapi_surface_sharing_yolo_11_pipeline(self):
        pipeline_runner = TestPipelineRunner()
        pipeline_runner.set_pipeline(
            D_VAAPI_SURFACE_SHARING_PIPELINE_STR, IMAGE_PATH, D_GOLD_TRUE)
        pipeline_runner.run_pipeline()
        for e in pipeline_runner.exceptions:
            print(e)
        pipeline_runner.assertEqual(len(pipeline_runner.exceptions), 0,
                                    "Exceptions have been caught.")

    def test_custom_opencv_resnet_pipeline(self):
        pipeline_runner = TestPipelineRunner()
        pipeline_runner.set_pipeline(
            C_OPENCV_PIPELINE_STR, IMAGE_PATH, C_GOLD_TRUE)
        pipeline_runner.run_pipeline()
        for e in pipeline_runner.exceptions:
            print(e)
        pipeline_runner.assertEqual(len(pipeline_runner.exceptions), 0,
                                    "Exceptions have been caught.")

    def test_custom_vaapi_resnet_pipeline(self):
        pipeline_runner = TestPipelineRunner()
        pipeline_runner.set_pipeline(
            C_VAAPI_PIPELINE_STR, IMAGE_PATH, C_GOLD_TRUE)
        pipeline_runner.run_pipeline()
        for e in pipeline_runner.exceptions:
            print(e)
        pipeline_runner.assertEqual(len(pipeline_runner.exceptions), 0,
                                    "Exceptions have been caught.")

    def test_custom_vaapi_surface_sharing_resnet_pipeline(self):
        pipeline_runner = TestPipelineRunner()
        pipeline_runner.set_pipeline(
            C_VAAPI_SURFACE_SHARING_PIPELINE_STR, IMAGE_PATH, C_GOLD_TRUE)
        pipeline_runner.run_pipeline()
        for e in pipeline_runner.exceptions:
            print(e)
        pipeline_runner.assertEqual(len(pipeline_runner.exceptions), 0,
                                    "Exceptions have been caught.")

if __name__ == "__main__":
    unittest.main()

