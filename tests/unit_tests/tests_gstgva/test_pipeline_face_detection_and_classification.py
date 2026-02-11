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
IMAGE_PATH = os.path.join(SCRIPT_DIR, "test_files", "face_detection.png")
FILE_PATH = os.path.join(SCRIPT_DIR, "meta_fdc.json")

D_MODEL_PATH = get_model_path("face-detection-adas-0001")
C1_MODEL_NAME = "age-gender-recognition-retail-0013"
C1_MODEL_PATH, C1_MODEL_PROC_PATH = get_model_path(
    C1_MODEL_NAME), get_model_proc_path(C1_MODEL_NAME)
C2_MODEL_NAME = "emotions-recognition-retail-0003"
C2_MODEL_PATH, C2_MODEL_PROC_PATH = get_model_path(
    C2_MODEL_NAME), get_model_proc_path(C2_MODEL_NAME)
C3_MODEL_NAME = "landmarks-regression-retail-0009"
C3_MODEL_PATH, C3_MODEL_PROC_PATH = get_model_path(
    C3_MODEL_NAME), get_model_proc_path(C3_MODEL_NAME)

# Previously the color format was BGRA but due to know issue CVS-97946 it has been changed to BGR
# Can be reverted back once issue is resolved
PIPELINE_STR_TEMPLATE = """appsrc name=mysrc ! \
decodebin ! videoconvert ! video/x-raw,format=BGR ! \
gvadetect model={} pre-process-backend={} ! \
gvaclassify model={} model-proc={} pre-process-backend={} ! \
gvaclassify model={} model-proc={} pre-process-backend={} ! \
gvaclassify model={} model-proc={} pre-process-backend={} ! \
gvametaconvert add-tensor-data=true ! gvametapublish file-format=json-lines file-path={} ! \
videoconvert ! gvawatermark ! videoconvert ! appsink name=mysink emit-signals=true sync=false """


def set_of_pipelines():
    preprocessors = ['ie', 'opencv']
    for preproc in preprocessors:
        pipeline_str = PIPELINE_STR_TEMPLATE.format(D_MODEL_PATH, preproc,
                                                    C1_MODEL_PATH, C1_MODEL_PROC_PATH, preproc,
                                                    C2_MODEL_PATH, C2_MODEL_PROC_PATH, preproc,
                                                    C3_MODEL_PATH, C3_MODEL_PROC_PATH, preproc, FILE_PATH,
                                                    )
        yield(pipeline_str)


GROUND_TRUTH_CV = [
    BBox(0.692409336566925, 0.1818923056125641, 0.8225383162498474, 0.5060393810272217,
         [
             {
                 'label': "21",
                 'layer_name': "age_conv3",
                 'name': "age"
             },
             {
                 'label': "Male",
                 'layer_name': "prob",
                 'name': "gender"
             },
             {
                 'label': "surprise",
                 'layer_name': "prob_emotion",
                 'name': "emotion"
             },
             {
                 'data': [
                     0.38138410449028015,
                     0.3854252099990845,
                     0.8030526041984558,
                     0.3897205889225006,
                     0.6947423219680786,
                     0.543339192867279,
                     0.4235397279262543,
                     0.731324315071106,
                     0.79606693983078,
                     0.7303038835525513
                 ],
                 'format': "landmark_points",
                 'layer_name': "95",
                 'name': "UNKNOWN"
             }
         ], class_id=0
         ),
    BBox(0.18316425383090973, 0.19858068227767944, 0.30258169770240784, 0.5096779465675354,
         [
             {
                 'label': "Female",
                 'layer_name': "prob",
                 'name': "gender"
             },
             {
                 'label': "happy",
                 'layer_name': "prob_emotion",
                 'name': "emotion"
             },
             {
                 'label': "25",
                 'layer_name': "age_conv3",
                 'name': "age"
             },
             {
                 'data': [
                     0.2102919965982437,
                     0.36596113443374634,
                     0.6744474172592163,
                     0.37297171354293823,
                     0.36115342378616333,
                     0.5115487575531006,
                     0.19850215315818787,
                     0.7386893033981323,
                     0.6249252557754517,
                     0.7444547414779663
                 ],
                 'format': "landmark_points",
                 'layer_name': "95",
                 'name': "UNKNOWN"
             }
         ], class_id=0,
         )]

GROUND_TRUTH_IE = [
    BBox(0.692409336566925, 0.1818923056125641, 0.8225383162498474, 0.5060393810272217,
         [
             {
                 'label': "21",
                 'layer_name': "age_conv3",
                 'name': "age"
             },
             {
                 'label': "Male",
                 'layer_name': "prob",
                 'name': "gender"
             },
             {
                 'label': "surprise",
                 'layer_name': "prob_emotion",
                 'name': "emotion"
             },
             {
                 'data': [
                     0.38138410449028015,
                     0.3854252099990845,
                     0.8030526041984558,
                     0.3897205889225006,
                     0.6947423219680786,
                     0.543339192867279,
                     0.4235397279262543,
                     0.731324315071106,
                     0.79606693983078,
                     0.7303038835525513
                 ],
                 'format': "landmark_points",
                 'layer_name': "95",
                 'name': "UNKNOWN"
             }
         ], class_id=0
         ),
    BBox(0.18316425383090973, 0.19858068227767944, 0.30258169770240784, 0.5096779465675354,
         [
             {
                 'label': "Female",
                 'layer_name': "prob",
                 'name': "gender"
             },
             {
                 'label': "happy",
                 'layer_name': "prob_emotion",
                 'name': "emotion"
             },
             {
                 'label': "26",
                 'layer_name': "age_conv3",
                 'name': "age"
             },
             {
                 'data': [
                     0.2102919965982437,
                     0.36596113443374634,
                     0.6744474172592163,
                     0.37297171354293823,
                     0.36115342378616333,
                     0.5115487575531006,
                     0.19850215315818787,
                     0.7386893033981323,
                     0.6249252557754517,
                     0.7444547414779663
                 ],
                 'format': "landmark_points",
                 'layer_name': "95",
                 'name': "UNKNOWN"
             }
         ], class_id=0,
         )]


class TestFaceDetectionAndClassification(unittest.TestCase):
    def test_face_detection_and_classification_pipeline(self):
        pipeline_runner = TestPipelineRunner()
        for pipeline_str in set_of_pipelines():

            if "pre-process-backend=opencv" in pipeline_str:
                pipeline_runner.set_pipeline(pipeline_str, IMAGE_PATH, GROUND_TRUTH_CV)
            else:
                pipeline_runner.set_pipeline(pipeline_str, IMAGE_PATH, GROUND_TRUTH_IE)
            pipeline_runner.run_pipeline()

            if os.path.isfile(FILE_PATH):
                os.remove(FILE_PATH)

            for e in pipeline_runner.exceptions:
                print(e)
            pipeline_runner.assertEqual(len(pipeline_runner.exceptions), 0,
                                        "Exceptions have been caught.")


if __name__ == "__main__":
    unittest.main()
