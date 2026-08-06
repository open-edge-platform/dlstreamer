# ==============================================================================
# Copyright (C) 2021-2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================

import os
import tempfile
import unittest

from pipeline_runner import TestPipelineRunner
from tests_gstgva.utils import BBox, get_model_path, get_model_proc_path

SCRIPT_DIR = os.path.dirname(os.path.realpath(__file__))
IMAGE_PATH = os.path.join(SCRIPT_DIR, "test_files", "face_detection.png")
FILE_PATH = os.path.join(tempfile.gettempdir(), "meta_fdc.json")

D_MODEL_PATH = get_model_path("centerface")
C1_MODEL_NAME = "dima806_fairface_gender_image_detection"
C1_MODEL_PATH = get_model_path(C1_MODEL_NAME)
C2_MODEL_NAME = "dima806_facial_age_image_detection"
C2_MODEL_PATH = get_model_path(C2_MODEL_NAME)

# Previously the color format was BGRA but due to know issue CVS-97946 it has been changed to BGR
# Can be reverted back once issue is resolved
PIPELINE_STR_TEMPLATE = """appsrc name=mysrc ! \
decodebin ! videoconvert ! video/x-raw,format=BGR ! \
gvadetect model={} pre-process-backend={} ! \
gvaclassify model={} pre-process-backend={} ! \
gvaclassify model={} pre-process-backend={} ! \
gvametaconvert add-tensor-data=true ! gvametapublish file-format=json-lines file-path={} ! \
videoconvert ! gvawatermark ! videoconvert ! appsink name=mysink emit-signals=true sync=false """


def set_of_pipelines():
    preprocessors = ['ie', 'opencv']
    for preproc in preprocessors:
        pipeline_str = PIPELINE_STR_TEMPLATE.format(D_MODEL_PATH, preproc,
                                                    C1_MODEL_PATH, preproc,
                                                    C2_MODEL_PATH, preproc, FILE_PATH,
                                                    )
        yield(pipeline_str)


GROUND_TRUTH = [
    BBox(0.692409336566925, 0.1818923056125641, 0.8225383162498474, 0.5060393810272217, [], class_id=0),
    BBox(0.18316425383090973, 0.19858068227767944, 0.30258169770240784, 0.5096779465675354, [], class_id=0)]


class TestFaceDetectionAndClassification(unittest.TestCase):
    def test_face_detection_and_classification_pipeline(self):
        pipeline_runner = TestPipelineRunner()
        for pipeline_str in set_of_pipelines():
            pipeline_runner.set_pipeline(pipeline_str, IMAGE_PATH, GROUND_TRUTH, check_additional_info=False)
            pipeline_runner.run_pipeline()

            if os.path.isfile(FILE_PATH):
                os.remove(FILE_PATH)

            for e in pipeline_runner.exceptions:
                print(e)
            pipeline_runner.assertEqual(len(pipeline_runner.exceptions), 0,
                                        "Exceptions have been caught.")


if __name__ == "__main__":
    unittest.main()
