# ==============================================================================
# Copyright (C) 2021-2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================

import unittest
import os
import json
import tempfile

from pipeline_runner import TestPipelineRunner
from tests_gstgva.utils import BBox, get_model_path

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

D_VA_PIPELINE_STR = f"""
appsrc name=mysrc
! jpegparse ! vajpegdec ! video/x-raw(memory:VAMemory)
! gvadetect pre-process-backend=va device=GPU model={D_MODEL_PATH} threshold=0.9
! appsink name=mysink emit-signals=true sync=false
"""

D_VA_SURFACE_SHARING_PIPELINE_STR = f"""
appsrc name=mysrc
! jpegparse ! vajpegdec ! video/x-raw(memory:VAMemory)
! gvadetect pre-process-backend=va-surface-sharing device=GPU model={D_MODEL_PATH} threshold=0.9
! appsink name=mysink emit-signals=true sync=false
"""

D_GOLD_TRUE = [
    BBox(0.582991799458739, 0.3142750898057349, 0.9671368743052611, 0.710030757159208, [], class_id=41, tracker_id=None)
]

C_MODEL_NAME = "resnet50"
C_MODEL_PATH = get_model_path(C_MODEL_NAME)
LABELS_SOURCE_PATH = os.path.join(SCRIPT_DIR, "test_files", "imagenet_custom_pre_proc_resnet.json")
LABELS_FILE_PATH = os.path.join(tempfile.gettempdir(), "imagenet_custom_pre_proc_resnet_labels.txt")


def ensure_labels_file():
    if os.path.isfile(LABELS_FILE_PATH):
        return LABELS_FILE_PATH

    with open(LABELS_SOURCE_PATH, encoding="utf-8") as labels_source_file:
        labels = json.load(labels_source_file)["output_postproc"][0]["labels"]

    with open(LABELS_FILE_PATH, "w", encoding="utf-8") as labels_file:
        labels_file.write("\n".join(labels))
        labels_file.write("\n")

    return LABELS_FILE_PATH


C_OPENCV_PIPELINE_STR = f"""
appsrc name=mysrc
! jpegparse ! jpegdec
! gvaclassify inference-region=full-frame pre-process-backend=opencv device=CPU model={C_MODEL_PATH} labels-file={ensure_labels_file()}
! appsink name=mysink emit-signals=true sync=false
"""

C_VA_PIPELINE_STR = f"""
appsrc name=mysrc
! jpegparse ! vajpegdec ! vapostproc ! video/x-raw(memory:VAMemory)
! gvaclassify inference-region=full-frame pre-process-backend=va device=GPU model={C_MODEL_PATH} labels-file={ensure_labels_file()}
! appsink name=mysink emit-signals=true sync=false
"""

C_VA_SURFACE_SHARING_PIPELINE_STR = f"""
appsrc name=mysrc
! jpegparse ! vajpegdec ! vapostproc ! video/x-raw(memory:VAMemory)
! gvaclassify inference-region=full-frame pre-process-backend=va-surface-sharing device=GPU model={C_MODEL_PATH} labels-file={ensure_labels_file()}
! appsink name=mysink emit-signals=true sync=false
"""

C_GOLD_TRUE = [BBox(0, 0, 1, 1, [])]
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

    def test_custom_va_yolo_11_pipeline(self):
        pipeline_runner = TestPipelineRunner()
        pipeline_runner.set_pipeline(
            D_VA_PIPELINE_STR, IMAGE_PATH, D_GOLD_TRUE)
        pipeline_runner.run_pipeline()
        for e in pipeline_runner.exceptions:
            print(e)
        pipeline_runner.assertEqual(len(pipeline_runner.exceptions), 0,
                                    "Exceptions have been caught.")

    def test_custom_va_surface_sharing_yolo_11_pipeline(self):
        pipeline_runner = TestPipelineRunner()
        pipeline_runner.set_pipeline(
            D_VA_SURFACE_SHARING_PIPELINE_STR, IMAGE_PATH, D_GOLD_TRUE)
        pipeline_runner.run_pipeline()
        for e in pipeline_runner.exceptions:
            print(e)
        pipeline_runner.assertEqual(len(pipeline_runner.exceptions), 0,
                                    "Exceptions have been caught.")

    def test_custom_opencv_resnet_pipeline(self):
        pipeline_runner = TestPipelineRunner()
        pipeline_runner.set_pipeline(
            C_OPENCV_PIPELINE_STR, IMAGE_PATH, C_GOLD_TRUE, check_additional_info=False)
        pipeline_runner.run_pipeline()
        for e in pipeline_runner.exceptions:
            print(e)
        pipeline_runner.assertEqual(len(pipeline_runner.exceptions), 0,
                                    "Exceptions have been caught.")

    def test_custom_va_resnet_pipeline(self):
        pipeline_runner = TestPipelineRunner()
        pipeline_runner.set_pipeline(
            C_VA_PIPELINE_STR, IMAGE_PATH, C_GOLD_TRUE, check_additional_info=False)
        pipeline_runner.run_pipeline()
        for e in pipeline_runner.exceptions:
            print(e)
        pipeline_runner.assertEqual(len(pipeline_runner.exceptions), 0,
                                    "Exceptions have been caught.")

    def test_custom_va_surface_sharing_resnet_pipeline(self):
        pipeline_runner = TestPipelineRunner()
        pipeline_runner.set_pipeline(
            C_VA_SURFACE_SHARING_PIPELINE_STR, IMAGE_PATH, C_GOLD_TRUE, check_additional_info=False)
        pipeline_runner.run_pipeline()
        for e in pipeline_runner.exceptions:
            print(e)
        pipeline_runner.assertEqual(len(pipeline_runner.exceptions), 0,
                                    "Exceptions have been caught.")

if __name__ == "__main__":
    unittest.main()

