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
PEOPLE_IMAGE_PATH = os.path.join(
    SCRIPT_DIR, "test_files", "people_detection.png")
CAR_IMAGE_PATH = os.path.join(SCRIPT_DIR, "test_files", "car_detection.png")
FILE_PATH = os.path.join(SCRIPT_DIR, "meta_vpt.json")

D_MODEL_NAME = "person-vehicle-bike-detection-crossroad-0078"
D_MODEL_PATH, D_MODEL_PROC_PATH = get_model_path(
    D_MODEL_NAME), get_model_proc_path(D_MODEL_NAME)
C1_MODEL_NAME = "person-attributes-recognition-crossroad-0230"
C1_MODEL_PATH, C1_MODEL_PROC_PATH = get_model_path(
    C1_MODEL_NAME), get_model_proc_path(C1_MODEL_NAME)
C2_MODEL_NAME = "vehicle-attributes-recognition-barrier-0039"
C2_MODEL_PATH, C2_MODEL_PROC_PATH = get_model_path(
    C2_MODEL_NAME), get_model_proc_path(C2_MODEL_NAME)


PIPELINE_STR_TEMPLATE = """appsrc name=mysrc ! \
decodebin ! videoconvert ! video/x-raw,format=BGRA ! \
gvadetect model={} model-proc={} inference-interval={} ! queue ! \
gvatrack tracking-type={} ! queue ! \
gvaclassify model={} model-proc={} reclassify-interval=4 object-class=person ! queue ! \
gvaclassify model={} model-proc={} reclassify-interval=4 object-class=vehicle ! queue ! \
gvametaconvert add-tensor-data=true ! gvametapublish file-format=json-lines file-path={} ! \
gvawatermark ! videoconvert ! \
appsink name=mysink emit-signals=true sync=false """


def set_of_pipelines():
    tracker_types = ['short-term-imageless', 'zero-term']
    inference_intervals = [4, 1]
    for tracker_type, interval in zip(tracker_types, inference_intervals):
        pipeline_str = PIPELINE_STR_TEMPLATE.format(D_MODEL_PATH, D_MODEL_PROC_PATH,
                                                    interval, tracker_type,
                                                    C1_MODEL_PATH, C1_MODEL_PROC_PATH,
                                                    C2_MODEL_PATH, C2_MODEL_PROC_PATH, FILE_PATH
                                                    )
        print(pipeline_str)
        yield(pipeline_str)


PEOPLE_GOLD_TRUE = [
    BBox(0.4875, 0.21782178217821782, 0.6875, 0.9851485148514851,
         [
             {
                 'label': "M: has_longpants ",
                 'layer_name': "453",
                 'name': "person-attributes"
             }
         ], tracker_id=1, class_id=1
         ),
    BBox(0.7055555555555556, 0.29207920792079206, 0.8305555555555556, 1.0,
         [
             {
                 "label": "F: has_bag has_longhair ",
                 "layer_name": "453",
                 "name": "person-attributes"
             }
         ], tracker_id=2, class_id=1
         )]

CAR_GOLD_TRUE = [
    BBox(0.10026041666666667, 0.19907407407407407, 0.32421875, 1.0,
         [
             {
                 "label": "white",
                 "layer_name": "color",
                 "name": "color"
             },
             {
                 "label": "car",
                 "layer_name": "type",
                 "name": "type"
             }

         ], tracker_id=1, class_id=2
         ),
    BBox(0.4127604166666667, 0.2523148148148148, 0.6184895833333334, 0.9467592592592593,
         [
             {
                 "label": "red",
                 "layer_name": "color",
                 "name": "color"
             },
             {
                 "label": "car",
                 "layer_name": "type",
                 "name": "type"
             }

         ], tracker_id=2, class_id=2
         )]


class TestVehiclePedestrianTracker(unittest.TestCase):
    def test_pedestrian_tracker_pipeline(self):
        pipeline_runner = TestPipelineRunner()
        for pipeline_str in set_of_pipelines():
            pipeline_runner.set_pipeline(
                pipeline_str, PEOPLE_IMAGE_PATH, PEOPLE_GOLD_TRUE, True)
            pipeline_runner.run_pipeline()

            if os.path.isfile(FILE_PATH):
                os.remove(FILE_PATH)

            for e in pipeline_runner.exceptions:
                print(e)
            pipeline_runner.assertEqual(len(pipeline_runner.exceptions), 0,
                                        "Exceptions have been caught.")

    def test_vehicle_tracker_pipeline(self):
        pipeline_runner = TestPipelineRunner()
        for pipeline_str in set_of_pipelines():
            pipeline_runner.set_pipeline(
                pipeline_str, CAR_IMAGE_PATH, CAR_GOLD_TRUE, True)
            pipeline_runner.run_pipeline()

            if os.path.isfile(FILE_PATH):
                os.remove(FILE_PATH)

            for e in pipeline_runner.exceptions:
                print(e)
            pipeline_runner.assertEqual(len(pipeline_runner.exceptions), 0,
                                        "Exceptions have been caught.")


if __name__ == "__main__":
    unittest.main()
